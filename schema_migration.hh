#ifndef __SCHEMA_MIGRATION__
#define __SCHEMA_MIGRATION__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

struct sqlite3;
class key_store;
class system_path;

std::string describe_sql_schema(sqlite3 * db);
void check_sql_schema(sqlite3 * db, system_path const & filename);
void migrate_sql_schema(sqlite3 * db, key_store & keys,
                        system_path const & filename);

// utility routine shared with database.cc
void assert_sqlite3_ok(sqlite3 * db);

// debugging
void test_migration_step(sqlite3 * db, key_store & keys,
                         system_path const & filename,
                         std::string const & schema);

// this constant is part of the database schema, but it is not in schema.sql
// because sqlite expressions can't do arithmetic on character values.  it
// is stored in the "user version" field of the database header.  when we
// encounter a database whose schema hash we don't recognize, we look for
// this code in the header to decide whether it's a monotone database or
// some other sqlite3 database.  the expectation is that it will never need
// to change.  we call it a creator code because it has the same format and
// function as file creator codes in old-sk00l Mac OS.

const unsigned int mtn_creator_code = ((('_'*256 + 'M')*256 + 'T')*256 + 'N');

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __SCHEMA_MIGRATION__
