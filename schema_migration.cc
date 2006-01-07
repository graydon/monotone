// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <string>
#include <vector>
#include <locale>
#include <stdexcept>
#include <iostream>
#include <map>

#include <boost/tokenizer.hpp>

#include <sqlite3.h>

#include "sanity.hh"
#include "schema_migration.hh"
#include "botan/botan.h"
#include "app_state.hh"
#include "keys.hh"

// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

// you will notice a little bit of duplicated code between here and
// transforms.cc / database.cc; this was originally to facilitate inclusion of
// migration capability into the depot code, which did not link against those
// objects.  the depot code is gone, but this isn't the sort of code that
// should ever be touched after being written, so the duplication currently
// remains.  if it becomes a maintainence burden, however, consider
// refactoring.

using namespace std;

static int logged_sqlite3_exec(sqlite3* db,
                               const char* sql,
                               sqlite3_callback cb,
                               void* data,
                               char** errmsg)
{
  L(F("executing SQL '%s'") % sql);
  int res = sqlite3_exec(db, sql, cb, data, errmsg);
  L(F("result: %i (%s)") % res % sqlite3_errmsg(db));
  if (errmsg && ((*errmsg)!=0))
    L(F("errmsg: %s") % *errmsg);
  return res;
}

typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

extern "C" {
  const char *sqlite3_value_text_s(sqlite3_value *v);
}

static string 
lowercase(string const & in)
{
  I(40==in.size());
  const int sz=40;
  char buf[sz];
  in.copy(buf, sz);
  locale loc;
  use_facet< ctype<char> >(loc).tolower(buf, buf+sz);
  return string(buf,sz);
}

static void 
massage_sql_tokens(string const & in,
                   string & out)
{
  boost::char_separator<char> sep(" \r\n\t", "(),;");
  tokenizer tokens(in, sep);
  out.clear();
  for (tokenizer::iterator i = tokens.begin();
       i != tokens.end(); ++i)
    {
      if (i != tokens.begin())
        out += " ";
      out += *i;
    }
}

static void 
calculate_id(string const & in,
             string & ident)
{
  Botan::Pipe p(new Botan::Hash_Filter("SHA-1"), new Botan::Hex_Encoder());
  p.process_msg(in);

  ident = lowercase(p.read_all_as_string());
}


struct 
is_ws
{
  bool operator()(char c) const
    {
      return c == '\r' || c == '\n' || c == '\t' || c == ' ';
    }
};

static void 
sqlite_sha1_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  string tmp, sha;
  if (nargs <= 1)
    {
      sqlite3_result_error(f, "need at least 1 arg to sha1()", -1);
      return;
    }

  if (nargs == 1)
    {
      string s = (sqlite3_value_text_s(args[0]));
      s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
      tmp = s;
    }
  else
    {
      string sep = string(sqlite3_value_text_s(args[0]));
      string s = (sqlite3_value_text_s(args[1]));
      s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
      tmp = s;
      for (int i = 2; i < nargs; ++i)
        {
          s = string(sqlite3_value_text_s(args[i]));
          s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
          tmp += sep + s;
        }
    }
  calculate_id(tmp, sha);
  sqlite3_result_text(f,sha.c_str(),sha.size(),SQLITE_TRANSIENT);
}

int 
append_sql_stmt(void * vp, 
                int ncols, 
                char ** values,
                char ** colnames)
{
  if (ncols != 1)
    return 1;
  
  if (vp == NULL)
    return 1;

  if (values == NULL)
    return 1;
  
  if (values[0] == NULL)
    return 1;

  string *str = reinterpret_cast<string *>(vp);
  str->append(values[0]);
  str->append("\n");
  return 0;
}

