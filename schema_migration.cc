// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/tokenizer.hpp>
#include <sqlite3.h>

#include "sanity.hh"
#include "schema_migration.hh"
#include "app_state.hh"
#include "keys.hh"
#include "transforms.hh"
#include "ui.hh"

using std::map;
using std::vector;

// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

// you will notice a little bit of duplicated code between here and
// database.cc; this was originally to facilitate inclusion of migration
// capability into the depot code, but is now preserved because the code
// in this file is easier to write and understand if it speaks directly
// to sqlite.

// we do not use sqlite3_exec because we want the better error handling that
// sqlite3_prepare_v2 gives us.

static sqlite3_stmt *
logged_sqlite3_prepare(sqlite3 * sql, char const * cmd)
{
  sqlite3_stmt * stmt;
  char const * after;

  L(FL("executing SQL '%s'") % cmd);

  int res = sqlite3_prepare_v2(sql, cmd, strlen(cmd), &stmt, &after);
  if (res != SQLITE_OK)
    {
      L(FL("prepare failure: %s") % sqlite3_errmsg(sql));
      return 0;
    }
  //I(stmt);
  //I(after == 0);

  return stmt;
}

// this function can only be used with statements that do not return rows.
static int
logged_sqlite3_exec(sqlite3 * sql, char const * cmd,
                    void* d1, void* d2, char **errmsg)
{
  I(d1 == 0);
  I(d2 == 0);

  sqlite3_stmt * stmt = logged_sqlite3_prepare(sql, cmd);
  if (stmt == 0)
    return SQLITE_ERROR;
  //I(sqlite3_column_count(stmt) == 0);

  int res = sqlite3_step(stmt);

  //I(res != SQLITE_ROW);
  if (res == SQLITE_DONE)
    {
      // callers expect OK
      L(FL("success"));
      res = SQLITE_OK;
    }
  else if (errmsg)
    *errmsg = const_cast<char *>(sqlite3_errmsg(sql));

  sqlite3_finalize(stmt);
  return res;
}

static void NORETURN
report_sqlite_error(sqlite3 * sql)
{
  // note: useful error messages should be kept consistent with
  // assert_sqlite3_ok() in database.cc
  char const * errmsg = sqlite3_errmsg(sql);
  char const * auxiliary_message = "";

  L(FL("sqlite error: %s") % errmsg);
  
  if (sqlite3_errcode(sql) == SQLITE_ERROR)
    auxiliary_message
      = _("make sure database and containing directory are writeable\n"
          "and you have not run out of disk space");

  logged_sqlite3_exec(sql, "ROLLBACK", 0, 0, 0);
  E(false, F("sqlite error: %s\n%s") % errmsg % auxiliary_message);
}

// execute an sql statement and return the single integer value that it
// should produce.
static int
logged_sqlite3_exec_int(sqlite3 * sql, char const * cmd)
{
  sqlite3_stmt * stmt = logged_sqlite3_prepare(sql, cmd);
  if (stmt == 0)
    report_sqlite_error(sql);
  //I(sqlite3_column_count(stmt) == 1);

  if (sqlite3_step(stmt) != SQLITE_ROW)
    report_sqlite_error(sql);

  int res = sqlite3_column_int(stmt, 0);

  if (sqlite3_step(stmt) != SQLITE_DONE)
    report_sqlite_error(sql);

  L(FL("success"));

  sqlite3_finalize(stmt);
  return res;
}

// sqlite3_value_text returns unsigned char const *, which is inconvenient
inline char const *
sqlite3_value_cstr(sqlite3_value * arg)
{
  return reinterpret_cast<char const *>(sqlite3_value_text(arg));
}

// sqlite3_column_text also returns unsigned char const *
inline string
sqlite3_column_string(sqlite3_stmt * stmt, int col)
{
  return string(reinterpret_cast<char const *>(sqlite3_column_text(stmt, col)));
}

inline bool is_ws(char c)
{
  return c == '\r' || c == '\n' || c == '\t' || c == ' ';
}

