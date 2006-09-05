// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <list>
#include <set>
#include <sstream>
#include <vector>

#include <string.h>

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include <sqlite3.h>

#include "app_state.hh"
#include "cert.hh"
#include "cleanup.hh"
#include "constants.hh"
#include "database.hh"
#include "hash_map.hh"
#include "keys.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "schema_migration.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "xdelta.hh"
#include "epoch.hh"

#undef _REENTRANT
#include "lru_cache.h"

// defined in schema.sql, converted to header:
#include "schema.h"

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// see file schema.sql for the text of the schema.

using std::deque;
using std::endl;
using std::istream;
using std::ifstream;
using std::make_pair;
using std::map;
using std::multimap;
using std::ostream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;
using boost::lexical_cast;

int const one_row = 1;
int const one_col = 1;
int const any_rows = -1;
int const any_cols = -1;

namespace
{
  struct query_param
  {
    enum arg_type { text, blob };
    arg_type type;
    string data;
  };

  query_param
  text(string const & txt)
  {
    query_param q = {
      query_param::text,
      txt,
    };
    return q;
  }

  query_param
  blob(string const & blb)
  {
    query_param q = {
      query_param::blob,
      blb,
    };
    return q;
  }

  // track all open databases for close_all_databases() handler
  set<sqlite3*> sql_contexts;
}

struct query
{
  explicit query(string const & cmd)
    : sql_cmd(cmd)
  {}

  query()
  {}

  query & operator %(query_param const & qp)
  {
    args.push_back(qp);
    return *this;
  }

  vector<query_param> args;
  string sql_cmd;
};

database::database(system_path const & fn) :
  filename(fn),
  // nb. update this if you change the schema. unfortunately we are not
  // using self-digesting schemas due to comment irregularities and
  // non-alphabetic ordering of tables in sql source files. we could create
  // a temporary db, write our intended schema into it, and read it back,
  // but this seems like it would be too rude. possibly revisit this issue.
  schema("9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df"),
  pending_writes_size(0),
  __sql(NULL),
  transaction_level(0)
{}

bool
database::is_dbfile(any_path const & file)
{
  system_path fn(file);// why is this needed?
  bool same = (filename.as_internal() == fn.as_internal());
  if (same)
    L(FL("'%s' is the database file") % file);
  return same;
}

void
database::check_schema()
{
  string db_schema_id;
  calculate_schema_id (__sql, db_schema_id);
  N (schema == db_schema_id,
     F("layout of database %s doesn't match this version of monotone\n"
       "wanted schema %s, got %s\n"
       "try '%s db migrate' to upgrade\n"
       "(this is irreversible; you may want to make a backup copy first)")
     % filename % schema % db_schema_id % ui.prog_name);
}

void
database::check_is_not_rosterified()
{
  results res;
  string rosters_query = "SELECT 1 FROM rosters LIMIT 1";
  fetch(res, one_col, any_rows, query(rosters_query));
  N(res.empty(),
    F("this database already contains rosters"));
}

void
database::check_format()
{
  results res_revisions;
  string manifests_query = "SELECT 1 FROM manifests LIMIT 1";
  string revisions_query = "SELECT 1 FROM revisions LIMIT 1";
  string rosters_query = "SELECT 1 FROM rosters LIMIT 1";

  fetch(res_revisions, one_col, any_rows, query(revisions_query));

  if (!res_revisions.empty())
    {
      // they have revisions, so they can't be _ancient_, but they still might
      // not have rosters
      results res_rosters;
      fetch(res_rosters, one_col, any_rows, query(rosters_query));
      N(!res_rosters.empty(),
        F("database %s contains revisions but no rosters\n"
          "if you are a project leader or doing local testing:\n"
          "  see the file UPGRADE for instructions on upgrading.\n"
          "if you are not a project leader:\n"
          "  wait for a leader to migrate project data, and then\n"
          "  pull into a fresh database.\n"
          "sorry about the inconvenience.")
        % filename);
    }
  else
    {
      // they have no revisions, so they shouldn't have any manifests either.
      // if they do, their db is probably ancient.  (though I guess you could
      // trigger this check by taking a pre-roster monotone, doing "db
      // init; commit; db kill_rev_locally", and then upgrading to a
      // rosterified monotone.)
      results res_manifests;
      fetch(res_manifests, one_col, any_rows, query(manifests_query));
      N(res_manifests.empty(),
        F("database %s contains manifests but no revisions\n"
          "this is a very old database; it needs to be upgraded\n"
          "please see README.changesets for details")
        % filename);
    }
}

static void
sqlite3_gunzip_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to gunzip()", -1);
      return;
    }
  data unpacked;
  const char *val = (const char*) sqlite3_value_blob(args[0]);
  int bytes = sqlite3_value_bytes(args[0]);
  decode_gzip(gzip<data>(string(val,val+bytes)), unpacked);
  sqlite3_result_blob(f, unpacked().c_str(), unpacked().size(), SQLITE_TRANSIENT);
}

void
database::set_app(app_state * app)
{
  __app = app;
}

static void
check_sqlite_format_version(system_path const & filename)
{
  // sqlite 3 files begin with this constant string
  // (version 2 files begin with a different one)
  string version_string("SQLite format 3");

  ifstream file(filename.as_external().c_str());
  N(file, F("unable to probe database version in file %s") % filename);

  for (string::const_iterator i = version_string.begin();
       i != version_string.end(); ++i)
    {
      char c;
      file.get(c);
      N(c == *i, F("database %s is not an sqlite version 3 file, "
                   "try dump and reload") % filename);
    }
}


static void
assert_sqlite3_ok(sqlite3 *s)
{
  int errcode = sqlite3_errcode(s);

  if (errcode == SQLITE_OK) return;

  const char * errmsg = sqlite3_errmsg(s);

  // sometimes sqlite is not very helpful
  // so we keep a table of errors people have gotten and more helpful versions
  if (errcode != SQLITE_OK)
    {
      // first log the code so we can find _out_ what the confusing code
      // was... note that code does not uniquely identify the errmsg, unlike
      // errno's.
      L(FL("sqlite error: %d: %s") % errcode % errmsg);
    }
  // note: if you update this, try to keep calculate_schema_id() in
  // schema_migration.cc consistent.
  string auxiliary_message = "";
  if (errcode == SQLITE_ERROR)
    {
      auxiliary_message += _("make sure database and containing directory are writeable\n"
                             "and you have not run out of disk space");
    }
  // if the last message is empty, the \n will be stripped off too
  E(errcode == SQLITE_OK,
    // kind of string surgery to avoid ~duplicate strings
    F("sqlite error: %s\n%s") % errmsg % auxiliary_message);
}

struct sqlite3 *
database::sql(bool init, bool migrating_format)
{
  if (! __sql)
    {
      check_filename();

      if (! init)
        {
          check_db_exists();
          check_sqlite_format_version(filename);
        }

      open();

      if (init)
        {
          sqlite3_exec(__sql, schema_constant, NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);
        }

      check_schema();
      install_functions(__app);

      if (!migrating_format)
        check_format();
    }
  else
    {
      I(!init);
      I(!migrating_format);
    }
  return __sql;
}

void
database::initialize()
{
  if (__sql)
    throw oops("cannot initialize database while it is open");

  require_path_is_nonexistent(filename,
                              F("could not initialize database: %s: already exists")
                              % filename);

  system_path journal(filename.as_internal() + "-journal");
  require_path_is_nonexistent(journal,
                              F("existing (possibly stale) journal file '%s' "
                                "has same stem as new database '%s'\n"
                                "cancelling database creation")
                              % journal % filename);

  sqlite3 *s = sql(true);
  I(s != NULL);
}


struct
dump_request
{
  dump_request() : sql(), out() {};
  struct sqlite3 *sql;
  ostream *out;
};

static void
dump_row(ostream &out, sqlite3_stmt *stmt, string const& table_name)
{
  out << FL("INSERT INTO %s VALUES(") % table_name;
  unsigned n = sqlite3_data_count(stmt);
  for (unsigned i = 0; i < n; ++i)
    {
      if (i != 0)
        out << ',';

      if (sqlite3_column_type(stmt, i) == SQLITE_BLOB)
        {
          out << "X'";
          const char *val = (const char*) sqlite3_column_blob(stmt, i);
          int bytes = sqlite3_column_bytes(stmt, i);
          out << encode_hexenc(string(val,val+bytes));
          out << "'";
        }
      else
        {
          const unsigned char *val = sqlite3_column_text(stmt, i);
          if (val == NULL)
            out << "NULL";
          else
            {
              out << "'";
              for (const unsigned char *cp = val; *cp; ++cp)
                {
                  if (*cp == '\'')
                    out << "''";
                  else
                    out << *cp;
                }
              out << "'";
            }
        }
    }
  out << ");\n";
}

static int
dump_table_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(vals[1] != NULL);
  I(vals[2] != NULL);
  I(n == 3);
  I(string(vals[1]) == "table");
  *(dump->out) << vals[2] << ";\n";
  string table_name(vals[0]);
  string query = "SELECT * FROM " + table_name;
  sqlite3_stmt *stmt = 0;
  sqlite3_prepare(dump->sql, query.c_str(), -1, &stmt, NULL);
  assert_sqlite3_ok(dump->sql);

  int stepresult = SQLITE_DONE;
  do
    {
      stepresult = sqlite3_step(stmt);
      I(stepresult == SQLITE_DONE || stepresult == SQLITE_ROW);
      if (stepresult == SQLITE_ROW)
        dump_row(*(dump->out), stmt, table_name);
    }
  while (stepresult == SQLITE_ROW);

  sqlite3_finalize(stmt);
  assert_sqlite3_ok(dump->sql);
  return 0;
}

