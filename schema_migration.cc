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

static string lowercase(string const & in)
{
  size_t const sz = in.size();
  char buf[sz];
  in.copy(buf, sz);
  use_facet< ctype<char> >(locale::locale()).tolower(buf, buf+sz);
  return string(buf,sz);
}

static void massage_sql_tokens(string const & in,
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

static void calculate_id(string const & in,
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


int append_sql_stmt(void * vp, 
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

void calculate_schema_id(sqlite *sql, string & id)
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

struct migrator
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
				"got" + curr + ", wanted " + target_id);
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


static bool migrate_depot_split_seqnumbers_into_groups(sqlite * sql, 
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

void migrate_depot_schema(sqlite *sql)
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

void migrate_monotone_schema(sqlite *sql)
{
}