static void
sqlite_sha1_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs <= 1)
    {
      sqlite3_result_error(f, "need at least 1 arg to sha1()", -1);
      return;
    }

  string tmp;
  if (nargs == 1)
    {
      char const * s = sqlite3_value_cstr(args[0]);
      char const * end = s + sqlite3_value_bytes(args[0]) - 1;
      remove_copy_if(s, end, back_inserter(tmp), is_ws);
    }
  else
    {
      char const * sep = sqlite3_value_cstr(args[0]);

      for (int i = 1; i < nargs; ++i)
        {
          if (i > 1)
            tmp += sep;
          char const * s = sqlite3_value_cstr(args[i]);
          char const * end = s + sqlite3_value_bytes(args[i]) - 1;
          remove_copy_if(s, end, back_inserter(tmp), is_ws);
        }
    }

  hexenc<id> sha;
  calculate_ident(data(tmp), sha);
  sqlite3_result_text(f, sha().c_str(), sha().size(), SQLITE_TRANSIENT);
}

void
calculate_schema_id(sqlite3 *sql, string & ident)
{
  sqlite3_stmt * stmt
    = logged_sqlite3_prepare(sql,
                             "SELECT sql FROM sqlite_master "
                             "WHERE (type = 'table' OR type = 'index') "
                             // filter out NULL sql statements, because
                             // those are auto-generated indices (for
                             // UNIQUE constraints, etc.).
                             "AND sql IS NOT NULL "
                             "AND name not like 'sqlite_stat%' "
                             "ORDER BY name");
  if (!stmt)
    report_sqlite_error(sql);
  //I(sqlite3_column_count(stmt) == 1);

  int res;
  string schema;
  using boost::char_separator;
  typedef boost::tokenizer<char_separator<char> > tokenizer;
  char_separator<char> sep(" \r\n\t", "(),;");

  while ((res = sqlite3_step(stmt)) == SQLITE_ROW)
    {
      string table_schema(sqlite3_column_string(stmt, 0));
      tokenizer tokens(table_schema, sep);
      for (tokenizer::iterator i = tokens.begin(); i != tokens.end(); i++)
        {
          if (schema.size() != 0)
            schema += " ";
          schema += *i;
        }
    }

  if (res != SQLITE_DONE)
    report_sqlite_error(sql);

  sqlite3_finalize(stmt);
  L(FL("success"));

  hexenc<id> tid;
  calculate_ident(data(schema), tid);
  ident = tid();
}

// these must be listed in order so that ones listed earlier override ones
// listed later
enum upgrade_regime
  {
    upgrade_changesetify,
    upgrade_rosterify,
    upgrade_regen_caches,
    upgrade_none, 
  };

static void
set_regime(upgrade_regime new_regime, upgrade_regime & regime)
{
  regime = std::min(new_regime, regime);
}

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
                                   app_state *,
                                   upgrade_regime & regime)
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
                                           app_state *,
                                           upgrade_regime & regime)
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
                            app_state *,
                            upgrade_regime & regime)
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

  set_regime(upgrade_changesetify, regime);

  return true;
}


static bool
migrate_client_to_epochs(sqlite3 * sql,
                         char ** errmsg,
                         app_state *,
                         upgrade_regime & regime)
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
                       app_state *,
                       upgrade_regime & regime)
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
                              app_state *,
                              upgrade_regime & regime)
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