static int
dump_index_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(vals[1] != NULL);
  I(vals[2] != NULL);
  I(n == 3);
  I(string(vals[1]) == "index");
  *(dump->out) << vals[2] << ";\n";
  return 0;
}

void
database::dump(ostream & out)
{
  // don't care about schema checking etc.
  check_filename();
  check_db_exists();
  open();
  {
    transaction_guard guard(*this);
    dump_request req;
    req.out = &out;
    req.sql = sql();
    out << "BEGIN EXCLUSIVE;\n";
    int res;
    res = sqlite3_exec(req.sql,
                          "SELECT name, type, sql FROM sqlite_master "
                          "WHERE type='table' AND sql NOT NULL "
                          "AND name not like 'sqlite_stat%' "
                          "ORDER BY name",
                          dump_table_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    res = sqlite3_exec(req.sql,
                          "SELECT name, type, sql FROM sqlite_master "
                          "WHERE type='index' AND sql NOT NULL "
                          "ORDER BY name",
                          dump_index_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    out << "COMMIT;\n";
    guard.commit();
  }
  close();
}

void
database::load(istream & in)
{
  string line;
  string sql_stmt;

  check_filename();

  require_path_is_nonexistent(filename,
                              F("cannot create %s; it already exists") % filename);

  open();

  // the page size can only be set before any other commands have been executed
  sqlite3_exec(__sql, "PRAGMA page_size=8192", NULL, NULL, NULL);

  while(in)
    {
      getline(in, line, ';');
      sql_stmt += line + ';';

      if (sqlite3_complete(sql_stmt.c_str()))
        {
          sqlite3_exec(__sql, sql_stmt.c_str(), NULL, NULL, NULL);
          sql_stmt.clear();
        }
    }

  assert_sqlite3_ok(__sql);
  execute(query("ANALYZE"));
}


void
database::debug(string const & sql, ostream & out)
{
  results res;
  fetch(res, any_cols, any_rows, query(sql));
  out << "'" << sql << "' -> " << res.size() << " rows\n" << endl;
  for (size_t i = 0; i < res.size(); ++i)
    {
      for (size_t j = 0; j < res[i].size(); ++j)
        {
          if (j != 0)
            out << " | ";
          out << res[i][j];
        }
      out << endl;
    }
}


namespace
{
  unsigned long
  add(unsigned long count, unsigned long & total)
  {
    total += count;
    return count;
  }
}

void
database::info(ostream & out)
{
  string id;
  calculate_schema_id(sql(), id);

  unsigned long total = 0UL;

  u64 num_nodes;
  {
    results res;
    fetch(res, one_col, any_rows, query("SELECT node FROM next_roster_node_number"));
    if (res.empty())
      num_nodes = 0;
    else
      {
        I(res.size() == 1);
        num_nodes = lexical_cast<u64>(res[0][0]) - 1;
      }
  }

#define SPACE_USAGE(TABLE, COLS) add(space_usage(TABLE, COLS), total)

  out << \
    F("schema version    : %s\n"
      "counts:\n"
      "  full rosters    : %u\n"
      "  roster deltas   : %u\n"
      "  full files      : %u\n"
      "  file deltas     : %u\n"
      "  revisions       : %u\n"
      "  ancestry edges  : %u\n"
      "  certs           : %u\n"
      "  logical files   : %u\n"
      "bytes:\n"
      "  full rosters    : %u\n"
      "  roster deltas   : %u\n"
      "  full files      : %u\n"
      "  file deltas     : %u\n"
      "  revisions       : %u\n"
      "  cached ancestry : %u\n"
      "  certs           : %u\n"
      "  total           : %u\n"
      "database:\n"
      "  page size       : %u\n"
      "  cache size      : %u\n"
      )
    % id
    // counts
    % count("rosters")
    % count("roster_deltas")
    % count("files")
    % count("file_deltas")
    % count("revisions")
    % count("revision_ancestry")
    % count("revision_certs")
    % num_nodes
    // bytes
    % SPACE_USAGE("rosters", "length(id) + length(data)")
    % SPACE_USAGE("roster_deltas", "length(id) + length(base) + length(delta)")
    % SPACE_USAGE("files", "length(id) + length(data)")
    % SPACE_USAGE("file_deltas", "length(id) + length(base) + length(delta)")
    % SPACE_USAGE("revisions", "length(id) + length(data)")
    % SPACE_USAGE("revision_ancestry", "length(parent) + length(child)")
    % SPACE_USAGE("revision_certs", "length(hash) + length(id) + length(name)"
                  " + length(value) + length(keypair) + length(signature)")
    % total
    % page_size()
    % cache_size();

#undef SPACE_USAGE
}

void
database::version(ostream & out)
{
  string id;

  check_filename();
  check_db_exists();
  open();

  calculate_schema_id(__sql, id);

  close();

  out << F("database schema version: %s") % id << endl;
}

void
database::migrate()
{
  check_filename();
  check_db_exists();
  open();

  migrate_monotone_schema(__sql, __app);

  close();
}

void
database::ensure_open()
{
  sqlite3 *s = sql();
  I(s != NULL);
}

void
database::ensure_open_for_format_changes()
{
  sqlite3 *s = sql(false, true);
  I(s != NULL);
}

database::~database()
{
  L(FL("statement cache statistics"));
  L(FL("prepared %d statements") % statement_cache.size());

  for (map<string, statement>::const_iterator i = statement_cache.begin();
       i != statement_cache.end(); ++i)
    L(FL("%d executions of %s") % i->second.count % i->first);
  // trigger destructors to finalize cached statements
  statement_cache.clear();

  close();
}

void
database::execute(query const & query)
{
  results res;
  fetch(res, 0, 0, query);
}

void
database::fetch(results & res,
                int const want_cols,
                int const want_rows,
                query const & query)
{
  int nrow;
  int ncol;
  int rescode;

  res.clear();
  res.resize(0);

  map<string, statement>::iterator i = statement_cache.find(query.sql_cmd);
  if (i == statement_cache.end())
    {
      statement_cache.insert(make_pair(query.sql_cmd, statement()));
      i = statement_cache.find(query.sql_cmd);
      I(i != statement_cache.end());

      const char * tail;
      sqlite3_prepare(sql(), query.sql_cmd.c_str(), -1, i->second.stmt.paddr(), &tail);
      assert_sqlite3_ok(sql());
      L(FL("prepared statement %s") % query.sql_cmd);

      // no support for multiple statements here
      E(*tail == 0,
        F("multiple statements in query: %s\n") % query.sql_cmd);
    }

  ncol = sqlite3_column_count(i->second.stmt());

  E(want_cols == any_cols || want_cols == ncol,
    F("wanted %d columns got %d in query: %s") % want_cols % ncol % query.sql_cmd);

  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(i->second.stmt());

  // Ensure that exactly the right number of parameters were given
  I(params == int(query.args.size()));

  // profiling finds this logging to be quite expensive
  if (global_sanity.debug)
    L(FL("binding %d parameters for %s") % params % query.sql_cmd);

  for (int param = 1; param <= params; param++)
    {
      // profiling finds this logging to be quite expensive
      if (global_sanity.debug)
        {
          string log = query.args[param-1].data;

          if (log.size() > constants::log_line_sz)
            log = log.substr(0, constants::log_line_sz);

          L(FL("binding %d with value '%s'") % param % log);
        }

      switch (idx(query.args, param - 1).type)
        {
        case query_param::text:
          sqlite3_bind_text(i->second.stmt(), param,
                            idx(query.args, param - 1).data.c_str(), -1,
                            SQLITE_STATIC);
          break;
        case query_param::blob:
          {
            string const & data = idx(query.args, param - 1).data;
            sqlite3_bind_blob(i->second.stmt(), param,
                              data.data(), data.size(),
                              SQLITE_STATIC);
          }
          break;
        default:
          I(false);
        }

      assert_sqlite3_ok(sql());
    }

  // execute and process results

  nrow = 0;
  for (rescode = sqlite3_step(i->second.stmt()); rescode == SQLITE_ROW;
       rescode = sqlite3_step(i->second.stmt()))
    {
      vector<string> row;
      for (int col = 0; col < ncol; col++)
        {
          const char * value = (const char*)sqlite3_column_blob(i->second.stmt(), col);
          int bytes = sqlite3_column_bytes(i->second.stmt(), col);
          E(value, F("null result in query: %s") % query.sql_cmd);
          row.push_back(string(value, value + bytes));
          //L(FL("row %d col %d value='%s'") % nrow % col % value);
        }
      res.push_back(row);
    }

  if (rescode != SQLITE_DONE)
    assert_sqlite3_ok(sql());

  sqlite3_reset(i->second.stmt());
  assert_sqlite3_ok(sql());

  nrow = res.size();

  i->second.count++;

  E(want_rows == any_rows || want_rows == nrow,
    F("wanted %d rows got %d in query: %s") % want_rows % nrow % query.sql_cmd);
}

// general application-level logic

void
database::set_filename(system_path const & file)
{
  I(!__sql);
  filename = file;
}

system_path
database::get_filename()
{
  return filename;
}

void
database::begin_transaction(bool exclusive)
{
  if (transaction_level == 0)
    {
      I(pending_writes.empty());
      if (exclusive)
        execute(query("BEGIN EXCLUSIVE"));
      else
        execute(query("BEGIN DEFERRED"));
      transaction_exclusive = exclusive;
    }
  else
    {
      // You can't start an exclusive transaction within a non-exclusive
      // transaction
      I(!exclusive || transaction_exclusive);
    }
  transaction_level++;
}


size_t
database::size_pending_write(std::string const & tab, hexenc<id> const & id, data const & dat)
{
  return tab.size() + id().size() + dat().size();
}

bool
database::have_pending_write(string const & tab, hexenc<id> const & id)
{
  return pending_writes.find(make_pair(tab, id)) != pending_writes.end();
}

void
database::load_pending_write(string const & tab, hexenc<id> const & id, data & dat)
{
  dat = safe_get(pending_writes, make_pair(tab, id));
}

// precondition: have_pending_write(tab, an_id) == true
void
database::cancel_pending_write(string const & tab, hexenc<id> const & an_id)
{
  data const & dat = safe_get(pending_writes, make_pair(tab, an_id));
  size_t cancel_size = size_pending_write(tab, an_id, dat);
  I(cancel_size < pending_writes_size);
  pending_writes_size -= cancel_size;
    
  safe_erase(pending_writes, make_pair(tab, an_id));
}

void
database::schedule_write(string const & tab,
                         hexenc<id> const & an_id,
                         data const & dat)
{
  if (!have_pending_write(tab, an_id))
    {
      safe_insert(pending_writes, make_pair(make_pair(tab, an_id), dat));
      pending_writes_size += size_pending_write(tab, an_id, dat);
    }
  if (pending_writes_size > constants::db_max_pending_writes_bytes)
    flush_pending_writes();
}

void
database::flush_pending_writes()
{
  for (map<pair<string, hexenc<id> >, data>::const_iterator i = pending_writes.begin();
       i != pending_writes.end(); ++i)
    put(i->first.second, i->second, i->first.first);
  pending_writes.clear();
  pending_writes_size = 0;
}

void
database::commit_transaction()
{
  if (transaction_level == 1)
    {
      flush_pending_writes();
      execute(query("COMMIT"));
    }
  transaction_level--;
}

void
database::rollback_transaction()
{
  if (transaction_level == 1)
    {
      pending_writes.clear();
      pending_writes_size = 0;
      execute(query("ROLLBACK"));
    }
  transaction_level--;
}


bool
database::exists(hexenc<id> const & ident,
                 string const & table)
{
  if (have_pending_write(table, ident))
    return true;

  results res;
  query q("SELECT id FROM " + table + " WHERE id = ?");
  fetch(res, one_col, any_rows, q % text(ident()));
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}


bool
database::delta_exists(hexenc<id> const & ident,
                       string const & table)
{
  results res;
  query q("SELECT id FROM " + table + " WHERE id = ?");
  fetch(res, one_col, any_rows, q % text(ident()));
  return res.size() > 0;
}

unsigned long
database::count(string const & table)
{
  results res;
  query q("SELECT COUNT(*) FROM " + table);
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned long>(res[0][0]);
}

unsigned long
database::space_usage(string const & table, string const & rowspace)
{
  results res;
  // COALESCE is required since SUM({empty set}) is NULL.
  // the sqlite docs for SUM suggest this as a workaround
  query q("SELECT COALESCE(SUM(" + rowspace + "), 0) FROM " + table);
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned long>(res[0][0]);
}

unsigned int
database::page_size()
{
  results res;
  query q("PRAGMA page_size");
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned int>(res[0][0]);
}

unsigned int
database::cache_size()
{
  // This returns the persistent (default) cache size.  It's possible to
  // override this setting transiently at runtime by setting PRAGMA
  // cache_size.
  results res;
  query q("PRAGMA default_cache_size");
  fetch(res, one_col, one_row, q);
  return lexical_cast<unsigned int>(res[0][0]);
}

void
database::get_ids(string const & table, set< hexenc<id> > & ids)
{
  results res;
  query q("SELECT id FROM " + table);
  fetch(res, one_col, any_rows, q);

  for (size_t i = 0; i < res.size(); ++i)
    {
      ids.insert(hexenc<id>(res[i][0]));
    }
}

void
database::get(hexenc<id> const & ident,
              data & dat,
              string const & table)
{
  if (have_pending_write(table, ident))
    {
      load_pending_write(table, ident, dat);
      return;
    }

  results res;
  query q("SELECT data FROM " + table + " WHERE id = ?");
  fetch(res, one_col, one_row, q % text(ident()));

  // consistency check
  gzip<data> rdata(res[0][0]);
  data rdata_unpacked;
  decode_gzip(rdata,rdata_unpacked);

  hexenc<id> tid;
  calculate_ident(rdata_unpacked, tid);
  I(tid == ident);

  dat = rdata_unpacked;
}

void
database::get_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    delta & del,
                    string const & table)
{
  I(ident() != "");
  I(base() != "");
  results res;
  query q("SELECT delta FROM " + table + " WHERE id = ? AND base = ?");
  fetch(res, one_col, one_row, q % text(ident()) % text(base()));

  gzip<delta> del_packed(res[0][0]);
  decode_gzip(del_packed, del);
}

void
database::put(hexenc<id> const & ident,
              data const & dat,
              string const & table)
{
  // consistency check
  I(ident() != "");
  hexenc<id> tid;
  calculate_ident(dat, tid);
  MM(ident);
  MM(tid);
  I(tid == ident);

  gzip<data> dat_packed;
  encode_gzip(dat, dat_packed);

  string insert = "INSERT INTO " + table + " VALUES(?, ?)";
  execute(query(insert)
          % text(ident())
          % blob(dat_packed()));
}
void
database::put_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    delta const & del,
                    string const & table)
{
  // nb: delta schema is (id, base, delta)
  I(ident() != "");
  I(base() != "");

  gzip<delta> del_packed;
  encode_gzip(del, del_packed);

  string insert = "INSERT INTO "+table+" VALUES(?, ?, ?)";
  execute(query(insert)
          % text(ident())
          % text(base())
          % blob(del_packed()));
}