void 
calculate_schema_id(sqlite3 *sql, string & id)
{
  id.clear();
  string tmp, tmp2;
  int res = logged_sqlite3_exec(sql, 
                                "SELECT sql FROM sqlite_master "
                                "WHERE (type = 'table' OR type = 'index') "
                                // filter out NULL sql statements, because
                                // those are auto-generated indices (for
                                // UNIQUE constraints, etc.).
                                "AND sql IS NOT NULL "
                                "AND name not like 'sqlite_stat%' "
                                "ORDER BY name", 
                                &append_sql_stmt, &tmp, NULL);
  if (res != SQLITE_OK)
    {
      logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
      E(false, F("failure extracting schema from sqlite_master"));
    }
  massage_sql_tokens(tmp, tmp2);
  calculate_id(tmp2, id);
}

typedef bool (*migrator_cb)(sqlite3 *, char **, app_state *);

struct 
migrator
{
  vector< pair<string,migrator_cb> > migration_events;
  app_state * __app;

  void set_app(app_state *app)
  {
    __app = app;
  }

  void add(string schema_id, migrator_cb cb)
  {
    migration_events.push_back(make_pair(schema_id, cb));
  }

  void migrate(sqlite3 *sql, string target_id)
  {
    string init;

    I(sql != NULL);

    calculate_schema_id(sql, init);

    I(!sqlite3_create_function(sql, "sha1", -1, SQLITE_UTF8, NULL,
                               &sqlite_sha1_fn, NULL, NULL));

    bool migrating = false;
    for (vector< pair<string, migrator_cb> >::const_iterator i = migration_events.begin();
         i != migration_events.end(); ++i)
      {

        if (i->first == init)
          {
            E(logged_sqlite3_exec(sql, "BEGIN EXCLUSIVE", NULL, NULL, NULL) == SQLITE_OK,
              F("error at transaction BEGIN statement"));
            migrating = true;
          }

        if (migrating)
          {
            // confirm that we are where we ought to be
            string curr;
            char *errmsg = NULL;
            calculate_schema_id(sql, curr);
            if (curr != i->first)
              {
                if (migrating)
                  logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
                I(false);
              }

            if (i->second == NULL)
              {
                logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
                I(false);
              }

            // do this migration step
            else if (! i->second(sql, &errmsg, __app))
              {
                logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
                E(false, F("migration step failed: %s")
                  % (errmsg ? errmsg : "unknown error"));
              }
          }
      }
    
    // confirm that our target schema was met
    if (migrating)
      {
        string curr;
        calculate_schema_id(sql, curr);
        if (curr != target_id)
          {
            logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
            E(false, F("mismatched result of migration, got %s, wanted %s")
                     % curr % target_id);
          }
        P(F("committing changes to database"));
        E(logged_sqlite3_exec(sql, "COMMIT", NULL, NULL, NULL) == SQLITE_OK,
          F("failure on COMMIT"));

        P(F("optimizing database"));
        E(logged_sqlite3_exec(sql, "VACUUM", NULL, NULL, NULL) == SQLITE_OK,
          F("error vacuuming after migration"));

        E(logged_sqlite3_exec(sql, "ANALYZE", NULL, NULL, NULL) == SQLITE_OK,
          F("error running analyze after migration"));
      }
    else
      {
        // if we didn't do anything, make sure that it's because we were
        // already up to date.
        E(init == target_id,
          F("database schema %s is unknown; cannot perform migration") % init);
        // We really want 'db migrate' on an up-to-date schema to be a no-op
        // (no vacuum or anything, even), so that automated scripts can fire
        // one off optimistically and not have to worry about getting their
        // administrators to do it by hand.
        P(F("no migration performed; database schema already up-to-date at %s\n") % init);
      }
  }
};