static bool
migrate_client_to_external_privkeys(sqlite3 * sql,
                                    char ** errmsg,
                                    app_state *app,
                                    upgrade_regime & regime)
{
  int res;
  map<string, string> pub, priv;
  vector<keypair> pairs;
  sqlite3_stmt * stmt;

  stmt = logged_sqlite3_prepare(sql, "SELECT id, keydata FROM private_keys;");
  if (stmt == 0)
    {
      *errmsg = const_cast<char *>(sqlite3_errmsg(sql));
      return false;
    }
  //I(sqlite3_column_count(stmt) == 2);

  while ((res = sqlite3_step(stmt)) == SQLITE_ROW)
    priv.insert(make_pair(sqlite3_column_string(stmt, 0),
                          sqlite3_column_string(stmt, 1)));

  if (res != SQLITE_DONE)
    {
      *errmsg = const_cast<char *>(sqlite3_errmsg(sql));
      return false;
    }
  sqlite3_finalize(stmt);

  stmt = logged_sqlite3_prepare(sql, "SELECT id, keydata FROM public_keys;");
  if (stmt == 0)
    {
      *errmsg = const_cast<char *>(sqlite3_errmsg(sql));
      return false;
    }
  //I(sqlite3_column_count(stmt) == 2);

  while ((res = sqlite3_step(stmt)) == SQLITE_ROW)
    pub.insert(make_pair(sqlite3_column_string(stmt, 0),
                         sqlite3_column_string(stmt, 1)));

  if (res != SQLITE_DONE)
    {
      *errmsg = const_cast<char *>(sqlite3_errmsg(sql));
      return false;
    }
  sqlite3_finalize(stmt);


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

static bool
migrate_client_to_add_rosters(sqlite3 * sql,
                              char ** errmsg,
                              app_state *,
                              upgrade_regime & regime)
{
  int res;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE rosters\n"
                            "(\n"
                            "id primary key,         -- strong hash of the roster\n"
                            "data not null           -- compressed, encoded contents of the roster\n"
                            ");",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE roster_deltas\n"
                            "(\n"
                            "id not null,            -- strong hash of the roster\n"
                            "base not null,          -- joins with either rosters.id or roster_deltas.id\n"
                            "delta not null,         -- rdiff to construct current from base\n"
                            "unique(id, base)\n"
                            ");",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE revision_roster\n"
                            "(\n"
                            "rev_id primary key,     -- joins with revisions.id\n"
                            "roster_id not null      -- joins with either rosters.id or roster_deltas.id\n"
                            ");",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE next_roster_node_number\n"
                            "(\n"
                            "node primary key        -- only one entry in this table, ever\n"
                            ");",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  set_regime(upgrade_rosterify, regime);

  return true;
}

static void
sqlite3_unbase64_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unbase64()", -1);
      return;
    }
  data decoded;
  decode_base64(base64<data>(string(sqlite3_value_cstr(args[0]))), decoded);
  sqlite3_result_blob(f, decoded().c_str(), decoded().size(), SQLITE_TRANSIENT);
}