// static ticker cache_hits("vcache hits", "h", 1);

struct datasz
{
  unsigned long operator()(data const & t) { return t().size(); }
};

static LRUCache<hexenc<id>, data, datasz>
vcache(constants::db_version_cache_sz);

typedef vector< hexenc<id> > version_path;

static void
extend_path_if_not_cycle(string table_name,
                         shared_ptr<version_path> p,
                         hexenc<id> const & ext,
                         set< hexenc<id> > & seen_nodes,
                         vector< shared_ptr<version_path> > & next_paths)
{
  for (version_path::const_iterator i = p->begin(); i != p->end(); ++i)
    {
      if ((*i)() == ext())
        throw oops("cycle in table '" + table_name + "', at node "
                   + (*i)() + " <- " + ext());
    }

  if (seen_nodes.find(ext) == seen_nodes.end())
    {
      p->push_back(ext);
      next_paths.push_back(p);
      seen_nodes.insert(ext);
    }
}

void
database::get_version(hexenc<id> const & ident,
                      data & dat,
                      string const & data_table,
                      string const & delta_table)
{
  I(ident() != "");
  if (vcache.fetch(ident, dat))
    {
      return;
    }
  else if (exists(ident, data_table))
    {
     // easy path
     get(ident, dat, data_table);
    }
  else
    {
      // tricky path

      // we start from the file we want to reconstruct and work *forwards*
      // through the database, until we get to a full data object. we then
      // trace back through the list of edges we followed to get to the data
      // object, applying reverse deltas.
      //
      // the effect of this algorithm is breadth-first search, backwards
      // through the storage graph, to discover a forwards shortest path, and
      // then following that shortest path with delta application.
      //
      // we used to do this with the boost graph library, but it invovled
      // loading too much of the storage graph into memory at any moment. this
      // imperative version only loads the descendents of the reconstruction
      // node, so it much cheaper in terms of memory.
      //
      // we also maintain a cycle-detecting set, just to be safe

      L(FL("reconstructing %s in %s") % ident % delta_table);
      I(delta_exists(ident, delta_table));

      // Our reconstruction algorithm involves keeping a set of parallel
      // linear paths, starting from ident, moving forward through the
      // storage DAG until we hit a storage root.
      //
      // On each iteration, we extend every active path by one step. If our
      // extension involves a fork, we duplicate the path. If any path
      // contains a cycle, we fault.
      //
      // If, by extending a path C, we enter a node which another path
      // D has already seen, we kill path C. This avoids the possibility of
      // exponential growth in the number of paths due to extensive forking
      // and merging.

      vector< shared_ptr<version_path> > live_paths;

      string delta_query = "SELECT base FROM " + delta_table + " WHERE id = ?";

      {
        shared_ptr<version_path> pth0 = shared_ptr<version_path>(new version_path());
        pth0->push_back(ident);
        live_paths.push_back(pth0);
      }

      shared_ptr<version_path> selected_path;
      set< hexenc<id> > seen_nodes;

      while (!selected_path)
        {
          vector< shared_ptr<version_path> > next_paths;

          for (vector<shared_ptr<version_path> >::const_iterator i = live_paths.begin();
               i != live_paths.end(); ++i)
            {
              shared_ptr<version_path> pth = *i;
              hexenc<id> tip = pth->back();

              if (vcache.exists(tip) || exists(tip, data_table))
                {
                  selected_path = pth;
                  break;
                }
              else
                {
                  // This tip is not a root, so extend the path.
                  results res;
                  fetch(res, one_col, any_rows,
                        query(delta_query)
                        % text(tip()));

                  I(res.size() != 0);

                  // Replicate the path if there's a fork.
                  for (size_t k = 1; k < res.size(); ++k)
                    {
                      shared_ptr<version_path> pthN
                        = shared_ptr<version_path>(new version_path(*pth));
                      extend_path_if_not_cycle(delta_table, pthN,
                                               hexenc<id>(res[k][0]),
                                               seen_nodes, next_paths);
                    }

                  // And extend the base path we're examining.
                  extend_path_if_not_cycle(delta_table, pth,
                                           hexenc<id>(res[0][0]),
                                           seen_nodes, next_paths);
                }
            }

          I(selected_path || !next_paths.empty());
          live_paths = next_paths;
        }

      // Found a root, now trace it back along the path.

      I(selected_path);
      I(selected_path->size() > 1);

      hexenc<id> curr = selected_path->back();
      selected_path->pop_back();
      data begin;

      if (vcache.exists(curr))
        {
          I(vcache.fetch(curr, begin));
        }
      else
        {
          get(curr, begin, data_table);
        }

      shared_ptr<delta_applicator> app = new_piecewise_applicator();
      app->begin(begin());

      for (version_path::reverse_iterator i = selected_path->rbegin();
           i != selected_path->rend(); ++i)
        {
          hexenc<id> const nxt = *i;

          if (!vcache.exists(curr))
            {
              string tmp;
              app->finish(tmp);
              vcache.insert(curr, tmp);
            }

          L(FL("following delta %s -> %s") % curr % nxt);
          delta del;
          get_delta(nxt, curr, del, delta_table);
          apply_delta (app, del());

          app->next();
          curr = nxt;
        }

      string tmp;
      app->finish(tmp);
      dat = data(tmp);

      hexenc<id> final;
      calculate_ident(dat, final);
      I(final == ident);
    }
  vcache.insert(ident, dat);
}


