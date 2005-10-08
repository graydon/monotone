#ifndef __SCHEMA_MIGRATION__
#define __SCHEMA_MIGRATION__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>

// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

struct sqlite3;
struct app_state;

void calculate_schema_id(sqlite3 *sql, std::string & id);
void migrate_monotone_schema(sqlite3 *sql, app_state *app);

#endif // __SCHEMA_MIGRATION__