static bool move_table(sqlite3 *sql, char **errmsg, 
                       char const * srcname,
                       char const * dstname,
                       char const * dstschema)
{
  string create = "CREATE TABLE ";
  create += dstname;
  create += " ";
  create += dstschema;

  int res = logged_sqlite3_exec(sql, create.c_str(), NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  
  string insert = "INSERT INTO ";
  insert += dstname;
  insert += " SELECT * FROM ";
  insert += srcname;

  res =  logged_sqlite3_exec(sql, insert.c_str(), NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  string drop = "DROP TABLE ";
  drop += srcname;

  res = logged_sqlite3_exec(sql, drop.c_str(), NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
    
  return true;
}


static bool 
migrate_client_merge_url_and_group(sqlite3 * sql, 
                                   char ** errmsg,
                                   app_state *)
{

  // migrate the posting_queue table
  if (!move_table(sql, errmsg, 
                  "posting_queue", 
                  "tmp", 
                  "("
                  "url not null,"
                  "groupname not null,"
                  "content not null"
                  ")"))
    return false;

  int res = logged_sqlite3_exec(sql, "CREATE TABLE posting_queue "
                                "("
                                "url not null, -- URL we are going to send this to\n"
                                "content not null -- the packets we're going to send\n"
                                ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO posting_queue "
                            "SELECT "
                            "(url || '/' || groupname), "
                            "content "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  
  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;


  // migrate the incoming_queue table
  if (!move_table(sql, errmsg, 
                  "incoming_queue", 
                  "tmp", 
                  "("
                  "url not null,"
                  "groupname not null,"
                  "content not null"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE incoming_queue "
                            "("
                            "url not null, -- URL we got this bundle from\n"
                            "content not null -- the packets we're going to read\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO incoming_queue "
                            "SELECT "
                            "(url || '/' || groupname), "
                            "content "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;


  // migrate the sequence_numbers table
  if (!move_table(sql, errmsg, 
                  "sequence_numbers", 
                  "tmp", 
                  "("
                  "url not null,"
                  "groupname not null,"
                  "major not null,"
                  "minor not null,"
                  "unique(url, groupname)"
                  ")"
                  ))
    return false;
  
  res = logged_sqlite3_exec(sql, "CREATE TABLE sequence_numbers "
                            "("
                            "url primary key, -- URL to read from\n"
                            "major not null, -- 0 in news servers, may be higher in depots\n"
                            "minor not null -- last article / packet sequence number we got\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO sequence_numbers "
                            "SELECT "
                            "(url || '/' || groupname), "
                            "major, "
                            "minor "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;


  // migrate the netserver_manifests table
  if (!move_table(sql, errmsg, 
                  "netserver_manifests", 
                  "tmp", 
                  "("
                  "url not null,"
                  "groupname not null,"
                  "manifest not null,"
                  "unique(url, groupname, manifest)"
                  ")"
                  ))
    return false;
  
  res = logged_sqlite3_exec(sql, "CREATE TABLE netserver_manifests "
                            "("
                            "url not null, -- url of some server\n"
                            "manifest not null, -- manifest which exists on url\n"
                            "unique(url, manifest)"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO netserver_manifests "
                            "SELECT "
                            "(url || '/' || groupname), "
                            "manifest "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  
  return true;  
}

static bool 
migrate_client_add_hashes_and_merkle_trees(sqlite3 * sql, 
                                           char ** errmsg,
                                           app_state *)
{

  // add the column to manifest_certs
  if (!move_table(sql, errmsg, 
                  "manifest_certs", 
                  "tmp", 
                  "("
                  "id not null,"
                  "name not null,"
                  "value not null,"
                  "keypair not null,"
                  "signature not null,"
                  "unique(name, id, value, keypair, signature)"
                  ")"))
    return false;

  int res = logged_sqlite3_exec(sql, "CREATE TABLE manifest_certs\n"
                                "(\n"
                                "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
                                "id not null,            -- joins with manifests.id or manifest_deltas.id\n"
                                "name not null,          -- opaque string chosen by user\n"
                                "value not null,         -- opaque blob\n"
                                "keypair not null,       -- joins with public_keys.id\n"
                                "signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
                                "unique(name, id, value, keypair, signature)\n"
                                ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO manifest_certs "
                            "SELECT "
                            "sha1(':', id, name, value, keypair, signature), "
                            "id, name, value, keypair, signature "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // add the column to file_certs
  if (!move_table(sql, errmsg, 
                  "file_certs", 
                  "tmp", 
                  "("
                  "id not null,"
                  "name not null,"
                  "value not null,"
                  "keypair not null,"
                  "signature not null,"
                  "unique(name, id, value, keypair, signature)"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE file_certs\n"
                            "(\n"
                            "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
                            "id not null,            -- joins with files.id or file_deltas.id\n"
                            "name not null,          -- opaque string chosen by user\n"
                            "value not null,         -- opaque blob\n"
                            "keypair not null,       -- joins with public_keys.id\n"
                            "signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
                            "unique(name, id, value, keypair, signature)\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO file_certs "
                            "SELECT "
                            "sha1(':', id, name, value, keypair, signature), "
                            "id, name, value, keypair, signature "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // add the column to public_keys
  if (!move_table(sql, errmsg, 
                  "public_keys", 
                  "tmp", 
                  "("
                  "id primary key,"
                  "keydata not null"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE public_keys\n"
                            "(\n"
                            "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
                            "id primary key,         -- key identifier chosen by user\n"
                            "keydata not null        -- RSA public params\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO public_keys "
                            "SELECT "
                            "sha1(':', id, keydata), "
                            "id, keydata "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // add the column to private_keys
  if (!move_table(sql, errmsg, 
                  "private_keys", 
                  "tmp", 
                  "("
                  "id primary key,"
                  "keydata not null"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE private_keys\n"
                            "(\n"
                            "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
                            "id primary key,         -- as in public_keys (same identifiers, in fact)\n"
                            "keydata not null        -- encrypted RSA private params\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO private_keys "
                            "SELECT "
                            "sha1(':', id, keydata), "
                            "id, keydata "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // add the merkle tree stuff

  res = logged_sqlite3_exec(sql, 
                            "CREATE TABLE merkle_nodes\n"
                            "(\n"
                            "type not null,                -- \"key\", \"mcert\", \"fcert\", \"manifest\"\n"
                            "collection not null,          -- name chosen by user\n"
                            "level not null,               -- tree level this prefix encodes\n"
                            "prefix not null,              -- label identifying node in tree\n"
                            "body not null,                -- binary, base64'ed node contents\n"
                            "unique(type, collection, level, prefix)\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  return true;
}

static bool 
migrate_client_to_revisions(sqlite3 * sql, 
                            char ** errmsg,
                            app_state *)
{
  int res;

  res = logged_sqlite3_exec(sql, "DROP TABLE schema_version;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE posting_queue;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE incoming_queue;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE sequence_numbers;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE file_certs;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE netserver_manifests;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE merkle_nodes;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, 
                            "CREATE TABLE merkle_nodes\n"
                            "(\n"
                            "type not null,                -- \"key\", \"mcert\", \"fcert\", \"rcert\"\n"
                            "collection not null,          -- name chosen by user\n"
                            "level not null,               -- tree level this prefix encodes\n"
                            "prefix not null,              -- label identifying node in tree\n"
                            "body not null,                -- binary, base64'ed node contents\n"
                            "unique(type, collection, level, prefix)\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE revision_certs\n"
                            "(\n"
                            "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
                            "id not null,            -- joins with revisions.id\n"
                            "name not null,          -- opaque string chosen by user\n"
                            "value not null,         -- opaque blob\n"
                            "keypair not null,       -- joins with public_keys.id\n"
                            "signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
                            "unique(name, id, value, keypair, signature)\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE revisions\n"
                            "(\n"
                            "id primary key,      -- SHA1(text of revision)\n"
                            "data not null        -- compressed, encoded contents of a revision\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE revision_ancestry\n"
                            "(\n"
                            "parent not null,     -- joins with revisions.id\n"
                            "child not null,      -- joins with revisions.id\n"
                            "unique(parent, child)\n"
                            ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  
  return true;
}


static bool 
migrate_client_to_epochs(sqlite3 * sql, 
                         char ** errmsg,
                         app_state *)
{
  int res;

  res = logged_sqlite3_exec(sql, "DROP TABLE merkle_nodes;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;


  res = logged_sqlite3_exec(sql, 
                            "CREATE TABLE branch_epochs\n"
                            "(\n"
                            "hash not null unique,         -- hash of remaining fields separated by \":\"\n"
                            "branch not null unique,       -- joins with revision_certs.value\n"
                            "epoch not null                -- random hex-encoded id\n"
                            ");", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  return true;
}

static bool
migrate_client_to_vars(sqlite3 * sql,
                       char ** errmsg,
                       app_state *)
{
  int res;
  
  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE db_vars\n"
                            "(\n"
                            "domain not null,      -- scope of application of a var\n"
                            "name not null,        -- var key\n"
                            "value not null,       -- var value\n"
                            "unique(domain, name)\n"
                            ");", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  return true;
}

static bool
migrate_client_to_add_indexes(sqlite3 * sql,
                              char ** errmsg,
                              app_state *)
{
  int res;
  
  res = logged_sqlite3_exec(sql,
                            "CREATE INDEX revision_ancestry__child "
                            "ON revision_ancestry (child)",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE INDEX revision_certs__id "
                            "ON revision_certs (id);",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE INDEX revision_certs__name_value "
                            "ON revision_certs (name, value);",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  return true;
}

static int
extract_key(void *ptr, int ncols, char **values, char **names)
{
  // This is stupid. The cast should not be needed.
  map<string, string> *out = (map<string, string>*)ptr;
  I(ncols == 2);
  out->insert(make_pair(string(values[0]), string(values[1])));
  return 0;
}
static bool
migrate_client_to_external_privkeys(sqlite3 * sql,
                                    char ** errmsg,
                                    app_state *app)
{
  int res;
  map<string, string> pub, priv;
  vector<keypair> pairs;

  res = logged_sqlite3_exec(sql,
                            "SELECT id, keydata FROM private_keys;",
                            &extract_key, &priv, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "SELECT id, keydata FROM public_keys;",
                            &extract_key, &pub, errmsg);
  if (res != SQLITE_OK)
    return false;

  for (map<string, string>::const_iterator i = priv.begin();
       i != priv.end(); ++i)
    {
      rsa_keypair_id ident = i->first;
      base64< arc4<rsa_priv_key> > old_priv = i->second;
      map<string, string>::const_iterator j = pub.find(i->first);
      keypair kp;
      migrate_private_key(*app, ident, old_priv, kp);
      MM(kp.pub);
      if (j != pub.end())
        {
          base64< rsa_pub_key > pub = j->second;
          MM(pub);
          N(keys_match(ident, pub, ident, kp.pub),
            F("public and private keys for %s don't match") % ident);
        }

      P(F("moving key '%s' from database to %s")
        % ident % app->keys.get_key_dir());
      app->keys.put_key_pair(ident, kp);
    }

  res = logged_sqlite3_exec(sql, "DROP TABLE private_keys;", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  return true;
}

void 
migrate_monotone_schema(sqlite3 *sql, app_state *app)
{

  migrator m;
  m.set_app(app);
  
  m.add("edb5fa6cef65bcb7d0c612023d267c3aeaa1e57a",
        &migrate_client_merge_url_and_group);

  m.add("f042f3c4d0a4f98f6658cbaf603d376acf88ff4b",
        &migrate_client_add_hashes_and_merkle_trees);

  m.add("8929e54f40bf4d3b4aea8b037d2c9263e82abdf4",
        &migrate_client_to_revisions);

  m.add("c1e86588e11ad07fa53e5d294edc043ce1d4005a",
        &migrate_client_to_epochs);

  m.add("40369a7bda66463c5785d160819ab6398b9d44f4",
        &migrate_client_to_vars);

  m.add("e372b508bea9b991816d1c74680f7ae10d2a6d94",
        &migrate_client_to_add_indexes);

  m.add("1509fd75019aebef5ac3da3a5edf1312393b70e9",
        &migrate_client_to_external_privkeys);

  // IMPORTANT: whenever you modify this to add a new schema version, you must
  // also add a new migration test for the new schema version.  See
  // tests/t_migrate_schema.at for details.

  m.migrate(sql, "bd86f9a90b5d552f0be1fa9aee847ea0f317778b");
}