void
database::drop(hexenc<id> const & ident,
               string const & table)
{
  string drop = "DELETE FROM " + table + " WHERE id = ?";
  execute(query(drop) % text(ident()));
}

void
database::put_version(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      delta const & del,
                      string const & data_table,
                      string const & delta_table)
{
  MM(del);
  data old_data, new_data;
  delta reverse_delta;
  MM(old_data);
  MM(new_data);
  MM(reverse_delta);

  get_version(old_id, old_data, data_table, delta_table);
  patch(old_data, del, new_data);
  {
    string tmp;
    invert_xdelta(old_data(), del(), tmp);
    reverse_delta = delta(tmp);
    data old_tmp;
    hexenc<id> old_tmp_id;
    patch(new_data, reverse_delta, old_tmp);
    calculate_ident(old_tmp, old_tmp_id);
    I(old_tmp_id == old_id);
  }

  transaction_guard guard(*this);
  if (exists(old_id, data_table))
    {
      // descendent of a head version replaces the head, therefore old head
      // must be disposed of
      if (have_pending_write(data_table, old_id))
        cancel_pending_write(data_table, old_id);
      else
        drop(old_id, data_table);
    }
  schedule_write(data_table, new_id, new_data);
  put_delta(old_id, new_id, reverse_delta, delta_table);
  guard.commit();
}

void
database::remove_version(hexenc<id> const & target_id,
                         string const & data_table,
                         string const & delta_table)
{
  // We have a one of two cases (for multiple 'older' nodes):
  //
  //    1.  pre:        older <- target <- newer
  //       post:                  older <- newer
  //
  //    2.  pre:        older <- target (a root)
  //       post:                  older (a root)
  //
  // In case 1 we want to build new deltas bypassing the target we're
  // removing. In case 2 we just promote the older object to a root.

  transaction_guard guard(*this);

  I(exists(target_id, data_table)
    || delta_exists(target_id, delta_table));

  map<hexenc<id>, data> older;

  {
    results res;
    query q("SELECT id FROM " + delta_table + " WHERE base = ?");
    fetch(res, one_col, any_rows, q % text(target_id()));
    for (size_t i = 0; i < res.size(); ++i)
      {
        hexenc<id> old_id(res[i][0]);
        data old_data;
        get_version(old_id, old_data, data_table, delta_table);
        older.insert(make_pair(old_id, old_data));
      }
  }

  // no deltas are allowed to point to the target.
  execute(query("DELETE from " + delta_table + " WHERE base = ?")
          % text(target_id()));

  if (delta_exists(target_id, delta_table))
    {
      if (!older.empty())
        {
          // Case 1: need to re-deltify all the older values against a newer
          // member of the delta chain. Doesn't really matter which newer
          // element (we have no good heuristic for guessing a good one
          // anyways).
          hexenc<id> newer_id;
          data newer_data;
          results res;
          query q("SELECT base FROM " + delta_table + " WHERE id = ?");
          fetch(res, one_col, any_rows, q % text(target_id()));
          I(res.size() > 0);
          newer_id = hexenc<id>(res[0][0]);
          get_version(newer_id, newer_data, data_table, delta_table);
          for (map<hexenc<id>, data>::const_iterator i = older.begin();
               i != older.end(); ++i)
            {
              if (delta_exists(i->first, delta_table))
                continue;
              delta bypass_delta;
              diff(newer_data, i->second, bypass_delta);
              put_delta(i->first, newer_id, bypass_delta, delta_table);
            }
        }
      execute(query("DELETE from " + delta_table + " WHERE id = ?")
              % text(target_id()));
    }
  else
    {
      // Case 2: just plop the older values down as new storage roots.
      I(exists(target_id, data_table));
      for (map<hexenc<id>, data>::const_iterator i = older.begin();
           i != older.end(); ++i)
        {
          if (!exists(i->first, data_table))
            put(i->first, i->second, data_table);
        }
      execute(query("DELETE from " + data_table + " WHERE id = ?")
              % text(target_id()));
    }

  guard.commit();
}


// ------------------------------------------------------------
// --                                                        --
// --              public interface follows                  --
// --                                                        --
// ------------------------------------------------------------

bool
database::file_version_exists(file_id const & id)
{
  return delta_exists(id.inner(), "file_deltas")
    || exists(id.inner(), "files");
}

bool
database::roster_version_exists(roster_id const & id)
{
  return delta_exists(id.inner(), "roster_deltas")
    || exists(id.inner(), "rosters");
}

bool
database::revision_exists(revision_id const & id)
{
  return exists(id.inner(), "revisions");
}

bool
database::roster_link_exists_for_revision(revision_id const & rev_id)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT roster_id FROM revision_roster WHERE rev_id = ? ")
        % text(rev_id.inner()()));
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}

bool
database::roster_exists_for_revision(revision_id const & rev_id)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT roster_id FROM revision_roster WHERE rev_id = ? ")
        % text(rev_id.inner()()));
  I((res.size() == 1) || (res.size() == 0));
  return (res.size() == 1) && roster_version_exists(roster_id(res[0][0]));
}

void
database::get_roster_links(map<revision_id, roster_id> & links)
{
  links.clear();
  results res;
  fetch(res, 2, any_rows, query("SELECT rev_id, roster_id FROM revision_roster"));
  for (size_t i = 0; i < res.size(); ++i)
    {
      links.insert(make_pair(revision_id(res[i][0]),
                             roster_id(res[i][1])));
    }
}