// I wish I had a form of ALTER TABLE COMMENT on sqlite3
static bool
migrate_files_BLOB(sqlite3 * sql,
                   char ** errmsg,
                   app_state *app,
                   upgrade_regime & regime)
{
  int res;
  I(sqlite3_create_function(sql, "unbase64", -1,
                           SQLITE_UTF8, NULL,
                           &sqlite3_unbase64_fn,
                           NULL, NULL) == 0);
  // change the encoding of file(_delta)s
  if (!move_table(sql, errmsg,
                  "files",
                  "tmp",
                  "("
                  "id primary key,"
                  "data not null"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE files\n"
                            "\t(\n"
                            "\tid primary key,   -- strong hash of file contents\n"
                            "\tdata not null     -- compressed contents of a file\n"
                            "\t)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO files "
                            "SELECT id, unbase64(data) "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  if (!move_table(sql, errmsg,
                  "file_deltas",
                  "tmp",
                  "("
                  "id not null,"
                  "base not null,"
                  "delta not null"
                  ")"))
    return false;

  res = logged_sqlite3_exec(sql, "CREATE TABLE file_deltas\n"
                            "\t(\n"
                            "\tid not null,      -- strong hash of file contents\n"
                            "\tbase not null,    -- joins with files.id or file_deltas.id\n"
                            "\tdelta not null,   -- compressed rdiff to construct current from base\n"
                            "\tunique(id, base)\n"
                            "\t)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "INSERT INTO file_deltas "
                            "SELECT id, base, unbase64(delta) "
                            "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // migrate other contents which are accessed by get|put_version
  res = logged_sqlite3_exec(sql, "UPDATE manifests SET data=unbase64(data)",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE manifest_deltas "
                            "SET delta=unbase64(delta)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE rosters SET data=unbase64(data) ",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE roster_deltas "
                            "SET delta=unbase64(delta)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql, "UPDATE db_vars "
      "SET value=unbase64(value),name=unbase64(name)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE public_keys "
      "SET keydata=unbase64(keydata)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE revision_certs "
      "SET value=unbase64(value),signature=unbase64(signature)",
      NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE manifest_certs "
      "SET value=unbase64(value),signature=unbase64(signature) ",
      NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE revisions "
      "SET data=unbase64(data)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "UPDATE branch_epochs "
      "SET branch=unbase64(branch)", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  return true;
}

static bool
migrate_rosters_no_hash(sqlite3 * sql,
                        char ** errmsg,
                        app_state * app,
                        upgrade_regime & regime)
{
  int res;
  
  res = logged_sqlite3_exec(sql, "DROP TABLE rosters", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "DROP TABLE roster_deltas", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  res = logged_sqlite3_exec(sql, "DROP TABLE revision_roster", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE rosters\n"
                            "\t(\n"
                            "\tid primary key,         -- a revision id\n"
                            "\tchecksum not null,      -- checksum of 'data', to protect against disk corruption\n"
                            "\tdata not null           -- compressed, encoded contents of the roster\n"
                            "\t);",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE roster_deltas\n"
                            "\t(\n"
                            "\tid primary key,         -- a revision id\n"
                            "\tchecksum not null,      -- checksum of 'delta', to protect against disk corruption\n"
                            "\tbase not null,          -- joins with either rosters.id or roster_deltas.id\n"
                            "\tdelta not null          -- rdiff to construct current from base\n"
                            ");",
                            NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  set_regime(upgrade_regen_caches, regime);

  return true;
}


static bool
migrate_add_heights(sqlite3 *sql,
                    char ** errmsg,
                    app_state *app,
                    upgrade_regime & regime)
{
  int res;

  res = logged_sqlite3_exec(sql,
                            "CREATE TABLE heights\n"
                            "(\n"
                            "revision not null,	-- joins with revisions.id\n"
                            "height not null,	-- complex height, array of big endian u32 integers\n"
                            "unique(revision, height)\n"
                            ");", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  set_regime(upgrade_regen_caches, regime);
  
  return true;
}

typedef bool (*migrator_cb)(sqlite3 *, char **, app_state *, upgrade_regime &);
struct migration_event
{
  char const * id;
  migrator_cb migrator;
};

// IMPORTANT: whenever you modify this to add a new schema version, you must
// also add a new migration test for the new schema version.  See
// tests/schema_migration for details.

const migration_event migration_events[] = {
  { "edb5fa6cef65bcb7d0c612023d267c3aeaa1e57a",
    migrate_client_merge_url_and_group },

  { "f042f3c4d0a4f98f6658cbaf603d376acf88ff4b",
    migrate_client_add_hashes_and_merkle_trees },

  { "8929e54f40bf4d3b4aea8b037d2c9263e82abdf4",
    migrate_client_to_revisions },

  { "c1e86588e11ad07fa53e5d294edc043ce1d4005a",
    migrate_client_to_epochs },

  { "40369a7bda66463c5785d160819ab6398b9d44f4",
    migrate_client_to_vars },

  { "e372b508bea9b991816d1c74680f7ae10d2a6d94",
    migrate_client_to_add_indexes },

  { "1509fd75019aebef5ac3da3a5edf1312393b70e9",
    migrate_client_to_external_privkeys },

  { "bd86f9a90b5d552f0be1fa9aee847ea0f317778b",
    migrate_client_to_add_rosters },

  { "1db80c7cee8fa966913db1a463ed50bf1b0e5b0e",
    migrate_files_BLOB },

  { "9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df",
    migrate_rosters_no_hash },

  { "ae196843d368d042f475e3dadfed11e9d7f9f01e",
    migrate_add_heights },

  // The last entry in this table should always be the current
  // schema ID, with 0 for the migrator.

  { "48fd5d84f1e5a949ca093e87e5ac558da6e5956d", 0 }
};
const size_t n_migration_events = (sizeof migration_events
                                   / sizeof migration_events[0]);

// Look through the migration_events table and return the index of the
// entry corresponding to schema ID, or -1 if it isn't there (i.e. if
// the database schema is not one we know).
static int
schema_to_migration(string const & id)
{
  int i;
  for (i = n_migration_events - 1; i >= 0; i--)
    if (migration_events[i].id == id)
      break;

  return i;
}

// Provide sensible diagnostics for a database schema whose hash we do not
// recognize.
static void NORETURN
diagnose_unrecognized_schema(sqlite3 * sql, system_path const & filename,
                             string const & id)
{
  // Give a special message for an utterly empty sqlite3 database, such as
  // is created by "mtn db load < /dev/null", or by the sqlite3 command line
  // utility if you don't give it anything to do.
  N(id != "da39a3ee5e6b4b0d3255bfef95601890afd80709",
    F("cannot use the empty sqlite database %s\n"
      "(monotone databases must be created with '%s db init')")
    % filename % ui.prog_name);

  // Do a sanity check to make sure we are actually looking at a monotone
  // database, not some other sqlite3 database.  Every version of the schema
  // has included tables named 'files', 'file_deltas', 'manifests', and
  // 'manifest_deltas'.
  // ??? Use PRAGMA user_version to record an additional magic number in
  // monotone databases.
  int n = logged_sqlite3_exec_int(sql,
                                  "SELECT COUNT(*) FROM sqlite_master "
                                  "WHERE type = 'table' AND sql IS NOT NULL "
                                  "AND (name = 'files' OR name = 'file_deltas'"
                                  "     OR name = 'manifests'"
                                  "     OR name = 'manifest_deltas')");
  N(n == 4,
    F("%s does not appear to be a monotone database\n"
      "(schema %s, core tables missing)")
    % filename % id);

  N(false,
    F("%s appears to be a monotone database, but this version of\n"
      "monotone does not recognize its schema (%s).\n"
      "you probably need a newer version of monotone.")
    % filename % id);
}
 
// check_sql_schema is called by database.cc on open, to determine whether
// the schema is up to date.  If it returns at all, the schema is indeed
// up to date (otherwise it throws a diagnostic).
void
check_sql_schema(sqlite3 * sql, system_path const & filename)
{
  I(sql != NULL);
 
  string id;
  calculate_schema_id(sql, id);
 
  int migration = schema_to_migration(id);
 
  if (migration == -1)
    diagnose_unrecognized_schema(sql, filename, id);
 
  N(migration_events[migration].migrator == 0,
    F("database %s is laid out according to an old schema, %s\n"
      "try '%s db migrate' to upgrade\n"
      "(this is irreversible; you may want to make a backup copy first)")
    % filename % id % ui.prog_name);
}

void
migrate_monotone_schema(sqlite3 * sql, app_state * app)
{
  I(sql != NULL);
  I(!sqlite3_create_function(sql, "sha1", -1, SQLITE_UTF8, NULL,
                             &sqlite_sha1_fn, NULL, NULL));

  string init;
  calculate_schema_id(sql, init);

  P(F("calculating migration for schema %s") % init);

  int i = schema_to_migration(init);

  if (i == -1)
    diagnose_unrecognized_schema(sql, app->db.get_filename(), init);

  // We really want 'db migrate' on an up-to-date schema to be a no-op
  // (no vacuum or anything, even), so that automated scripts can fire
  // one off optimistically and not have to worry about getting their
  // administrators to do it by hand.
  if (migration_events[i].migrator == 0)
    {
      P(F("no migration performed; database schema already up-to-date"));
      return;
    }

  upgrade_regime regime = upgrade_none;
  P(F("migrating data"));

  E(logged_sqlite3_exec(sql, "BEGIN EXCLUSIVE", NULL, NULL, NULL) == SQLITE_OK,
    F("error at transaction BEGIN statement: %s") % sqlite3_errmsg(sql));

  for (;;)
    {
      // confirm that we are where we ought to be
      string curr;
      calculate_schema_id(sql, curr);
      if (curr != migration_events[i].id)
        {
          logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
          I(false);
        }

      if (migration_events[i].migrator == 0)
        break;

      char * errmsg;
      if (! migration_events[i].migrator(sql, &errmsg, app, regime))
        {
          logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
          E(false, F("migration step failed: %s")
            % (errmsg ? errmsg : "unknown error"));
        }

      i++;
      if ((size_t)i >= n_migration_events)
        {
          logged_sqlite3_exec(sql, "ROLLBACK", NULL, NULL, NULL);
          I(false);
        }
    }

  P(F("committing changes to database"));
  E(logged_sqlite3_exec(sql, "COMMIT", NULL, NULL, NULL) == SQLITE_OK,
    F("failure on COMMIT"));

  P(F("optimizing database"));
  logged_sqlite3_exec(sql, "VACUUM", NULL, NULL, NULL);

  switch (regime)
    {
    case upgrade_changesetify:
    case upgrade_rosterify:
      {
        string command_str = (regime == upgrade_changesetify
                              ? "changesetify" : "rosterify");
        P(F("NOTE: because this database was last used by a rather old version\n"
            "of monotone, you're not done yet.  If you're a project leader, then\n"
            "see the file UPGRADE for instructions on running '%s db %s'")
          % ui.prog_name % command_str);
      }
      break;
    case upgrade_regen_caches:
      P(F("NOTE: this upgrade cleared monotone's caches\n"
          "you should now run '%s db regenerate_caches'")
        % ui.prog_name);
      break;
    case upgrade_none:
      break;
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
