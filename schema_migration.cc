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

#include <boost/tokenizer.hpp>

#include "schema_migration.hh"
#include "sqlite/sqlite.h"
#include "cryptopp/filters.h"
#include "cryptopp/sha.h"
#include "cryptopp/hex.h"

// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

// you will notice a little bit of duplicated code between here and
// transforms.cc / database.cc; this is to facilitate includion of
// migration capability into the depot code, which does not link against
// those objects.

using namespace std;

typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

static string 
lowercase(string const & in)
{
  size_t const sz = in.size();
  char buf[sz];
  in.copy(buf, sz);
  use_facet< ctype<char> >(locale::locale()).tolower(buf, buf+sz);
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
  CryptoPP::SHA hash;
  unsigned int const sz = 2 * CryptoPP::SHA::DIGESTSIZE;
  char buffer[sz];
  CryptoPP::StringSource 
    s(in, true, new CryptoPP::HashFilter
      (hash, new CryptoPP::HexEncoder
       (new CryptoPP::ArraySink(reinterpret_cast<byte *>(buffer), sz))));
  ident = lowercase(string(buffer, sz));
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
sqlite_sha1_fn(sqlite_func *f, int nargs, char const ** args)
{
  string tmp, sha;
  if (nargs <= 1)
    {
      sqlite_set_result_error(f, "need at least 1 arg to sha1()", -1);
      return;
    }

  if (nargs == 1)
    {
      string s = (args[0]);
      s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
      tmp = s;
    }
  else
    {
      string sep = string(args[0]);
      string s = (args[1]);
      s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
      tmp = s;
      for (int i = 2; i < nargs; ++i)
	{
	  s = string(args[i]);
	  s.erase(remove_if(s.begin(), s.end(), is_ws()),s.end());
	  tmp += sep + s;
	}
    }
  calculate_id(tmp, sha);
  sqlite_set_result_string(f,sha.c_str(),sha.size());
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
calculate_schema_id(sqlite *sql, string & id)
{
  id.clear();
  string tmp, tmp2;
  int res = sqlite_exec_printf(sql, 
			       "SELECT sql FROM sqlite_master "
			       "WHERE type = 'table' "
			       "ORDER BY name", 
			       &append_sql_stmt, &tmp, NULL);
  if (res != SQLITE_OK)
    {
      sqlite_exec(sql, "ROLLBACK", NULL, NULL, NULL);
      throw runtime_error("failure extracting schema from sqlite_master");
    }
  massage_sql_tokens(tmp, tmp2);
  calculate_id(tmp2, id);
}

typedef bool (*migrator_cb)(sqlite *, char **);

struct 
migrator
{
  vector< pair<string,migrator_cb> > migration_events;

  void add(string schema_id, migrator_cb cb)
  {
    migration_events.push_back(make_pair(schema_id, cb));
  }

  void migrate(sqlite *sql, string target_id)
  {
    string init;
    calculate_schema_id(sql, init);

    if (sql == NULL)
      throw runtime_error("NULL sqlite object given to migrate");

    if (sqlite_create_function(sql, "sha1", -1, &sqlite_sha1_fn, NULL))
      throw runtime_error("error registering sha1 function with sqlite");

    bool migrating = false;
    for (vector< pair<string, migrator_cb> >::const_iterator i = migration_events.begin();
	 i != migration_events.end(); ++i)
      {

	if (i->first == init)
	  {
	    if (sqlite_exec(sql, "BEGIN", NULL, NULL, NULL) != SQLITE_OK)
	      throw runtime_error("error at transaction BEGIN statement");	    
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
		  sqlite_exec(sql, "ROLLBACK", NULL, NULL, NULL);
		throw runtime_error("mismatched pre-state to migration step");
	      }

	    if (i->second == NULL)
	      {
		sqlite_exec(sql, "ROLLBACK", NULL, NULL, NULL);
		throw runtime_error("NULL migration specifier");
	      }

	    // do this migration step
	    else if (! i->second(sql, &errmsg))
	      {
		string e("migration step failed");
		if (errmsg != NULL)
		  e.append(string(": ") + errmsg);
		sqlite_exec(sql, "ROLLBACK", NULL, NULL, NULL);
		throw runtime_error(e);
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
	    sqlite_exec(sql, "ROLLBACK", NULL, NULL, NULL);
	    throw runtime_error("mismatched result of migration, "
				"got " + curr + ", wanted " + target_id);
	  }
	if (sqlite_exec(sql, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
	  {
	    throw runtime_error("failure on COMMIT");
	  }
      }
  }
};

static bool move_table(sqlite *sql, char **errmsg, 
		       char const * srcname,
		       char const * dstname,
		       char const * dstschema)
{
  int res = 
    sqlite_exec_printf(sql, "CREATE TABLE %s %s", NULL, NULL, errmsg,
		       dstname, dstschema);
  if (res != SQLITE_OK)
    return false;
  
  res = 
    sqlite_exec_printf(sql, "INSERT INTO %s SELECT * FROM %s",
		       NULL, NULL, errmsg, dstname, srcname);
  if (res != SQLITE_OK)
    return false;
  
  res = 
    sqlite_exec_printf(sql, "DROP TABLE %s",
		       NULL, NULL, errmsg, srcname);
  if (res != SQLITE_OK)
    return false;
    
  return true;
}


static bool 
migrate_depot_split_seqnumbers_into_groups(sqlite * sql, 
					   char ** errmsg)
{

  // this migration event handles the bug related to sequence numbers only
  // being assigned on a per-depot, rather than per-group basis.  in the
  // process it also corrects the false UNIQUE constraint on contents
  // (which may occur multiple times in different groups).
  //
  // after this migration event, all major sequence numbers are bumped, so
  // your clients will re-fetch (idempotently) the contents of your depot.

  if (!move_table(sql, errmsg, 
		  "packets", 
		  "tmp", 
		  "("
		  "major      INTEGER,"
		  "minor      INTEGER,"
		  "groupname  TEXT NOT NULL,"
		  "adler32    TEXT NOT NULL,"
		  "contents   TEXT NOT NULL UNIQUE,"
		  "unique(major, minor)"
		  ")"))
    return false;

  int res = 
    sqlite_exec(sql,
		" UPDATE tmp SET major = \n"
		"   (SELECT MAX(major) + 1 FROM tmp);\n",
		NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  if (!move_table(sql, errmsg, 
		  "tmp", 
		  "packets", 
		  "("
		  "major      INTEGER,"
		  "minor      INTEGER,"
		  "groupname  TEXT NOT NULL,"
		  "adler32    TEXT NOT NULL,"
		  "contents   TEXT NOT NULL,"
		  "unique(groupname, contents),"
		  "unique(major, minor, groupname)"
		  ")"))
    return false;

  return true;
}

static bool migrate_depot_make_seqnumbers_non_null(sqlite * sql, 
						   char ** errmsg)
{
  // this just adds NOT NULL constraints to the INTEGER fields

  if (!move_table(sql, errmsg, 
		  "packets", 
		  "tmp", 
		  "("
		  "major      INTEGER,"
		  "minor      INTEGER,"
		  "groupname  TEXT NOT NULL,"
		  "adler32    TEXT NOT NULL,"
		  "contents   TEXT NOT NULL,"
		  "unique(groupname, contents),"
		  "unique(major, minor, groupname)"
		  ")"))
    return false;

  if (!move_table(sql, errmsg, 
		  "tmp", 
		  "packets", 
		  "("
		  "major      INTEGER NOT NULL,"
		  "minor      INTEGER NOT NULL,"
		  "groupname  TEXT NOT NULL,"
		  "adler32    TEXT NOT NULL,"
		  "contents   TEXT NOT NULL,"
		  "unique(groupname, contents),"
		  "unique(major, minor, groupname)"
		  ")"))
    return false;
  
  return true;
}


static bool 
migrate_client_merge_url_and_group(sqlite * sql, 
				   char ** errmsg)
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

  int res = sqlite_exec_printf(sql, "CREATE TABLE posting_queue "
			       "("
			       "url not null, -- URL we are going to send this to\n"
			       "content not null -- the packets we're going to send\n"
			       ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO posting_queue "
			   "SELECT "
			   "(url || '/' || groupname), "
			   "content "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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

  res = sqlite_exec_printf(sql, "CREATE TABLE incoming_queue "
			   "("
			   "url not null, -- URL we got this bundle from\n"
			   "content not null -- the packets we're going to read\n"
			   ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO incoming_queue "
			   "SELECT "
			   "(url || '/' || groupname), "
			   "content "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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
  
  res = sqlite_exec_printf(sql, "CREATE TABLE sequence_numbers "
			   "("
			   "url primary key, -- URL to read from\n"
			   "major not null, -- 0 in news servers, may be higher in depots\n"
			   "minor not null -- last article / packet sequence number we got\n"
			   ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO sequence_numbers "
			   "SELECT "
			   "(url || '/' || groupname), "
			   "major, "
			   "minor "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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
  
  res = sqlite_exec_printf(sql, "CREATE TABLE netserver_manifests "
			   "("
			   "url not null, -- url of some server\n"
			   "manifest not null, -- manifest which exists on url\n"
			   "unique(url, manifest)"
			   ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO netserver_manifests "
			   "SELECT "
			   "(url || '/' || groupname), "
			   "manifest "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;
  
  return true;  
}

static bool 
migrate_client_add_hashes_and_merkle_trees(sqlite * sql, 
					   char ** errmsg)
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

  int res = sqlite_exec_printf(sql, "CREATE TABLE manifest_certs\n"
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

  res = sqlite_exec_printf(sql, "INSERT INTO manifest_certs "
			   "SELECT "
			   "sha1(':', id, name, value, keypair, signature), "
			   "id, name, value, keypair, signature "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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

  res = sqlite_exec_printf(sql, "CREATE TABLE file_certs\n"
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

  res = sqlite_exec_printf(sql, "INSERT INTO file_certs "
			   "SELECT "
			   "sha1(':', id, name, value, keypair, signature), "
			   "id, name, value, keypair, signature "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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

  res = sqlite_exec_printf(sql, "CREATE TABLE public_keys\n"
			   "(\n"
			   "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
			   "id primary key,         -- key identifier chosen by user\n"
			   "keydata not null        -- RSA public params\n"
			   ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO public_keys "
			   "SELECT "
			   "sha1(':', id, keydata), "
			   "id, keydata "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
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

  res = sqlite_exec_printf(sql, "CREATE TABLE private_keys\n"
			   "(\n"
			   "hash not null unique,   -- hash of remaining fields separated by \":\"\n"
			   "id primary key,         -- as in public_keys (same identifiers, in fact)\n"
			   "keydata not null        -- encrypted RSA private params\n"
			   ")", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "INSERT INTO private_keys "
			   "SELECT "
			   "sha1(':', id, keydata), "
			   "id, keydata "
			   "FROM tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  res = sqlite_exec_printf(sql, "DROP TABLE tmp", NULL, NULL, errmsg);
  if (res != SQLITE_OK)
    return false;

  // add the merkle tree stuff

  res = sqlite_exec_printf(sql, 
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

void 
migrate_depot_schema(sqlite *sql)
{  
  migrator m;

  m.add("da3d5798a6ae61bd6566e74d8888faebc413dd2f",
	&migrate_depot_split_seqnumbers_into_groups);

  m.add("b820522b75efb31c8afb1b6f114841354d87e22d",
	&migrate_depot_make_seqnumbers_non_null);

  m.migrate(sql, "b0f3041a8ded95006584340ef76bd70ae81bb376");
  
  if (sqlite_exec(sql, "VACUUM", NULL, NULL, NULL) != SQLITE_OK)
    throw runtime_error("error vacuuming after migration");
}

void 
migrate_monotone_schema(sqlite *sql)
{

  migrator m;
  
  m.add("edb5fa6cef65bcb7d0c612023d267c3aeaa1e57a",
	&migrate_client_merge_url_and_group);

  m.add("f042f3c4d0a4f98f6658cbaf603d376acf88ff4b",
	&migrate_client_add_hashes_and_merkle_trees);

  m.migrate(sql, "8929e54f40bf4d3b4aea8b037d2c9263e82abdf4");
  
  if (sqlite_exec(sql, "VACUUM", NULL, NULL, NULL) != SQLITE_OK)
    throw runtime_error("error vacuuming after migration");

}