void
database::get_file_ids(set<file_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("files", tmp);
  get_ids("file_deltas", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void
database::get_revision_ids(set<revision_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("revisions", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void
database::get_roster_ids(set<roster_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("rosters", tmp);
  get_ids("roster_deltas", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void
database::get_file_version(file_id const & id,
                           file_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "files", "file_deltas");
  dat = tmp;
}

void
database::get_manifest_version(manifest_id const & id,
                               manifest_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "manifests", "manifest_deltas");
  dat = tmp;
}

void
database::get_roster_version(roster_id const & id,
                             roster_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "rosters", "roster_deltas");
  dat = tmp;
}

void
database::put_file(file_id const & id,
                   file_data const & dat)
{
  schedule_write("files", id.inner(), dat.inner());
}

void
database::put_file_version(file_id const & old_id,
                           file_id const & new_id,
                           file_delta const & del)
{
  put_version(old_id.inner(), new_id.inner(), del.inner(),
              "files", "file_deltas");
}

void
database::get_arbitrary_file_delta(file_id const & src_id,
                                   file_id const & dst_id,
                                   file_delta & del)
{
  delta dtmp;
  // Deltas stored in the database go from base -> id.
  results res;
  query q1("SELECT delta FROM file_deltas "
           "WHERE base = ? AND id = ?");
  fetch(res, one_col, any_rows,
        q1 % text(src_id.inner()()) % text(dst_id.inner()()));

  if (!res.empty())
    {
      // Exact hit: a plain delta from src -> dst.
      gzip<delta> del_packed(res[0][0]);
      decode_gzip(del_packed, dtmp);
      del = file_delta(dtmp);
      return;
    }

  query q2("SELECT delta FROM file_deltas "
           "WHERE id = ? AND base = ?");
  fetch(res, one_col, any_rows,
        q2 % text(dst_id.inner()()) % text(src_id.inner()()));

  if (!res.empty())
    {
      // We have a delta from dst -> src; we need to
      // invert this to a delta from src -> dst.
      gzip<delta> del_packed(res[0][0]);
      decode_gzip(del_packed, dtmp);
      string fwd_delta;
      file_data dst;
      get_file_version(dst_id, dst);
      invert_xdelta(dst.inner()(), dtmp(), fwd_delta);
      del = file_delta(fwd_delta);
      return;
    }

  // No deltas of use; just load both versions and diff.
  file_data fd1, fd2;
  get_file_version(src_id, fd1);
  get_file_version(dst_id, fd2);
  diff(fd1.inner(), fd2.inner(), dtmp);
  del = file_delta(dtmp);
}


void
database::get_revision_ancestry(multimap<revision_id, revision_id> & graph)
{
  results res;
  graph.clear();
  fetch(res, 2, any_rows,
        query("SELECT parent,child FROM revision_ancestry"));
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(make_pair(revision_id(res[i][0]),
                                revision_id(res[i][1])));
}

void
database::get_revision_parents(revision_id const & id,
                               set<revision_id> & parents)
{
  I(!null_id(id));
  results res;
  parents.clear();
  fetch(res, one_col, any_rows,
        query("SELECT parent FROM revision_ancestry WHERE child = ?")
        % text(id.inner()()));
  for (size_t i = 0; i < res.size(); ++i)
    parents.insert(revision_id(res[i][0]));
}

void
database::get_revision_children(revision_id const & id,
                                set<revision_id> & children)
{
  results res;
  children.clear();
  fetch(res, one_col, any_rows,
        query("SELECT child FROM revision_ancestry WHERE parent = ?")
        % text(id.inner()()));
  for (size_t i = 0; i < res.size(); ++i)
    children.insert(revision_id(res[i][0]));
}

void
database::get_revision_manifest(revision_id const & rid,
                               manifest_id & mid)
{
  revision_t rev;
  get_revision(rid, rev);
  mid = rev.new_manifest;
}

void
database::get_revision(revision_id const & id,
                       revision_t & rev)
{
  revision_data d;
  get_revision(id, d);
  read_revision(d, rev);
}

void
database::get_revision(revision_id const & id,
                       revision_data & dat)
{
  I(!null_id(id));
  results res;
  fetch(res, one_col, one_row,
        query("SELECT data FROM revisions WHERE id = ?")
        % text(id.inner()()));

  gzip<data> gzdata(res[0][0]);
  data rdat;
  decode_gzip(gzdata,rdat);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(rdat, tmp);
    I(id == tmp);
  }

  dat = rdat;
}

void
database::deltify_revision(revision_id const & rid)
{
  transaction_guard guard(*this);
  revision_t rev;
  MM(rev);
  MM(rid);
  get_revision(rid, rev);
  // Make sure that all parent revs have their files replaced with deltas
  // from this rev's files.
  {
    for (edge_map::const_iterator i = rev.edges.begin();
         i != rev.edges.end(); ++i)
      {
        for (map<split_path, pair<file_id, file_id> >::const_iterator
               j = edge_changes(i).deltas_applied.begin();
             j != edge_changes(i).deltas_applied.end(); ++j)
          {
            if (exists(delta_entry_src(j).inner(), "files") &&
                file_version_exists(delta_entry_dst(j)))
              {
                file_data old_data;
                file_data new_data;
                get_file_version(delta_entry_src(j), old_data);
                get_file_version(delta_entry_dst(j), new_data);
                delta delt;
                diff(old_data.inner(), new_data.inner(), delt);
                file_delta del(delt);
                drop(delta_entry_dst(j).inner(), "files");
                drop(delta_entry_dst(j).inner(), "file_deltas");
                put_file_version(delta_entry_src(j), delta_entry_dst(j), del);
              }
          }
      }
  }
  guard.commit();
}


void
database::put_revision(revision_id const & new_id,
                       revision_t const & rev)
{
  MM(new_id);
  MM(rev);

  I(!null_id(new_id));
  I(!revision_exists(new_id));

  I(rev.made_for == made_for_database);
  rev.check_sane();
  revision_data d;
  MM(d.inner());
  write_revision(rev, d);

  // Phase 1: confirm the revision makes sense
  {
    revision_id tmp;
    MM(tmp);
    calculate_ident(d, tmp);
    I(tmp == new_id);
  }

  transaction_guard guard(*this);

  // Phase 2: construct a new roster and sanity-check its manifest_id
  // against the manifest_id of the revision you're writing.
  roster_t ros;
  marking_map mm;
  {
    manifest_id roster_manifest_id;
    MM(roster_manifest_id);
    make_roster_for_revision(rev, new_id, ros, mm, *__app);
    calculate_ident(ros, roster_manifest_id);
    I(rev.new_manifest == roster_manifest_id);
  }

  // Phase 3: Write the revision data

  gzip<data> d_packed;
  encode_gzip(d.inner(), d_packed);
  execute(query("INSERT INTO revisions VALUES(?, ?)")
          % text(new_id.inner()())
          % blob(d_packed()));

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      execute(query("INSERT INTO revision_ancestry VALUES(?, ?)")
              % text(edge_old_revision(e).inner()())
              % text(new_id.inner()()));
    }

  deltify_revision(new_id);

  // Phase 4: write the roster data and commit
  put_roster(new_id, ros, mm);

  guard.commit();
}


void
database::put_revision(revision_id const & new_id,
                       revision_data const & dat)
{
  revision_t rev;
  read_revision(dat, rev);
  put_revision(new_id, rev);
}


void
database::delete_existing_revs_and_certs()
{
  execute(query("DELETE FROM revisions"));
  execute(query("DELETE FROM revision_ancestry"));
  execute(query("DELETE FROM revision_certs"));
}

void
database::delete_existing_manifests()
{
  execute(query("DELETE FROM manifests"));
  execute(query("DELETE FROM manifest_deltas"));
}

/// Deletes one revision from the local database.
/// @see kill_rev_locally
void
database::delete_existing_rev_and_certs(revision_id const & rid)
{
  transaction_guard guard (*this);

  // Check that the revision exists and doesn't have any children.
  I(revision_exists(rid));
  set<revision_id> children;
  get_revision_children(rid, children);
  I(!children.size());


  L(FL("Killing revision %s locally") % rid);

  // Kill the certs, ancestry, and rev itself.
  execute(query("DELETE from revision_certs WHERE id = ?")
          % text(rid.inner()()));

  execute(query("DELETE from revision_ancestry WHERE child = ?")
          % text(rid.inner()()));

  execute(query("DELETE from revisions WHERE id = ?")
          % text(rid.inner()()));

  // Find the associated roster and count the number of links to it
  roster_id ros_id;
  size_t link_count = 0;
  get_roster_id_for_revision(rid, ros_id);
  {
    results res;
    fetch(res, 2, any_rows,
          query("SELECT rev_id, roster_id FROM revision_roster "
                "WHERE roster_id = ?") % text(ros_id.inner()()));
    I(res.size() > 0);
    link_count = res.size();
  }

  // Delete our link.
  execute(query("DELETE from revision_roster WHERE rev_id = ?")
          % text(rid.inner()()));

  // If that was the last link to the roster, kill the roster too.
  if (link_count == 1)
    remove_version(ros_id.inner(), "rosters", "roster_deltas");

  guard.commit();
}

/// Deletes all certs referring to a particular branch.
void
database::delete_branch_named(cert_value const & branch)
{
  L(FL("Deleting all references to branch %s") % branch);
  execute(query("DELETE FROM revision_certs WHERE name='branch' AND value =?")
          % blob(branch()));
  execute(query("DELETE FROM branch_epochs WHERE branch=?")
          % blob(branch()));
}

/// Deletes all certs referring to a particular tag.
void
database::delete_tag_named(cert_value const & tag)
{
  L(FL("Deleting all references to tag %s") % tag);
  execute(query("DELETE FROM revision_certs WHERE name='tag' AND value =?")
          % blob(tag()));
}

// crypto key management

void
database::get_key_ids(string const & pattern,
                      vector<rsa_keypair_id> & pubkeys)
{
  pubkeys.clear();
  results res;

  if (pattern != "")
    fetch(res, one_col, any_rows,
          query("SELECT id FROM public_keys WHERE id GLOB ?")
          % text(pattern));
  else
    fetch(res, one_col, any_rows,
          query("SELECT id FROM public_keys"));

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(res[i][0]);
}

void
database::get_keys(string const & table, vector<rsa_keypair_id> & keys)
{
  keys.clear();
  results res;
  fetch(res, one_col, any_rows, query("SELECT id FROM " + table));
  for (size_t i = 0; i < res.size(); ++i)
    keys.push_back(res[i][0]);
}

void
database::get_public_keys(vector<rsa_keypair_id> & keys)
{
  get_keys("public_keys", keys);
}

bool
database::public_key_exists(hexenc<id> const & hash)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT id FROM public_keys WHERE hash = ?")
        % text(hash()));
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1)
    return true;
  return false;
}

bool
database::public_key_exists(rsa_keypair_id const & id)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT id FROM public_keys WHERE id = ?")
        % text(id()));
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1)
    return true;
  return false;
}

void
database::get_pubkey(hexenc<id> const & hash,
                     rsa_keypair_id & id,
                     base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, 2, one_row,
        query("SELECT id, keydata FROM public_keys WHERE hash = ?")
        % text(hash()));
  id = res[0][0];
  encode_base64(rsa_pub_key(res[0][1]), pub_encoded);
}

void
database::get_key(rsa_keypair_id const & pub_id,
                  base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, one_col, one_row,
        query("SELECT keydata FROM public_keys WHERE id = ?")
        % text(pub_id()));
  encode_base64(rsa_pub_key(res[0][0]), pub_encoded);
}

void
database::put_key(rsa_keypair_id const & pub_id,
                  base64<rsa_pub_key> const & pub_encoded)
{
  hexenc<id> thash;
  key_hash_code(pub_id, pub_encoded, thash);
  I(!public_key_exists(thash));
  E(!public_key_exists(pub_id),
    F("another key with name '%s' already exists") % pub_id);
  rsa_pub_key pub_key;
  decode_base64(pub_encoded, pub_key);
  execute(query("INSERT INTO public_keys VALUES(?, ?, ?)")
          % text(thash())
          % text(pub_id())
          % blob(pub_key()));
}

void
database::delete_public_key(rsa_keypair_id const & pub_id)
{
  execute(query("DELETE FROM public_keys WHERE id = ?")
          % text(pub_id()));
}

// cert management

bool
database::cert_exists(cert const & t,
                      string const & table)
{
  results res;
  cert_value value;
  decode_base64(t.value, value);
  rsa_sha1_signature sig;
  decode_base64(t.sig, sig);
  query q = query("SELECT id FROM " + table + " WHERE id = ? "
                  "AND name = ? "
                  "AND value = ? "
                  "AND keypair = ? "
                  "AND signature = ?")
    % text(t.ident())
    % text(t.name())
    % blob(value())
    % text(t.key())
    % blob(sig());

  fetch(res, 1, any_rows, q);

  I(res.size() == 0 || res.size() == 1);
  return res.size() == 1;
}

void
database::put_cert(cert const & t,
                   string const & table)
{
  hexenc<id> thash;
  cert_hash_code(t, thash);
  cert_value value;
  decode_base64(t.value, value);
  rsa_sha1_signature sig;
  decode_base64(t.sig, sig);

  string insert = "INSERT INTO " + table + " VALUES(?, ?, ?, ?, ?, ?)";

  execute(query(insert)
          % text(thash())
          % text(t.ident())
          % text(t.name())
          % blob(value())
          % text(t.key())
          % blob(sig()));
}

void
database::results_to_certs(results const & res,
                           vector<cert> & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      cert t;
      base64<cert_value> value;
      encode_base64(cert_value(res[i][2]), value);
      base64<rsa_sha1_signature> sig;
      encode_base64(rsa_sha1_signature(res[i][4]), sig);
      t = cert(hexenc<id>(res[i][0]),
              cert_name(res[i][1]),
              value,
              rsa_keypair_id(res[i][3]),
              sig);
      certs.push_back(t);
    }
}

void
database::install_functions(app_state * app)
{
  // register any functions we're going to use
  I(sqlite3_create_function(sql(), "gunzip", -1,
                           SQLITE_UTF8, NULL,
                           &sqlite3_gunzip_fn,
                           NULL, NULL) == 0);
}

void
database::get_certs(vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table);
  fetch(res, 5, any_rows, q);
  results_to_certs(res, certs);
}


void
database::get_certs(hexenc<id> const & ident,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE id = ?");

  fetch(res, 5, any_rows, q % text(ident()));
  results_to_certs(res, certs);
}


void
database::get_certs(cert_name const & name,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE name = ?");
  fetch(res, 5, any_rows, q % text(name()));
  results_to_certs(res, certs);
}


void
database::get_certs(hexenc<id> const & ident,
                    cert_name const & name,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE id = ? AND name = ?");

  fetch(res, 5, any_rows,
        q % text(ident())
          % text(name()));
  results_to_certs(res, certs);
}

void
database::get_certs(cert_name const & name,
                    base64<cert_value> const & val,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE name = ? AND value = ?");

  cert_value binvalue;
  decode_base64(val, binvalue);
  fetch(res, 5, any_rows,
        q % text(name())
          % blob(binvalue()));
  results_to_certs(res, certs);
}


void
database::get_certs(hexenc<id> const & ident,
                    cert_name const & name,
                    base64<cert_value> const & value,
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  query q("SELECT id, name, value, keypair, signature FROM " + table +
          " WHERE id = ? AND name = ? AND value = ?");

  cert_value binvalue;
  decode_base64(value, binvalue);
  fetch(res, 5, any_rows,
        q % text(ident())
          % text(name())
          % blob(binvalue()));
  results_to_certs(res, certs);
}



bool
database::revision_cert_exists(revision<cert> const & cert)
{
  return cert_exists(cert.inner(), "revision_certs");
}

void
database::put_revision_cert(revision<cert> const & cert)
{
  put_cert(cert.inner(), "revision_certs");
}

void database::get_revision_cert_nobranch_index(vector< pair<hexenc<id>,
                                                pair<revision_id, rsa_keypair_id> > > & idx)
{
  results res;
  fetch(res, 3, any_rows,
        query("SELECT hash, id, keypair "
        "FROM 'revision_certs' WHERE name != 'branch'"));

  idx.clear();
  idx.reserve(res.size());
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      idx.push_back(make_pair(hexenc<id>((*i)[0]),
                              make_pair(revision_id((*i)[1]),
                                        rsa_keypair_id((*i)[2]))));
    }
}

void
database::get_revision_certs(vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revision_certs(cert_name const & name,
                            vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             base64<cert_value> const & val,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, val, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revisions_with_cert(cert_name const & name,
                                  base64<cert_value> const & val,
                                  set<revision_id> & revisions)
{
  revisions.clear();
  results res;
  query q("SELECT id FROM revision_certs WHERE name = ? AND value = ?");
  cert_value binvalue;
  decode_base64(val, binvalue);
  fetch(res, one_col, any_rows, q % text(name()) % blob(binvalue()));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    revisions.insert(revision_id((*i)[0]));
}

void
database::get_revision_certs(cert_name const & name,
                             base64<cert_value> const & val,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, val, certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revision_certs(revision_id const & id,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), certs, "revision_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}

void
database::get_revision_certs(revision_id const & ident,
                             vector< hexenc<id> > & ts)
{
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows,
        query("SELECT hash "
        "FROM revision_certs "
        "WHERE id = ?")
        % text(ident.inner()()));
  ts.clear();
  for (size_t i = 0; i < res.size(); ++i)
    ts.push_back(hexenc<id>(res[i][0]));
}

void
database::get_revision_cert(hexenc<id> const & hash,
                            revision<cert> & c)
{
  results res;
  vector<cert> certs;
  fetch(res, 5, one_row,
        query("SELECT id, name, value, keypair, signature "
        "FROM revision_certs "
        "WHERE hash = ?")
        % text(hash()));
  results_to_certs(res, certs);
  I(certs.size() == 1);
  c = revision<cert>(certs[0]);
}

bool
database::revision_cert_exists(hexenc<id> const & hash)
{
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows,
        query("SELECT id "
        "FROM revision_certs "
        "WHERE hash = ?")
        % text(hash()));
  I(res.size() == 0 || res.size() == 1);
  return (res.size() == 1);
}

void
database::get_manifest_certs(manifest_id const & id,
                             vector< manifest<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), certs, "manifest_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}


void
database::get_manifest_certs(cert_name const & name,
                            vector< manifest<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "manifest_certs");
  ts.clear();
  copy(certs.begin(), certs.end(), back_inserter(ts));
}


// completions
void
database::complete(string const & partial,
                   set<revision_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        query("SELECT id FROM revisions WHERE id GLOB ?")
        % text(pattern));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0]));
}


void
database::complete(string const & partial,
                   set<file_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        query("SELECT id FROM files WHERE id GLOB ?")
        % text(pattern));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));

  res.clear();

  fetch(res, 1, any_rows,
        query("SELECT id FROM file_deltas WHERE id GLOB ?")
        % text(pattern));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));
}

void
database::complete(string const & partial,
                   set< pair<key_id, utf8 > > & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 2, any_rows,
        query("SELECT hash, id FROM public_keys WHERE hash GLOB ?")
        % text(pattern));

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(make_pair(key_id(res[i][0]), utf8(res[i][1])));
}

using selectors::selector_type;

static void selector_to_certname(selector_type ty,
                                 string & s,
                                 string & prefix,
                                 string & suffix)
{
  prefix = suffix = "*";
  switch (ty)
    {
    case selectors::sel_author:
      s = author_cert_name;
      break;
    case selectors::sel_branch:
      prefix = suffix = "";
      s = branch_cert_name;
      break;
    case selectors::sel_head:
      prefix = suffix = "";
      s = branch_cert_name;
      break;
    case selectors::sel_date:
    case selectors::sel_later:
    case selectors::sel_earlier:
      s = date_cert_name;
      break;
    case selectors::sel_tag:
      prefix = suffix = "";
      s = tag_cert_name;
      break;
    case selectors::sel_ident:
    case selectors::sel_cert:
    case selectors::sel_unknown:
      I(false); // don't do this.
      break;
    }
}

void database::complete(selector_type ty,
                        string const & partial,
                        vector<pair<selector_type, string> > const & limit,
                        set<string> & completions)
{
  //L(FL("database::complete for partial '%s'") % partial);
  completions.clear();

  // step 1: the limit is transformed into an SQL select statement which
  // selects a set of IDs from the manifest_certs table which match the
  // limit.  this is done by building an SQL select statement for each term
  // in the limit and then INTERSECTing them all.

  query lim;
  lim.sql_cmd = "(";
  if (limit.empty())
    {
      lim.sql_cmd += "SELECT id FROM revision_certs";
    }
  else
    {
      bool first_limit = true;
      for (vector<pair<selector_type, string> >::const_iterator i = limit.begin();
           i != limit.end(); ++i)
        {
          if (first_limit)
            first_limit = false;
          else
            lim.sql_cmd += " INTERSECT ";

          if (i->first == selectors::sel_ident)
            {
              lim.sql_cmd += "SELECT id FROM revision_certs WHERE id GLOB ?";
              lim % text(i->second + "*");
            }
          else if (i->first == selectors::sel_cert)
            {
              if (i->second.length() > 0)
                {
                  size_t spot = i->second.find("=");

                  if (spot != (size_t)-1)
                    {
                      string certname;
                      string certvalue;

                      certname = i->second.substr(0, spot);
                      spot++;
                      certvalue = i->second.substr(spot);
                      lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=? AND CAST(value AS TEXT) glob ?";
                      lim % text(certname) % text(certvalue);
                    }
                  else
                    {
                      lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=?";
                      lim % text(i->second);
                    }

                }
            }
          else if (i->first == selectors::sel_unknown)
            {
              lim.sql_cmd += "SELECT id FROM revision_certs WHERE (name=? OR name=? OR name=?)";
              lim % text(author_cert_name) % text(tag_cert_name) % text(branch_cert_name);
              lim.sql_cmd += " AND CAST(value AS TEXT) glob ?";
              lim % text(i->second + "*");
            }
          else if (i->first == selectors::sel_head)
            {
              // get branch names
              vector<cert_value> branch_names;
              if (i->second.size() == 0)
                {
                  __app->require_workspace("the empty head selector h: refers to the head of the current branch");
                  branch_names.push_back((__app->branch_name)());
                }
              else
                {
                  query subquery("SELECT DISTINCT value FROM revision_certs WHERE name=? AND CAST(value AS TEXT) glob ?");
                  subquery % text(branch_cert_name) % text(i->second);
                  results res;
                  fetch(res, one_col, any_rows, subquery);
                  for (size_t i = 0; i < res.size(); ++i)
                    {
                      data row_decoded(res[i][0]);
                      branch_names.push_back(row_decoded());
                    }
                }

              // for each branch name, get the branch heads
              set<revision_id> heads;
              for (vector<cert_value>::const_iterator bn = branch_names.begin(); bn != branch_names.end(); bn++)
                {
                  set<revision_id> branch_heads;
                  get_branch_heads(*bn, *__app, branch_heads);
                  heads.insert(branch_heads.begin(), branch_heads.end());
                  L(FL("after get_branch_heads for %s, heads has %d entries") % (*bn) % heads.size());
                }

              lim.sql_cmd += "SELECT id FROM revision_certs WHERE id IN (";
              if (heads.size())
                {
                  set<revision_id>::const_iterator r = heads.begin();
                  lim.sql_cmd += "?";
                  lim % text(r->inner()());
                  r++;
                  while (r != heads.end())
                    {
                      lim.sql_cmd += ", ?";
                      lim % text(r->inner()());
                      r++;
                    }
                }
              lim.sql_cmd += ") ";
            }
          else
            {
              string certname;
              string prefix;
              string suffix;
              selector_to_certname(i->first, certname, prefix, suffix);
              L(FL("processing selector type %d with i->second '%s'") % ty % i->second);
              if ((i->first == selectors::sel_branch) && (i->second.size() == 0))
                {
                  __app->require_workspace("the empty branch selector b: refers to the current branch");
                  lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=? AND CAST(value AS TEXT) glob ?";
                  lim % text(branch_cert_name) % text(__app->branch_name());
                  L(FL("limiting to current branch '%s'") % __app->branch_name);
                }
              else
                {
                  lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=? AND ";
                  lim % text(certname);
                  switch (i->first)
                    {
                    case selectors::sel_earlier:
                      lim.sql_cmd += "value <= ?";
                      lim % blob(i->second);
                      break;
                    case selectors::sel_later:
                      lim.sql_cmd += "value > ?";
                      lim % blob(i->second);
                      break;
                    default:
                      lim.sql_cmd += "CAST(value AS TEXT) glob ?";
                      lim % text(prefix + i->second + suffix);
                      break;
                    }
                }
            }
          //L(FL("found selector type %d, selecting_head is now %d") % i->first % selecting_head);
        }
    }
  lim.sql_cmd += ")";

  // step 2: depending on what we've been asked to disambiguate, we
  // will complete either some idents, or cert values, or "unknown"
  // which generally means "author, tag or branch"

  if (ty == selectors::sel_ident)
    {
      lim.sql_cmd = "SELECT id FROM " + lim.sql_cmd;
    }
  else
    {
      string prefix = "*";
      string suffix = "*";
      lim.sql_cmd = "SELECT value FROM revision_certs WHERE";
      if (ty == selectors::sel_unknown)
        {
          lim.sql_cmd += " (name=? OR name=? OR name=?)";
          lim % text(author_cert_name) % text(tag_cert_name) % text(branch_cert_name);
        }
      else
        {
          string certname;
          selector_to_certname(ty, certname, prefix, suffix);
          lim.sql_cmd += " (name=?)";
          lim % text(certname);
        }

      lim.sql_cmd += " AND (CAST(value AS TEXT) GLOB ?) AND (id IN " + lim.sql_cmd + ")";
      lim % text(prefix + partial + suffix);
    }

  results res;
  fetch(res, one_col, any_rows, lim);
  for (size_t i = 0; i < res.size(); ++i)
    {
      if (ty == selectors::sel_ident)
        completions.insert(res[i][0]);
      else
        {
          data row_decoded(res[i][0]);
          completions.insert(row_decoded());
        }
    }
}

// epochs

void
database::get_epochs(map<cert_value, epoch_data> & epochs)
{
  epochs.clear();
  results res;
  fetch(res, 2, any_rows, query("SELECT branch, epoch FROM branch_epochs"));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      cert_value decoded(idx(*i, 0));
      I(epochs.find(decoded) == epochs.end());
      epochs.insert(make_pair(decoded, epoch_data(idx(*i, 1))));
    }
}

void
database::get_epoch(epoch_id const & eid,
                    cert_value & branch, epoch_data & epo)
{
  I(epoch_exists(eid));
  results res;
  fetch(res, 2, any_rows,
        query("SELECT branch, epoch FROM branch_epochs"
        " WHERE hash = ?")
        % text(eid.inner()()));
  I(res.size() == 1);
  branch = cert_value(idx(idx(res, 0), 0));
  epo = epoch_data(idx(idx(res, 0), 1));
}

bool
database::epoch_exists(epoch_id const & eid)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT hash FROM branch_epochs WHERE hash = ?")
        % text(eid.inner()()));
  I(res.size() == 1 || res.size() == 0);
  return res.size() == 1;
}

void
database::set_epoch(cert_value const & branch, epoch_data const & epo)
{
  epoch_id eid;
  epoch_hash_code(branch, epo, eid);
  I(epo.inner()().size() == constants::epochlen);
  execute(query("INSERT OR REPLACE INTO branch_epochs VALUES(?, ?, ?)")
          % text(eid.inner()())
          % blob(branch())
          % text(epo.inner()()));
}

void
database::clear_epoch(cert_value const & branch)
{
  execute(query("DELETE FROM branch_epochs WHERE branch = ?")
          % blob(branch()));
}

bool
database::check_integrity()
{
  results res;
  fetch(res, one_col, any_rows, query("PRAGMA integrity_check"));
  I(res.size() == 1);
  I(res[0].size() == 1);

  return res[0][0] == "ok";
}

// vars

void
database::get_vars(map<var_key, var_value> & vars)
{
  vars.clear();
  results res;
  fetch(res, 3, any_rows, query("SELECT domain, name, value FROM db_vars"));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      var_domain domain(idx(*i, 0));
      var_name name(idx(*i, 1));
      var_value value(idx(*i, 2));
      I(vars.find(make_pair(domain, name)) == vars.end());
      vars.insert(make_pair(make_pair(domain, name), value));
    }
}

void
database::get_var(var_key const & key, var_value & value)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  map<var_key, var_value> vars;
  get_vars(vars);
  map<var_key, var_value>::const_iterator i = vars.find(key);
  I(i != vars.end());
  value = i->second;
}

bool
database::var_exists(var_key const & key)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  map<var_key, var_value> vars;
  get_vars(vars);
  map<var_key, var_value>::const_iterator i = vars.find(key);
  return i != vars.end();
}

void
database::set_var(var_key const & key, var_value const & value)
{
  execute(query("INSERT OR REPLACE INTO db_vars VALUES(?, ?, ?)")
          % text(key.first())
          % blob(key.second())
          % blob(value()));
}

void
database::clear_var(var_key const & key)
{
  execute(query("DELETE FROM db_vars WHERE domain = ? AND name = ?")
          % text(key.first())
          % blob(key.second()));
}

// branches

void
database::get_branches(vector<string> & names)
{
    results res;
    query q("SELECT DISTINCT value FROM revision_certs WHERE name = ?");
    string cert_name = "branch";
    fetch(res, one_col, any_rows, q % text(cert_name));
    for (size_t i = 0; i < res.size(); ++i)
      {
        names.push_back(res[i][0]);
      }
}

void
database::get_roster_id_for_revision(revision_id const & rev_id,
                                     roster_id & ros_id)
{
  if (rev_id.inner()().empty())
    {
      ros_id = roster_id();
      return;
    }

  results res;
  query q("SELECT roster_id FROM revision_roster WHERE rev_id = ? ");
  fetch(res, one_col, any_rows, q % text(rev_id.inner()()));
  I(res.size() == 1);
  ros_id = roster_id(res[0][0]);
}

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster)
{
  marking_map mm;
  get_roster(rev_id, roster, mm);
}

static LRUCache<revision_id,
                shared_ptr<pair<roster_t, marking_map> > >
rcache(constants::db_roster_cache_sz);

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster,
                     marking_map & marks)
{
  if (rev_id.inner()().empty())
    {
      roster = roster_t();
      marks = marking_map();
      return;
    }

  shared_ptr<pair<roster_t, marking_map> > sp;
  if (rcache.fetch(rev_id, sp))
    {
      roster = sp->first;
      marks = sp->second;
      return;
    }

  roster_data dat;
  roster_id ident;

  get_roster_id_for_revision(rev_id, ident);
  get_roster_version(ident, dat);
  read_roster_and_marking(dat, roster, marks);
  sp = shared_ptr<pair<roster_t, marking_map> >
    (new pair<roster_t, marking_map>(roster, marks));
  rcache.insert(rev_id, sp);
}


void
database::put_roster(revision_id const & rev_id,
                     roster_t & roster,
                     marking_map & marks)
{
  MM(rev_id);
  roster_data old_data, new_data;
  delta reverse_delta;
  roster_id old_id, new_id;

  if (!rcache.exists(rev_id))
    {
      shared_ptr<pair<roster_t, marking_map> > sp
        (new pair<roster_t, marking_map>(roster, marks));
      rcache.insert(rev_id, sp);
    }

  write_roster_and_marking(roster, marks, new_data);
  calculate_ident(new_data, new_id);

  // First: find the "old" revision; if there are multiple old
  // revisions, we just pick the first. It probably doesn't matter for
  // the sake of delta-encoding.

  string data_table = "rosters";
  string delta_table = "roster_deltas";

  transaction_guard guard(*this);

  execute(query("INSERT into revision_roster VALUES (?, ?)")
          % text(rev_id.inner()())
          % text(new_id.inner()()));

  if (exists(new_id.inner(), data_table)
      || delta_exists(new_id.inner(), delta_table))
    {
      guard.commit();
      return;
    }

  // Else we have a new roster the database hasn't seen yet; our task is to
  // add it, and deltify all the incoming edges (if they aren't already).

  schedule_write(data_table, new_id.inner(), new_data.inner());

  set<revision_id> parents;
  get_revision_parents(rev_id, parents);

  // Now do what deltify would do if we bothered (we have the
  // roster written now, so might as well do it here).
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
    {
      if (null_id(*i))
        continue;
      revision_id old_rev = *i;
      get_roster_id_for_revision(old_rev, old_id);
      if (exists(old_id.inner(), data_table))
        {
          get_roster_version(old_id, old_data);
          diff(new_data.inner(), old_data.inner(), reverse_delta);
          if (have_pending_write(data_table, old_id.inner()))
            cancel_pending_write(data_table, old_id.inner());
          else
            drop(old_id.inner(), data_table);
          put_delta(old_id.inner(), new_id.inner(), reverse_delta, delta_table);
        }
    }
  guard.commit();
}


typedef hashmap::hash_multimap<string, string> ancestry_map;

static void
transitive_closure(string const & x,
                   ancestry_map const & m,
                   set<revision_id> & results)
{
  results.clear();

  deque<string> work;
  work.push_back(x);
  while (!work.empty())
    {
      string c = work.front();
      work.pop_front();
      revision_id curr(c);
      if (results.find(curr) == results.end())
        {
          results.insert(curr);
          pair<ancestry_map::const_iterator, ancestry_map::const_iterator> \
            range(m.equal_range(c));
          for (ancestry_map::const_iterator i(range.first); i != range.second; ++i)
            {
              if (i->first == c)
                work.push_back(i->second);
            }
        }
    }
}

void
database::get_uncommon_ancestors(revision_id const & a,
                                 revision_id const & b,
                                 set<revision_id> & a_uncommon_ancs,
                                 set<revision_id> & b_uncommon_ancs)
{
  // FIXME: This is a somewhat ugly, and possibly unaccepably slow way
  // to do it. Another approach involves maintaining frontier sets for
  // each and slowly deepening them into history; would need to
  // benchmark to know which is actually faster on real datasets.

  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  results res;
  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  fetch(res, 2, any_rows,
        query("SELECT parent,child FROM revision_ancestry"));

  set<revision_id> a_ancs, b_ancs;

  ancestry_map child_to_parent_map;
  for (size_t i = 0; i < res.size(); ++i)
    child_to_parent_map.insert(make_pair(res[i][1], res[i][0]));

  transitive_closure(a.inner()(), child_to_parent_map, a_ancs);
  transitive_closure(b.inner()(), child_to_parent_map, b_ancs);

  set_difference(a_ancs.begin(), a_ancs.end(),
                 b_ancs.begin(), b_ancs.end(),
                 inserter(a_uncommon_ancs, a_uncommon_ancs.begin()));

  set_difference(b_ancs.begin(), b_ancs.end(),
                 a_ancs.begin(), a_ancs.end(),
                 inserter(b_uncommon_ancs, b_uncommon_ancs.begin()));
}

node_id
database::next_node_id()
{
  transaction_guard guard(*this);
  results res;

  // We implement this as a fixed db var.

  fetch(res, one_col, any_rows,
        query("SELECT node FROM next_roster_node_number"));

  node_id n;
  if (res.empty())
    {
      n = 1;
      execute (query("INSERT INTO next_roster_node_number VALUES(?)")
               % text(lexical_cast<string>(n)));
    }
  else
    {
      I(res.size() == 1);
      n = lexical_cast<node_id>(res[0][0]);
      ++n;
      execute (query("UPDATE next_roster_node_number SET node = ?")
               % text(lexical_cast<string>(n)));

    }
  guard.commit();
  return n;
}


void
database::check_filename()
{
  N(!filename.empty(), F("no database specified"));
}


void
database::check_db_exists()
{
  require_path_is_file(filename,
                       F("database %s does not exist") % filename,
                       F("%s is a directory, not a database") % filename);
}

bool
database::database_specified()
{
  return !filename.empty();
}


void
database::open()
{
  int error;

  I(!__sql);

  error = sqlite3_open(filename.as_external().c_str(), &__sql);

  if (__sql)
    {
      I(sql_contexts.find(__sql) == sql_contexts.end());
      sql_contexts.insert(__sql);
    }

  N(!error, (F("could not open database '%s': %s")
             % filename % string(sqlite3_errmsg(__sql))));
}

void
database::close()
{
  if (__sql)
    {
      sqlite3_close(__sql);
      I(sql_contexts.find(__sql) != sql_contexts.end());
      sql_contexts.erase(__sql);
      __sql = 0;
    }
}


// transaction guards

transaction_guard::transaction_guard(database & d, bool exclusive,
                                     size_t checkpoint_batch_size,
                                     size_t checkpoint_batch_bytes)
  : committed(false), db(d), exclusive(exclusive),
    checkpoint_batch_size(checkpoint_batch_size),
    checkpoint_batch_bytes(checkpoint_batch_bytes),
    checkpointed_calls(0),
    checkpointed_bytes(0)
{
  db.begin_transaction(exclusive);
}

transaction_guard::~transaction_guard()
{
  if (committed)
    db.commit_transaction();
  else
    db.rollback_transaction();
}

void
transaction_guard::do_checkpoint()
{
  db.commit_transaction();
  db.begin_transaction(exclusive);
  checkpointed_calls = 0;
  checkpointed_bytes = 0;
}

void
transaction_guard::maybe_checkpoint(size_t nbytes)
{
  checkpointed_calls += 1;
  checkpointed_bytes += nbytes;
  if (checkpointed_calls >= checkpoint_batch_size
      || checkpointed_bytes >= checkpoint_batch_bytes)
    do_checkpoint();
}

void
transaction_guard::commit()
{
  committed = true;
}



// called to avoid foo.db-journal files hanging around if we exit cleanly
// without unwinding the stack (happens with SIGINT & SIGTERM)
void
close_all_databases()
{
  L(FL("attempting to rollback and close %d databases") % sql_contexts.size());
  for (set<sqlite3*>::iterator i = sql_contexts.begin();
       i != sql_contexts.end(); i++)
    {
      // the ROLLBACK is required here, even though the sqlite docs
      // imply that transactions are rolled back on database closure
      int exec_err = sqlite3_exec(*i, "ROLLBACK", NULL, NULL, NULL);
      int close_err = sqlite3_close(*i);

      L(FL("exec_err = %d, close_err = %d") % exec_err % close_err);
    }
  sql_contexts.clear();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
