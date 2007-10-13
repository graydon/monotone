// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <list>
#include <set>
#include <sstream>
#include "vector.hh"

#include <string.h>

#include <boost/shared_ptr.hpp>
#include "lexical_cast.hh"

#include "sqlite/sqlite3.h"

#include "app_state.hh"
#include "cert.hh"
#include "cleanup.hh"
#include "constants.hh"
#include "database.hh"
#include "hash_map.hh"
#include "keys.hh"
#include "platform-wrapped.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "schema_migration.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "vocab_cast.hh"
#include "xdelta.hh"
#include "epoch.hh"
#include "graph.hh"
#include "roster_delta.hh"
#include "rev_height.hh"
#include "vocab_hash.hh"
#include "globish.hh"

// defined in schema.c, generated from schema.sql:
extern char const schema_constant[];

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// see file schema.sql for the text of the schema.

using std::deque;
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
  __sql(NULL),
  transaction_level(0),
  roster_cache(constants::db_roster_cache_sz,
               roster_writeback_manager(*this)),
  delayed_writes_size(0)
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
  results res;

  // Check for manifests, revisions, rosters, and heights.
  fetch(res, one_col, any_rows, query("SELECT 1 FROM manifests LIMIT 1"));
  bool have_manifests = !res.empty();
  fetch(res, one_col, any_rows, query("SELECT 1 FROM revisions LIMIT 1"));
  bool have_revisions = !res.empty();
  fetch(res, one_col, any_rows, query("SELECT 1 FROM rosters LIMIT 1"));
  bool have_rosters = !res.empty();
  fetch(res, one_col, any_rows, query("SELECT 1 FROM heights LIMIT 1"));
  bool have_heights = !res.empty();
  

  if (!have_manifests)
    {
      // Must have been changesetified and rosterified already.
      // Or else the database was just created.
      // Do we need to regenerate cached data?
      E(!have_revisions || (have_rosters && have_heights),
        F("database %s lacks some cached data\n"
          "run '%s db regenerate_caches' to restore use of this database")
        % filename % ui.prog_name);
    }
  else
    {
      // The rosters and heights tables should be empty.
      I(!have_rosters && !have_heights);

      // they need to either changesetify or rosterify.  which?
      if (have_revisions)
        E(false,
          F("database %s contains old-style revisions\n"
            "if you are a project leader or doing local testing:\n"
            "  see the file UPGRADE for instructions on upgrading.\n"
            "if you are not a project leader:\n"
            "  wait for a leader to migrate project data, and then\n"
            "  pull into a fresh database.\n"
            "sorry about the inconvenience.")
          % filename);
      else
        E(false,
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

struct sqlite3 *
database::sql(enum open_mode mode)
{
  if (! __sql)
    {
      check_filename();
      check_db_exists();
      open();

      if (mode != schema_bypass_mode)
        {
          check_sql_schema(__sql, filename);

          if (mode != format_bypass_mode)
            check_format();
        }

      install_functions(__app);
    }
  else
    I(mode == normal_mode);

  return __sql;
}

void
database::initialize()
{
  check_filename();
  check_db_nonexistent();
  open();

  sqlite3_exec(__sql, schema_constant, NULL, NULL, NULL);
  assert_sqlite3_ok(__sql);

  sqlite3_exec(__sql, (FL("PRAGMA user_version = %u;")
                       % mtn_creator_code).str().c_str(), NULL, NULL, NULL);
  assert_sqlite3_ok(__sql);

  // make sure what we wanted is what we got
  check_sql_schema(__sql, filename);
  
  close();
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
          out << '\'';
        }
      else
        {
          const unsigned char *val = sqlite3_column_text(stmt, i);
          if (val == NULL)
            out << "NULL";
          else
            {
              out << '\'';
              for (const unsigned char *cp = val; *cp; ++cp)
                {
                  if (*cp == '\'')
                    out << "''";
                  else
                    out << *cp;
                }
              out << '\'';
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
  sqlite3_prepare_v2(dump->sql, query.c_str(), -1, &stmt, NULL);
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

static int
dump_user_version_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(dump->sql != NULL);
  I(vals != NULL);
  I(vals[0] != NULL);
  I(n == 1);
  *(dump->out) << "PRAGMA user_version = " << vals[0] << ";\n";
  return 0;
}

void
database::dump(ostream & out)
{
  ensure_open_for_maintenance();

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
    res = sqlite3_exec(req.sql,
                       "PRAGMA user_version;",
                       dump_user_version_cb, &req, NULL);
    assert_sqlite3_ok(req.sql);
    out << "COMMIT;\n";
    guard.commit();
  }
}

void
database::load(istream & in)
{
  string line;
  string sql_stmt;

  check_filename();
  check_db_nonexistent();
  open();

  // the page size can only be set before any other commands have been executed
  sqlite3_exec(__sql, "PRAGMA page_size=8192", NULL, NULL, NULL);
  assert_sqlite3_ok(__sql);

  while(in)
    {
      getline(in, line, ';');
      sql_stmt += line + ';';

      if (sqlite3_complete(sql_stmt.c_str()))
        {
          sqlite3_exec(__sql, sql_stmt.c_str(), NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);
          sql_stmt.clear();
        }
    }

  assert_sqlite3_ok(__sql);
}


void
database::debug(string const & sql, ostream & out)
{
  ensure_open_for_maintenance();

  results res;
  fetch(res, any_cols, any_rows, query(sql));
  out << '\'' << sql << "' -> " << res.size() << " rows\n\n";
  for (size_t i = 0; i < res.size(); ++i)
    {
      for (size_t j = 0; j < res[i].size(); ++j)
        {
          if (j != 0)
            out << " | ";
          out << res[i][j];
        }
      out << '\n';
    }
}

// Subroutine of info().  This compares strings that might either be numbers
// or error messages surrounded by square brackets.  We want the longest
// number, even if there's an error message that's longer than that.
static bool longest_number(string a, string b)
{
  if(a.length() > 0 && a[0] == '[')
    return true;  // b is longer
  if(b.length() > 0 && b[0] == '[')
    return false; // a is longer

  return a.length() < b.length();
}

// Subroutine of info() and some things it calls.
// Given an informative_failure which is believed to represent an SQLite
// error, either return a string version of the error message (if it was an
// SQLite error) or rethrow the execption (if it wasn't).
static string
format_sqlite_error_for_info(informative_failure const & e)
{
  string err(e.what());
  string prefix = _("error: ");
  prefix.append(_("sqlite error: "));
  if (err.find(prefix) != 0)
    throw;

  err.replace(0, prefix.length(), "[");
  string::size_type nl = err.find('\n');
  if (nl != string::npos)
    err.erase(nl);

  err.append("]");
  return err;
}

// Subroutine of info().  Pretty-print the database's "creator code", which
// is a 32-bit unsigned number that we interpret as a four-character ASCII
// string, provided that all four characters are graphic.  (On disk, it's
// stored in the "user version" field of the database.)
static string
format_creator_code(u32 code)
{
  char buf[5];
  string result;

  if (code == 0)
    return _("not set");

  buf[4] = '\0';
  buf[3] = ((code & 0x000000ff) >>  0);
  buf[2] = ((code & 0x0000ff00) >>  8);
  buf[1] = ((code & 0x00ff0000) >> 16);
  buf[0] = ((code & 0xff000000) >> 24);

  if (isgraph(buf[0]) && isgraph(buf[1]) && isgraph(buf[2]) && isgraph(buf[3]))
    result = (FL("%s (0x%08x)") % buf % code).str();
  else
    result = (FL("0x%08x") % code).str();
  if (code != mtn_creator_code)
    result += _(" (not a monotone database)");
  return result;
}


void
database::info(ostream & out)
{
  // don't check the schema
  ensure_open_for_maintenance();

  // do a dummy query to confirm that the database file is an sqlite3
  // database.  (this doesn't happen on open() because sqlite postpones the
  // actual file open until the first access.  we can't piggyback it on the
  // query of the user version because there's a bug in sqlite 3.3.10:
  // the routine that reads meta-values from the database header does not
  // check the file format.  reported as sqlite bug #2182.)
  sqlite3_exec(__sql, "SELECT 1 FROM sqlite_master LIMIT 0", 0, 0, 0);
  assert_sqlite3_ok(__sql);

  u32 ccode;
  {
    results res;
    fetch(res, one_col, one_row, query("PRAGMA user_version"));
    I(res.size() == 1);
    ccode = lexical_cast<u32>(res[0][0]);
  }

  vector<string> counts;
  counts.push_back(count("rosters"));
  counts.push_back(count("roster_deltas"));
  counts.push_back(count("files"));
  counts.push_back(count("file_deltas"));
  counts.push_back(count("revisions"));
  counts.push_back(count("revision_ancestry"));
  counts.push_back(count("revision_certs"));

  {
    results res;
    try
      {
        fetch(res, one_col, any_rows,
              query("SELECT node FROM next_roster_node_number"));
        if (res.empty())
          counts.push_back("0");
        else
          {
            I(res.size() == 1);
            counts.push_back((F("%u")
                              % (lexical_cast<u64>(res[0][0]) - 1)).str());
          }
      }
    catch (informative_failure const & e)
      {
        counts.push_back(format_sqlite_error_for_info(e));
      }
  }

  vector<string> bytes;
  {
    u64 total = 0;
    bytes.push_back(space("rosters",
                          "length(id) + length(checksum) + length(data)",
                          total));
    bytes.push_back(space("roster_deltas",
                          "length(id) + length(checksum)"
                          "+ length(base) + length(delta)", total));
    bytes.push_back(space("files", "length(id) + length(data)", total));
    bytes.push_back(space("file_deltas",
                          "length(id) + length(base) + length(delta)", total));
    bytes.push_back(space("revisions", "length(id) + length(data)", total));
    bytes.push_back(space("revision_ancestry",
                          "length(parent) + length(child)", total));
    bytes.push_back(space("revision_certs",
                          "length(hash) + length(id) + length(name)"
                          "+ length(value) + length(keypair)"
                          "+ length(signature)", total));
    bytes.push_back(space("heights", "length(revision) + length(height)",
                          total));
    bytes.push_back((F("%u") % total).str());
  }

  // pad each vector's strings on the left with spaces to make them all the
  // same length
  {
    string::size_type width
      = max_element(counts.begin(), counts.end(), longest_number)->length();
    for(vector<string>::iterator i = counts.begin(); i != counts.end(); i++)
      if (width > i->length() && (*i)[0] != '[')
        i->insert(0, width - i->length(), ' ');

    width = max_element(bytes.begin(), bytes.end(), longest_number)->length();
    for(vector<string>::iterator i = bytes.begin(); i != bytes.end(); i++)
      if (width > i->length() && (*i)[0] != '[')
        i->insert(0, width - i->length(), ' ');
  }

  i18n_format form =
    F("creator code      : %s\n"
      "schema version    : %s\n"
      "counts:\n"
      "  full rosters    : %s\n"
      "  roster deltas   : %s\n"
      "  full files      : %s\n"
      "  file deltas     : %s\n"
      "  revisions       : %s\n"
      "  ancestry edges  : %s\n"
      "  certs           : %s\n"
      "  logical files   : %s\n"
      "bytes:\n"
      "  full rosters    : %s\n"
      "  roster deltas   : %s\n"
      "  full files      : %s\n"
      "  file deltas     : %s\n"
      "  revisions       : %s\n"
      "  cached ancestry : %s\n"
      "  certs           : %s\n"
      "  heights         : %s\n"
      "  total           : %s\n"
      "database:\n"
      "  page size       : %s\n"
      "  cache size      : %s"
      );

  form = form % format_creator_code(ccode);
  form = form % describe_sql_schema(__sql);

  for (vector<string>::iterator i = counts.begin(); i != counts.end(); i++)
    form = form % *i;

  for (vector<string>::iterator i = bytes.begin(); i != bytes.end(); i++)
    form = form % *i;

  form = form % page_size();
  form = form % cache_size();

  out << form.str() << '\n'; // final newline is kept out of the translation
}

void
database::version(ostream & out)
{
  ensure_open_for_maintenance();
  out << (F("database schema version: %s") % describe_sql_schema(__sql)).str()
      << '\n';
}

void
database::migrate()
{
  ensure_open_for_maintenance();
  migrate_sql_schema(__sql, *__app);
}

void
database::test_migration_step(string const & schema)
{
  ensure_open_for_maintenance();
  ::test_migration_step(__sql, *__app, schema);
}

void
database::ensure_open()
{
  sql();
}

void
database::ensure_open_for_format_changes()
{
  sql(format_bypass_mode);
}

void
database::ensure_open_for_maintenance()
{
  sql(schema_bypass_mode);
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

  if (__sql)
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
      sqlite3_prepare_v2(sql(), query.sql_cmd.c_str(), -1, i->second.stmt.paddr(), &tail);
      assert_sqlite3_ok(sql());
      L(FL("prepared statement %s") % query.sql_cmd);

      // no support for multiple statements here
      E(*tail == 0,
        F("multiple statements in query: %s") % query.sql_cmd);
    }

  ncol = sqlite3_column_count(i->second.stmt());

  E(want_cols == any_cols || want_cols == ncol,
    F("wanted %d columns got %d in query: %s") % want_cols % ncol % query.sql_cmd);

  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(i->second.stmt());

  // Ensure that exactly the right number of parameters were given
  I(params == int(query.args.size()));

  // profiling finds this logging to be quite expensive
  if (global_sanity.debug_p())
    L(FL("binding %d parameters for %s") % params % query.sql_cmd);

  for (int param = 1; param <= params; param++)
    {
      // profiling finds this logging to be quite expensive
      if (global_sanity.debug_p())
        {
          string log;
          switch (query.args[param-1].type)
            { // FIXME: this is somewhat ugly...
            case query_param::text:
              log = query.args[param-1].data;
              if (log.size() > constants::log_line_sz)
                log = log.substr(0, constants::log_line_sz);
              L(FL("binding %d with value '%s'") % param % log);
              break;
            case query_param::blob:
              log = encode_hexenc(query.args[param-1].data);
              if (log.size() > constants::log_line_sz)
                log = log.substr(0, constants::log_line_sz);
              L(FL("binding %d with value x'%s'") % param % log);
              break;
            default:
              L(FL("binding %d with unknown type") % param);
            }
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

bool
database::table_has_entry(hexenc<id> const & key, std::string const & column,
                          std::string const & table)
{
  results res;
  query q("SELECT 1 FROM " + table + " WHERE " + column + " = ? LIMIT 1");
  fetch(res, one_col, any_rows, q % blob(decode_hexenc(key())));
  return !res.empty();
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
      I(delayed_files.empty());
      I(roster_cache.all_clean());
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
database::size_delayed_file(file_id const & id, file_data const & dat)
{
  return id.inner()().size() + dat.inner()().size();
}

bool
database::have_delayed_file(file_id const & id)
{
  return delayed_files.find(id) != delayed_files.end();
}

void
database::load_delayed_file(file_id const & id, file_data & dat)
{
  dat = safe_get(delayed_files, id);
}

// precondition: have_delayed_file(an_id) == true
void
database::cancel_delayed_file(file_id const & an_id)
{
  file_data const & dat = safe_get(delayed_files, an_id);
  size_t cancel_size = size_delayed_file(an_id, dat);
  I(cancel_size <= delayed_writes_size);
  delayed_writes_size -= cancel_size;

  safe_erase(delayed_files, an_id);
}

void
database::drop_or_cancel_file(file_id const & id)
{
  if (have_delayed_file(id))
    cancel_delayed_file(id);
  else
    drop(id.inner(), "files");
}

void
database::schedule_delayed_file(file_id const & an_id,
                                file_data const & dat)
{
  if (!have_delayed_file(an_id))
    {
      safe_insert(delayed_files, make_pair(an_id, dat));
      delayed_writes_size += size_delayed_file(an_id, dat);
    }
  if (delayed_writes_size > constants::db_max_delayed_file_bytes)
    flush_delayed_writes();
}

void
database::flush_delayed_writes()
{
  for (map<file_id, file_data>::const_iterator i = delayed_files.begin();
       i != delayed_files.end(); ++i)
    write_delayed_file(i->first, i->second);
  clear_delayed_writes();
}

void
database::clear_delayed_writes()
{
  delayed_files.clear();
  delayed_writes_size = 0;
}

void
database::roster_writeback_manager::writeout(revision_id const & id,
                                             cached_roster const & cr)
{
  I(cr.first);
  I(cr.second);
  db.write_delayed_roster(id, *(cr.first), *(cr.second));
}

unsigned long
database::roster_size_estimator::operator()(cached_roster const & cr)
{
  I(cr.first);
  I(cr.second);
  // do estimate using a totally made up multiplier, probably wildly off
  return cr.first->all_nodes().size() * constants::db_estimated_roster_node_sz;
}

void
database::commit_transaction()
{
  if (transaction_level == 1)
    {
      flush_delayed_writes();
      roster_cache.clean_all();
      execute(query("COMMIT"));
    }
  transaction_level--;
}

void
database::rollback_transaction()
{
  if (transaction_level == 1)
    {
      clear_delayed_writes();
      roster_cache.clear_and_drop_writes();
      execute(query("ROLLBACK"));
    }
  transaction_level--;
}


bool
database::file_or_manifest_base_exists(hexenc<id> const & ident,
                                       std::string const & table)
{
  // just check for a delayed file, since there are no delayed manifests
  if (have_delayed_file(file_id(ident)))
    return true;
  return table_has_entry(ident, "id", table);
}

// returns true if we are currently storing (or planning to store) a
// full-text for 'ident'
bool
database::roster_base_stored(revision_id const & ident)
{
  if (roster_cache.exists(ident) && roster_cache.is_dirty(ident))
    return true;
  return table_has_entry(ident.inner(), "id", "rosters");
}

// returns true if we currently have a full-text for 'ident' available
// (possibly cached).  Warning: the results of this method are invalidated
// by calling roster_cache.insert_{clean,dirty}, because they can trigger
// cache cleaning.
bool
database::roster_base_available(revision_id const & ident)
{
  if (roster_cache.exists(ident))
    return true;
  return table_has_entry(ident.inner(), "id", "rosters");
}

bool
database::delta_exists(hexenc<id> const & ident,
                       string const & table)
{
  return table_has_entry(ident, "id", table);
}

bool
database::delta_exists(hexenc<id> const & ident,
                       hexenc<id> const & base,
                       string const & table)
{
  results res;
  query q("SELECT 1 FROM " + table + " WHERE id = ? and base = ? LIMIT 1");
  fetch(res, one_col, any_rows,
        q % blob(decode_hexenc(ident())) % blob(decode_hexenc(base())));
  return !res.empty();
}

string
database::count(string const & table)
{
  try
    {
      results res;
      query q("SELECT COUNT(*) FROM " + table);
      fetch(res, one_col, one_row, q);
      return (F("%u") % lexical_cast<u64>(res[0][0])).str();
    }
  catch (informative_failure const & e)
    {
      return format_sqlite_error_for_info(e);
    }
        
}

string
database::space(string const & table, string const & rowspace, u64 & total)
{
  try
    {
      results res;
      // SUM({empty set}) is NULL; TOTAL({empty set}) is 0.0
      query q("SELECT TOTAL(" + rowspace + ") FROM " + table);
      fetch(res, one_col, one_row, q);
      u64 bytes = static_cast<u64>(lexical_cast<double>(res[0][0]));
      total += bytes;
      return (F("%u") % bytes).str();
    }
  catch (informative_failure & e)
    {
      return format_sqlite_error_for_info(e);
    }
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
      ids.insert(hexenc<id>(encode_hexenc(res[i][0])));
    }
}

// for files and legacy manifest support
void
database::get_file_or_manifest_base_unchecked(hexenc<id> const & ident,
                                              data & dat,
                                              string const & table)
{
  if (have_delayed_file(file_id(ident)))
    {
      file_data tmp;
      load_delayed_file(file_id(ident), tmp);
      dat = tmp.inner();
      return;
    }

  results res;
  query q("SELECT data FROM " + table + " WHERE id = ?");
  fetch(res, one_col, one_row, q % blob(decode_hexenc(ident())));

  gzip<data> rdata(res[0][0]);
  data rdata_unpacked;
  decode_gzip(rdata,rdata_unpacked);

  dat = rdata_unpacked;
}

// for files and legacy manifest support
void
database::get_file_or_manifest_delta_unchecked(hexenc<id> const & ident,
                                               hexenc<id> const & base,
                                               delta & del,
                                               string const & table)
{
  I(ident() != "");
  I(base() != "");
  results res;
  query q("SELECT delta FROM " + table + " WHERE id = ? AND base = ?");
  fetch(res, one_col, one_row,
        q % blob(decode_hexenc(ident())) % blob(decode_hexenc(base())));

  gzip<delta> del_packed(res[0][0]);
  decode_gzip(del_packed, del);
}

void
database::get_roster_base(revision_id const & ident,
                          roster_t & roster, marking_map & marking)
{
  if (roster_cache.exists(ident))
    {
      cached_roster cr;
      roster_cache.fetch(ident, cr);
      I(cr.first);
      roster = *(cr.first);
      I(cr.second);
      marking = *(cr.second);
      return;
    }
  results res;
  query q("SELECT checksum, data FROM rosters WHERE id = ?");
  fetch(res, 2, one_row, q % blob(decode_hexenc(ident.inner()())));

  hexenc<id> checksum(res[0][0]);
  hexenc<id> calculated;
  calculate_ident(data(res[0][1]), calculated);
  I(calculated == checksum);

  gzip<data> dat_packed(res[0][1]);
  data dat;
  decode_gzip(dat_packed, dat);
  read_roster_and_marking(roster_data(dat), roster, marking);
}

void
database::get_roster_delta(hexenc<id> const & ident,
                           hexenc<id> const & base,
                           roster<delta> & del)
{
  results res;
  query q("SELECT checksum, delta FROM roster_deltas WHERE id = ? AND base = ?");
  fetch(res, 2, one_row, q % blob(decode_hexenc(ident())) % blob(decode_hexenc(base())));

  hexenc<id> checksum(res[0][0]);
  hexenc<id> calculated;
  calculate_ident(data(res[0][1]), calculated);
  I(calculated == checksum);

  gzip<delta> del_packed(res[0][1]);
  delta tmp;
  decode_gzip(del_packed, tmp);
  del = roster<delta>(tmp);
}

void
database::write_delayed_file(file_id const & ident,
                             file_data const & dat)
{
  gzip<data> dat_packed;
  encode_gzip(dat.inner(), dat_packed);

  // ident is a hash, which we should check
  I(!null_id(ident));
  file_id tid;
  calculate_ident(dat, tid);
  MM(ident);
  MM(tid);
  I(tid == ident);
  // and then write things to the db
  query q("INSERT INTO files (id, data) VALUES (?, ?)");
  execute(q % blob(decode_hexenc(ident.inner()())) % blob(dat_packed()));
}

void
database::write_delayed_roster(revision_id const & ident,
                               roster_t const & roster,
                               marking_map const & marking)
{
  roster_data dat;
  write_roster_and_marking(roster, marking, dat);
  gzip<data> dat_packed;
  encode_gzip(dat.inner(), dat_packed);

  // ident is a number, and we should calculate a checksum on what
  // we write
  hexenc<id> checksum;
  calculate_ident(data(dat_packed()), checksum);

  // and then write it
  query q("INSERT INTO rosters (id, checksum, data) VALUES (?, ?, ?)");
  execute(q % blob(decode_hexenc(ident.inner()())) % text(checksum()) % blob(dat_packed()));
}


void
database::put_file_delta(file_id const & ident,
                         file_id const & base,
                         file_delta const & del)
{
  // nb: delta schema is (id, base, delta)
  I(!null_id(ident));
  I(!null_id(base));

  gzip<delta> del_packed;
  encode_gzip(del.inner(), del_packed);

  query q("INSERT INTO file_deltas VALUES (?, ?, ?)");
  execute(q
          % blob(decode_hexenc(ident.inner()()))
          % blob(decode_hexenc(base.inner()()))
          % blob(del_packed()));
}

void
database::put_roster_delta(revision_id const & ident,
                           revision_id const & base,
                           roster_delta const & del)
{
  gzip<delta> del_packed;
  encode_gzip(del.inner(), del_packed);

  hexenc<id> checksum;
  calculate_ident(data(del_packed()), checksum);

  query q("INSERT INTO roster_deltas (id, base, checksum, delta) VALUES (?, ?, ?, ?)");
  execute(q
          % blob(decode_hexenc(ident.inner()()))
          % blob(decode_hexenc(base.inner()()))
          % text(checksum())
          % blob(del_packed()));
}


// static ticker cache_hits("vcache hits", "h", 1);

struct datasz
{
  unsigned long operator()(data const & t) { return t().size(); }
};

static LRUWritebackCache<string, data, datasz>
vcache(constants::db_version_cache_sz);

struct file_and_manifest_reconstruction_graph : public reconstruction_graph
{
  database & db;
  string const & data_table;
  string const & delta_table;

  file_and_manifest_reconstruction_graph(database & db,
                                         string const & data_table,
                                         string const & delta_table)
    : db(db), data_table(data_table), delta_table(delta_table)
  {}
  virtual bool is_base(hexenc<id> const & node) const
  {
    return vcache.exists(node())
      || db.file_or_manifest_base_exists(node, data_table);
  }
  virtual void get_next(hexenc<id> const & from, std::set< hexenc<id> > & next) const
  {
    next.clear();
    database::results res;
    query q("SELECT base FROM " + delta_table + " WHERE id = ?");
    db.fetch(res, one_col, any_rows, q % blob(decode_hexenc(from())));
    for (database::results::const_iterator i = res.begin(); i != res.end(); ++i)
      next.insert(hexenc<id>(encode_hexenc((*i)[0])));
  }
};

// used for files and legacy manifest migration
void
database::get_version(hexenc<id> const & ident,
                      data & dat,
                      string const & data_table,
                      string const & delta_table)
{
  I(ident() != "");

  reconstruction_path selected_path;
  {
    file_and_manifest_reconstruction_graph graph(*this, data_table, delta_table);
    get_reconstruction_path(ident, graph, selected_path);
  }

  I(!selected_path.empty());

  hexenc<id> curr = hexenc<id>(selected_path.back());
  selected_path.pop_back();
  data begin;

  if (vcache.exists(curr()))
    I(vcache.fetch(curr(), begin));
  else
    get_file_or_manifest_base_unchecked(curr, begin, data_table);

  shared_ptr<delta_applicator> appl = new_piecewise_applicator();
  appl->begin(begin());

  for (reconstruction_path::reverse_iterator i = selected_path.rbegin();
       i != selected_path.rend(); ++i)
    {
      hexenc<id> const nxt = hexenc<id>(*i);

      if (!vcache.exists(curr()))
        {
          string tmp;
          appl->finish(tmp);
          vcache.insert_clean(curr(), data(tmp));
        }

      L(FL("following delta %s -> %s") % curr % nxt);
      delta del;
      get_file_or_manifest_delta_unchecked(nxt, curr, del, delta_table);
      apply_delta(appl, del());

      appl->next();
      curr = nxt;
    }

  string tmp;
  appl->finish(tmp);
  dat = data(tmp);

  hexenc<id> final;
  calculate_ident(dat, final);
  I(final == ident);

  if (!vcache.exists(ident()))
    vcache.insert_clean(ident(), dat);
}

struct roster_reconstruction_graph : public reconstruction_graph
{
  database & db;
  roster_reconstruction_graph(database & db) : db(db) {}
  virtual bool is_base(hexenc<id> const & node) const
  {
    return db.roster_base_available(revision_id(node));
  }
  virtual void get_next(hexenc<id> const & from, std::set< hexenc<id> > & next) const
  {
    next.clear();
    database::results res;
    query q("SELECT base FROM roster_deltas WHERE id = ?");
    db.fetch(res, one_col, any_rows, q % blob(decode_hexenc(from())));
    for (database::results::const_iterator i = res.begin(); i != res.end(); ++i)
      next.insert(hexenc<id>(encode_hexenc((*i)[0])));
  }
};

struct database::extractor
{
  virtual bool look_at_delta(roster_delta const & del) = 0;
  virtual void look_at_roster(roster_t const & roster, marking_map const & mm) = 0;
  virtual ~extractor() {};
};

struct database::markings_extractor : public database::extractor
{
private:
  node_id const & nid;
  marking_t & markings;

public:
  markings_extractor(node_id const & _nid, marking_t & _markings) :
    nid(_nid), markings(_markings) {} ;
  
  bool look_at_delta(roster_delta const & del)
  {
    return try_get_markings_from_roster_delta(del, nid, markings);
  }
  
  void look_at_roster(roster_t const & roster, marking_map const & mm)
  {
    marking_map::const_iterator mmi =
      mm.find(nid);
    I(mmi != mm.end());
    markings = mmi->second;
  }
};

struct database::file_content_extractor : database::extractor
{
private:
  node_id const & nid;
  file_id & content;

public:
  file_content_extractor(node_id const & _nid, file_id & _content) :
    nid(_nid), content(_content) {} ;

  bool look_at_delta(roster_delta const & del)
  {
    return try_get_content_from_roster_delta(del, nid, content);
  }

  void look_at_roster(roster_t const & roster, marking_map const & mm)
  {
    if (roster.has_node(nid))
      content = downcast_to_file_t(roster.get_node(nid))->content;
    else
      content = file_id();
  }
};

void
database::extract_from_deltas(revision_id const & ident, extractor & x)
{
  reconstruction_path selected_path;
  {
    roster_reconstruction_graph graph(*this);
    {
      // we look at the nearest delta(s) first, without constructing the
      // whole path, as that would be a rather expensive operation.
      //
      // the reason why this strategy is worth the effort is, that in most
      // cases we are looking at the parent of a (content-)marked node, thus
      // the information we are for is right there in the delta leading to
      // this node.
      // 
      // recording the deltas visited here in a set as to avoid inspecting
      // them later seems to be of little value, as it imposes a cost here,
      // but can seldom be exploited.
      set< hexenc<id> > deltas;
      graph.get_next(ident.inner(), deltas);
      for (set< hexenc<id> >::const_iterator i = deltas.begin();
           i != deltas.end(); ++i)
        {
          roster_delta del;
          get_roster_delta(ident.inner(), *i, del);
          bool found = x.look_at_delta(del);
          if (found)
            return;
        }
    }
    get_reconstruction_path(ident.inner(), graph, selected_path);
  }

  int path_length(selected_path.size());
  int i(0);
  hexenc<id> target_rev;

  for (reconstruction_path::const_iterator p = selected_path.begin();
       p != selected_path.end(); ++p)
    {
      if (i > 0)
        {
          roster_delta del;
          get_roster_delta(target_rev, *p, del);
          bool found = x.look_at_delta(del);
          if (found)
            return;
        }
      if (i == path_length-1)
        {
          // last iteration, we have reached a roster base
          roster_t roster;
          marking_map mm;
          get_roster_base(revision_id(*p), roster, mm);
          x.look_at_roster(roster, mm);
          return;
        }
      target_rev = *p;
      ++i;
    }
}

void
database::get_markings(revision_id const & id,
                       node_id const & nid,
                       marking_t & markings)
{
  markings_extractor x(nid, markings);
  extract_from_deltas(id, x);
}

void
database::get_file_content(revision_id const & id,
                           node_id const & nid,
                           file_id & content)
{
  // the imaginary root revision doesn't have any file.
  if (null_id(id))
    {
      content = file_id();
      return;
    }
  file_content_extractor x(nid, content);
  extract_from_deltas(id, x);
}

void
database::get_roster_version(revision_id const & ros_id,
                             cached_roster & cr)
{
  // if we already have it, exit early
  if (roster_cache.exists(ros_id))
    {
      roster_cache.fetch(ros_id, cr);
      return;
    }

  reconstruction_path selected_path;
  {
    roster_reconstruction_graph graph(*this);
    get_reconstruction_path(ros_id.inner(), graph, selected_path);
  }

  hexenc<id> curr = selected_path.back();
  selected_path.pop_back();
  // we know that this isn't already in the cache (because of the early exit
  // above), so we should create new objects and spend time filling them in.
  shared_ptr<roster_t> roster(new roster_t);
  shared_ptr<marking_map> marking(new marking_map);
  get_roster_base(revision_id(curr), *roster, *marking);

  for (reconstruction_path::reverse_iterator i = selected_path.rbegin();
       i != selected_path.rend(); ++i)
    {
      hexenc<id> const nxt = *i;
      L(FL("following delta %s -> %s") % curr % nxt);
      roster_delta del;
      get_roster_delta(nxt, curr, del);
      apply_roster_delta(del, *roster, *marking);
      curr = nxt;
    }

  // Double-check that the thing we got out looks okay.  We know that when
  // the roster was written to the database, it passed both of these tests,
  // and we also know that the data on disk has passed our checks for data
  // corruption -- so in theory, we know that what we got out is exactly
  // what we put in, and these checks are redundant.  (They cannot catch all
  // possible errors in any case, e.g., they don't test that the marking is
  // correct.)  What they can do, though, is serve as a sanity check on the
  // delta reconstruction code; if there is a bug where we put something
  // into the database and then later get something different back out, then
  // this is the only thing that can catch it.
  roster->check_sane_against(*marking);
  manifest_id expected_mid, actual_mid;
  get_revision_manifest(ros_id, expected_mid);
  calculate_ident(*roster, actual_mid);
  I(expected_mid == actual_mid);

  // const'ify the objects, to save them and pass them out
  cr.first = roster;
  cr.second = marking;
  roster_cache.insert_clean(ros_id, cr);
}


void
database::drop(hexenc<id> const & ident,
               string const & table)
{
  string drop = "DELETE FROM " + table + " WHERE id = ?";
  execute(query(drop) % blob(decode_hexenc(ident())));
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
    || file_or_manifest_base_exists(id.inner(), "files");
}

bool
database::roster_version_exists(revision_id const & id)
{
  return delta_exists(id.inner(), "roster_deltas")
    || roster_base_available(id);
}

bool
database::revision_exists(revision_id const & id)
{
  results res;
  query q("SELECT id FROM revisions WHERE id = ?");
  fetch(res, one_col, any_rows, q % blob(decode_hexenc(id.inner()())));
  I(res.size() <= 1);
  return res.size() == 1;
}

void
database::get_file_ids(set<file_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("files", tmp);
  get_ids("file_deltas", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_revision_ids(set<revision_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("revisions", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_roster_ids(set<revision_id> & ids)
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("rosters", tmp);
  add_decoration_to_container(tmp, ids);
  get_ids("roster_deltas", tmp);
  add_decoration_to_container(tmp, ids);
}

void
database::get_file_version(file_id const & id,
                           file_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "files", "file_deltas");
  dat = file_data(tmp);
}

void
database::get_manifest_version(manifest_id const & id,
                               manifest_data & dat)
{
  data tmp;
  get_version(id.inner(), tmp, "manifests", "manifest_deltas");
  dat = manifest_data(tmp);
}

void
database::put_file(file_id const & id,
                   file_data const & dat)
{
  if (file_version_exists(id))
    L(FL("file version '%s' already exists in db") % id);
  else
    schedule_delayed_file(id, dat);
}

void
database::put_file_version(file_id const & old_id,
                           file_id const & new_id,
                           file_delta const & del)
{
  I(!(old_id == new_id));
  file_data old_data, new_data;
  file_delta reverse_delta;

  if (!file_version_exists(old_id))
    {
      W(F("file preimage '%s' missing in db") % old_id);
      W(F("dropping delta '%s' -> '%s'") % old_id % new_id);
      return;
    }
  
  get_file_version(old_id, old_data);
  {
    data tmp;
    patch(old_data.inner(), del.inner(), tmp);
    new_data = file_data(tmp);
  }

  {
    string tmp;
    invert_xdelta(old_data.inner()(), del.inner()(), tmp);
    reverse_delta = file_delta(tmp);
    data old_tmp;
    patch(new_data.inner(), reverse_delta.inner(), old_tmp);
    // We already have the real old data, so compare the
    // reconstruction to that directly instead of hashing
    // the reconstruction and comparing hashes.
    I(old_tmp == old_data.inner());
  }
  
  transaction_guard guard(*this);  
  if (file_or_manifest_base_exists(old_id.inner(), "files"))
    {
      // descendent of a head version replaces the head, therefore old head
      // must be disposed of
      drop_or_cancel_file(old_id);
    }
  if (!file_or_manifest_base_exists(new_id.inner(), "files"))
    {
      schedule_delayed_file(new_id, new_data);
      drop(new_id.inner(), "file_deltas");
    }
    
  if (!delta_exists(old_id.inner(), new_id.inner(), "file_deltas"))
    {
      put_file_delta(old_id, new_id, reverse_delta);
      guard.commit();
    }
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
           "WHERE base = ? AND id = ?");
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
database::get_revision_ancestry(rev_ancestry_map & graph)
{
  // share some storage
  id::symtab id_syms;
  
  results res;
  graph.clear();
  fetch(res, 2, any_rows,
        query("SELECT parent,child FROM revision_ancestry"));
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(make_pair(revision_id(encode_hexenc(res[i][0])),
                                revision_id(encode_hexenc(res[i][1]))));
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
        % blob(decode_hexenc(id.inner()())));
  for (size_t i = 0; i < res.size(); ++i)
    parents.insert(revision_id(encode_hexenc(res[i][0])));
}

void
database::get_revision_children(revision_id const & id,
                                set<revision_id> & children)
{
  results res;
  children.clear();
  fetch(res, one_col, any_rows,
        query("SELECT child FROM revision_ancestry WHERE parent = ?")
        % blob(decode_hexenc(id.inner()())));
  for (size_t i = 0; i < res.size(); ++i)
    children.insert(revision_id(encode_hexenc(res[i][0])));
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
        % blob(decode_hexenc(id.inner()())));

  gzip<data> gzdata(res[0][0]);
  data rdat;
  decode_gzip(gzdata,rdat);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(revision_data(rdat), tmp);
    I(id == tmp);
  }

  dat = revision_data(rdat);
}

typedef std::map<revision_id, rev_height> height_map;
static height_map height_cache;

void
database::get_rev_height(revision_id const & id,
                         rev_height & height)
{
  if (null_id(id))
    {
      height = rev_height::root_height();
      return;
    }

  height_map::const_iterator i = height_cache.find(id);
  if (i == height_cache.end())
    {
      results res;
      fetch(res, one_col, one_row,
            query("SELECT height FROM heights WHERE revision = ?")
            % blob(decode_hexenc(id.inner()())));

      I(res.size() == 1);

      height = rev_height(res[0][0]);
      height_cache.insert(make_pair(id, height));
    }
  else
    {
      height = i->second;
    }

  I(height.valid());
}

void
database::put_rev_height(revision_id const & id,
                         rev_height const & height)
{
  I(!null_id(id));
  I(revision_exists(id));
  I(height.valid());
  
  height_cache.erase(id);
  
  execute(query("INSERT INTO heights VALUES(?, ?)")
          % blob(decode_hexenc(id.inner()()))
          % blob(height()));
}

bool
database::has_rev_height(rev_height const & height)
{
  results res;
  fetch(res, one_col, any_rows,
        query("SELECT height FROM heights WHERE height = ?")
        % blob(height()));
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
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
        for (map<file_path, pair<file_id, file_id> >::const_iterator
               j = edge_changes(i).deltas_applied.begin();
             j != edge_changes(i).deltas_applied.end(); ++j)
          {
            if (file_or_manifest_base_exists(delta_entry_src(j).inner(), "files") &&
                file_version_exists(delta_entry_dst(j)))
              {
                file_data old_data;
                file_data new_data;
                get_file_version(delta_entry_src(j), old_data);
                get_file_version(delta_entry_dst(j), new_data);
                delta delt;
                diff(old_data.inner(), new_data.inner(), delt);
                file_delta del(delt);
                drop_or_cancel_file(delta_entry_dst(j));
                drop(delta_entry_dst(j).inner(), "file_deltas");
                put_file_version(delta_entry_src(j), delta_entry_dst(j), del);
              }
          }
      }
  }
  guard.commit();
}


bool
database::put_revision(revision_id const & new_id,
                       revision_t const & rev)
{
  MM(new_id);
  MM(rev);

  I(!null_id(new_id));

  if (revision_exists(new_id))
    {
      L(FL("revision '%s' already exists in db") % new_id);
      return false;
    }

  I(rev.made_for == made_for_database);
  rev.check_sane();

  // Phase 1: confirm the revision makes sense, and we the required files
  // actually exist
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); ++i)
    {
      if (!edge_old_revision(i).inner()().empty()
          && !revision_exists(edge_old_revision(i)))
        {
          W(F("missing prerequisite revision '%s'") % edge_old_revision(i));
          W(F("dropping revision '%s'") % new_id);
          return false;
        }

      for (map<file_path, file_id>::const_iterator a
             = edge_changes(i).files_added.begin();
           a != edge_changes(i).files_added.end(); ++a)
        {
          if (! file_version_exists(a->second))
            {
              W(F("missing prerequisite file '%s'") % a->second);
              W(F("dropping revision '%s'") % new_id);
              return false;
            }
        }

      for (map<file_path, pair<file_id, file_id> >::const_iterator d
             = edge_changes(i).deltas_applied.begin();
           d != edge_changes(i).deltas_applied.end(); ++d)
        {
          I(!delta_entry_src(d).inner()().empty());
          I(!delta_entry_dst(d).inner()().empty());

          if (! file_version_exists(delta_entry_src(d)))
            {
              W(F("missing prerequisite file pre-delta '%s'")
                % delta_entry_src(d));
              W(F("dropping revision '%s'") % new_id);
              return false;
            }

          if (! file_version_exists(delta_entry_dst(d)))
            {
              W(F("missing prerequisite file post-delta '%s'")
                % delta_entry_dst(d));
              W(F("dropping revision '%s'") % new_id);
              return false;
            }
        }
    }

  transaction_guard guard(*this);

  // Phase 2: Write the revision data (inside a transaction)

  revision_data d;
  write_revision(rev, d);
  gzip<data> d_packed;
  encode_gzip(d.inner(), d_packed);
  execute(query("INSERT INTO revisions VALUES(?, ?)")
          % blob(decode_hexenc(new_id.inner()()))
          % blob(d_packed()));

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      execute(query("INSERT INTO revision_ancestry VALUES(?, ?)")
              % blob(decode_hexenc(edge_old_revision(e).inner()()))
              % blob(decode_hexenc(new_id.inner()())));
    }

  // Phase 3: Construct and write the roster (which also checks the manifest
  // id as it goes), but only if the roster does not already exist in the db
  // (i.e. because it was left over by a kill_rev_locally)
  // FIXME: there is no knowledge yet on speed implications for commands which
  // put a lot of revisions in a row (i.e. tailor or cvs_import)!

  if (!roster_version_exists(new_id))
    {
      put_roster_for_revision(new_id, rev);
    }
  else
    {
      L(FL("roster for revision '%s' already exists in db") % new_id);
    }

  // Phase 4: rewrite any files that need deltas added

  deltify_revision(new_id);

  // Phase 5: determine the revision height

  put_height_for_revision(new_id, rev);

  // Finally, commit.

  guard.commit();
  return true;
}

void
database::put_height_for_revision(revision_id const & new_id,
                                  revision_t const & rev)
{
  I(!null_id(new_id));
  
  rev_height highest_parent;
  // we always branch off the highest parent ...
  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      rev_height parent; MM(parent);
      get_rev_height(edge_old_revision(e), parent);
      if (parent > highest_parent)
      {
        highest_parent = parent;
      }
    }
    
  // ... then find the first unused child
  u32 childnr(0);
  rev_height candidate; MM(candidate);
  while(true)
    {
      candidate = highest_parent.child_height(childnr);
      if (!has_rev_height(candidate))
        {
          break;
        }
      I(childnr < std::numeric_limits<u32>::max());
      ++childnr;
    }
  put_rev_height(new_id, candidate);
}

void
database::put_roster_for_revision(revision_id const & new_id,
                                  revision_t const & rev)
{
  // Construct, the roster, sanity-check the manifest id, and then write it
  // to the db
  shared_ptr<roster_t> ros_writeable(new roster_t); MM(*ros_writeable);
  shared_ptr<marking_map> mm_writeable(new marking_map); MM(*mm_writeable);
  manifest_id roster_manifest_id;
  MM(roster_manifest_id);
  make_roster_for_revision(rev, new_id, *ros_writeable, *mm_writeable, *__app);
  calculate_ident(*ros_writeable, roster_manifest_id);
  I(rev.new_manifest == roster_manifest_id);
  // const'ify the objects, suitable for caching etc.
  roster_t_cp ros = ros_writeable;
  marking_map_cp mm = mm_writeable;
  put_roster(new_id, ros, mm);
}

bool
database::put_revision(revision_id const & new_id,
                       revision_data const & dat)
{
  revision_t rev;
  read_revision(dat, rev);
  return put_revision(new_id, rev);
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

void
database::delete_existing_rosters()
{
  execute(query("DELETE FROM rosters"));
  execute(query("DELETE FROM roster_deltas"));
  execute(query("DELETE FROM next_roster_node_number"));
}

void
database::delete_existing_heights()
{
  execute(query("DELETE FROM heights"));
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
  I(children.empty());


  L(FL("Killing revision %s locally") % rid);

  // Kill the certs, ancestry, and revision.
  execute(query("DELETE from revision_certs WHERE id = ?")
          % blob(decode_hexenc(rid.inner()())));
  cert_stamper.note_change();

  execute(query("DELETE from revision_ancestry WHERE child = ?")
          % blob(decode_hexenc(rid.inner()())));

  execute(query("DELETE from heights WHERE revision = ?")
          % blob(decode_hexenc(rid.inner()())));

  execute(query("DELETE from revisions WHERE id = ?")
          % blob(decode_hexenc(rid.inner()())));

  guard.commit();
}

/// Deletes all certs referring to a particular branch.
void
database::delete_branch_named(cert_value const & branch)
{
  L(FL("Deleting all references to branch %s") % branch);
  execute(query("DELETE FROM revision_certs WHERE name='branch' AND value =?")
          % blob(branch()));
  cert_stamper.note_change();
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
  cert_stamper.note_change();
}

// crypto key management

void
database::get_key_ids(vector<rsa_keypair_id> & pubkeys)
{
  pubkeys.clear();
  results res;

  fetch(res, one_col, any_rows, query("SELECT id FROM public_keys"));

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(rsa_keypair_id(res[i][0]));
}

void
database::get_key_ids(globish const & pattern,
                      vector<rsa_keypair_id> & pubkeys)
{
  pubkeys.clear();
  results res;

  fetch(res, one_col, any_rows, query("SELECT id FROM public_keys"));

  for (size_t i = 0; i < res.size(); ++i)
    if (pattern.matches(res[i][0]))
      pubkeys.push_back(rsa_keypair_id(res[i][0]));
}

void
database::get_keys(string const & table, vector<rsa_keypair_id> & keys)
{
  keys.clear();
  results res;
  fetch(res, one_col, any_rows, query("SELECT id FROM " + table));
  for (size_t i = 0; i < res.size(); ++i)
    keys.push_back(rsa_keypair_id(res[i][0]));
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
  id = rsa_keypair_id(res[0][0]);
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

bool
database::put_key(rsa_keypair_id const & pub_id,
                  base64<rsa_pub_key> const & pub_encoded)
{
  if (public_key_exists(pub_id))
    {
      base64<rsa_pub_key> tmp;
      get_key(pub_id, tmp);
      if (!keys_match(pub_id, tmp, pub_id, pub_encoded))
        W(F("key '%s' is not equal to key '%s' in database") % pub_id % pub_id);
      L(FL("skipping existing public key %s") % pub_id);
      return false;
    }

  L(FL("putting public key %s") % pub_id);

  hexenc<id> thash;
  key_hash_code(pub_id, pub_encoded, thash);
  I(!public_key_exists(thash));

  rsa_pub_key pub_key;
  decode_base64(pub_encoded, pub_key);
  execute(query("INSERT INTO public_keys VALUES(?, ?, ?)")
          % text(thash())
          % text(pub_id())
          % blob(pub_key()));

  return true;
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
    % text(decode_hexenc(t.ident()))
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
          % blob(decode_hexenc(thash()))
          % blob(decode_hexenc(t.ident()))
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
      t = cert(hexenc<id>(encode_hexenc(res[i][0])),
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

  fetch(res, 5, any_rows, q % blob(decode_hexenc(ident())));
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
        q % blob(decode_hexenc(ident()))
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
        q % blob(decode_hexenc(ident()))
          % text(name())
          % blob(binvalue()));
  results_to_certs(res, certs);
}



bool
database::revision_cert_exists(revision<cert> const & cert)
{
  return cert_exists(cert.inner(), "revision_certs");
}

bool
database::put_revision_cert(revision<cert> const & cert)
{
  if (revision_cert_exists(cert))
    {
      L(FL("revision cert on '%s' already exists in db")
        % cert.inner().ident);
      return false;
    }

  if (!revision_exists(revision_id(cert.inner().ident)))
    {
      W(F("cert revision '%s' does not exist in db")
        % cert.inner().ident);
      W(F("dropping cert"));
      return false;
    }

  put_cert(cert.inner(), "revision_certs");
  cert_stamper.note_change();
  return true;
}

outdated_indicator
database::get_revision_cert_nobranch_index(vector< pair<hexenc<id>,
                                           pair<revision_id, rsa_keypair_id> > > & idx)
{
  // share some storage
  id::symtab id_syms;
  
  results res;
  fetch(res, 3, any_rows,
        query("SELECT hash, id, keypair "
        "FROM 'revision_certs' WHERE name != 'branch'"));

  idx.clear();
  idx.reserve(res.size());
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      idx.push_back(make_pair(hexenc<id>(encode_hexenc((*i)[0])),
                              make_pair(revision_id(encode_hexenc((*i)[1])),
                                        rsa_keypair_id((*i)[2]))));
    }
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(cert_name const & name,
                            vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             cert_name const & name,
                             base64<cert_value> const & val,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, val, certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
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
    revisions.insert(revision_id(encode_hexenc((*i)[0])));
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(cert_name const & name,
                             base64<cert_value> const & val,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, val, certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & id,
                             vector< revision<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), certs, "revision_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
  return cert_stamper.get_indicator();
}

outdated_indicator
database::get_revision_certs(revision_id const & ident,
                             vector< hexenc<id> > & ts)
{
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows,
        query("SELECT hash "
        "FROM revision_certs "
        "WHERE id = ?")
        % blob(decode_hexenc(ident.inner()())));
  ts.clear();
  for (size_t i = 0; i < res.size(); ++i)
    ts.push_back(hexenc<id>(res[i][0]));
  return cert_stamper.get_indicator();
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
        % blob(decode_hexenc(hash())));
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
        % blob(decode_hexenc(hash())));
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
  add_decoration_to_container(certs, ts);
}


void
database::get_manifest_certs(cert_name const & name,
                            vector< manifest<cert> > & ts)
{
  vector<cert> certs;
  get_certs(name, certs, "manifest_certs");
  ts.clear();
  add_decoration_to_container(certs, ts);
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
                                 cert_name & name,
                                 string & prefix,
                                 string & suffix)
{
  prefix = suffix = "*";
  switch (ty)
    {
    case selectors::sel_author:
      prefix = suffix = "";
      name = author_cert_name;
      break;
    case selectors::sel_branch:
      prefix = suffix = "";
      name = branch_cert_name;
      break;
    case selectors::sel_head:
      prefix = suffix = "";
      name = branch_cert_name;
      break;
    case selectors::sel_date:
    case selectors::sel_later:
    case selectors::sel_earlier:
      name = date_cert_name;
      break;
    case selectors::sel_tag:
      prefix = suffix = "";
      name = tag_cert_name;
      break;
    case selectors::sel_ident:
    case selectors::sel_cert:
    case selectors::sel_unknown:
    case selectors::sel_parent:
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
  // selects a set of IDs from the revision_certs table which match the
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
          else if (i->first == selectors::sel_parent)
            {
              lim.sql_cmd += "SELECT parent AS id FROM revision_ancestry WHERE child GLOB ?";
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
              lim % text(author_cert_name()) % text(tag_cert_name()) % text(branch_cert_name());
              lim.sql_cmd += " AND CAST(value AS TEXT) glob ?";
              lim % text(i->second + "*");
            }
          else if (i->first == selectors::sel_head)
            {
              // get branch names
              set<branch_name> branch_names;
              if (i->second.size() == 0)
                {
                  __app->require_workspace("the empty head selector h: refers to the head of the current branch");
                  branch_names.insert(__app->opts.branchname);
                }
              else
                {
                  __app->get_project().get_branch_list(globish(i->second), branch_names);
                }

                L(FL("found %d matching branches") % branch_names.size());

              // for each branch name, get the branch heads
              set<revision_id> heads;
              for (set<branch_name>::const_iterator bn = branch_names.begin();
                   bn != branch_names.end(); bn++)
                {
                  set<revision_id> branch_heads;
                  __app->get_project().get_branch_heads(*bn, branch_heads);
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
              cert_name certname;
              string prefix;
              string suffix;
              selector_to_certname(i->first, certname, prefix, suffix);
              L(FL("processing selector type %d with i->second '%s'") % ty % i->second);
              if ((i->first == selectors::sel_branch) && (i->second.size() == 0))
                {
                  __app->require_workspace("the empty branch selector b: refers to the current branch");
                  lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=? AND CAST(value AS TEXT) glob ?";
                  lim % text(branch_cert_name()) % text(__app->opts.branchname());
                  L(FL("limiting to current branch '%s'") % __app->opts.branchname);
                }
              else
                {
                  lim.sql_cmd += "SELECT id FROM revision_certs WHERE name=? AND ";
                  lim % text(certname());
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

  if (ty == selectors::sel_ident || ty == selectors::sel_parent)
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
          lim % text(author_cert_name()) % text(tag_cert_name()) % text(branch_cert_name());
        }
      else
        {
          cert_name certname;
          selector_to_certname(ty, certname, prefix, suffix);
          lim.sql_cmd += " (name=?)";
          lim % text(certname());
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
database::get_epochs(map<branch_name, epoch_data> & epochs)
{
  epochs.clear();
  results res;
  fetch(res, 2, any_rows, query("SELECT branch, epoch FROM branch_epochs"));
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      branch_name decoded(idx(*i, 0));
      I(epochs.find(decoded) == epochs.end());
      epochs.insert(make_pair(decoded, epoch_data(idx(*i, 1))));
    }
}

void
database::get_epoch(epoch_id const & eid,
                    branch_name & branch, epoch_data & epo)
{
  I(epoch_exists(eid));
  results res;
  fetch(res, 2, any_rows,
        query("SELECT branch, epoch FROM branch_epochs"
        " WHERE hash = ?")
        % text(eid.inner()()));
  I(res.size() == 1);
  branch = branch_name(idx(idx(res, 0), 0));
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
database::set_epoch(branch_name const & branch, epoch_data const & epo)
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
database::clear_epoch(branch_name const & branch)
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

outdated_indicator
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
    return cert_stamper.get_indicator();
}

outdated_indicator
database::get_branches(globish const & glob,
                       vector<string> & names)
{
    results res;
    query q("SELECT DISTINCT value FROM revision_certs WHERE name = ?");
    string cert_name = "branch";
    fetch(res, one_col, any_rows, q % text(cert_name));
    for (size_t i = 0; i < res.size(); ++i)
      {
        if (glob.matches(res[i][0]))
          names.push_back(res[i][0]);
      }
    return cert_stamper.get_indicator();
}

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster)
{
  marking_map mm;
  get_roster(rev_id, roster, mm);
}

void
database::get_roster(revision_id const & rev_id,
                     roster_t & roster,
                     marking_map & marking)
{
  if (rev_id.inner()().empty())
    {
      roster = roster_t();
      marking = marking_map();
      return;
    }

  cached_roster cr;
  get_roster(rev_id, cr);
  roster = *cr.first;
  marking = *cr.second;
}

void
database::get_roster(revision_id const & rev_id,
                     cached_roster & cr)
{
  get_roster_version(rev_id, cr);
  I(cr.first);
  I(cr.second);
}

void
database::put_roster(revision_id const & rev_id,
                     roster_t_cp const & roster,
                     marking_map_cp const & marking)
{
  I(roster);
  I(marking);
  MM(rev_id);

  transaction_guard guard(*this);

  // Our task is to add this roster, and deltify all the incoming edges (if
  // they aren't already).

  roster_cache.insert_dirty(rev_id, make_pair(roster, marking));

  set<revision_id> parents;
  get_revision_parents(rev_id, parents);

  // Now do what deltify would do if we bothered
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
    {
      if (null_id(*i))
        continue;
      revision_id old_rev = *i;
      if (roster_base_stored(old_rev))
        {
          cached_roster cr;
          get_roster_version(old_rev, cr);
          roster_delta reverse_delta;
          delta_rosters(*roster, *marking, *(cr.first), *(cr.second), reverse_delta);
          if (roster_cache.exists(old_rev))
            roster_cache.mark_clean(old_rev);
          drop(old_rev.inner(), "rosters");
          put_roster_delta(old_rev, rev_id, reverse_delta);
        }
    }
  guard.commit();
}

// for get_uncommon_ancestors
struct rev_height_graph : rev_graph
{
  typedef hashmap::hash_map<revision_id, set<revision_id> > parent_map;
  typedef hashmap::hash_map<revision_id, rev_height> height_map;
  static parent_map parent_cache;
  static height_map height_cache;
  rev_height_graph(database & db) : db(db) {}
  virtual void get_parents(revision_id const & rev, set<revision_id> & parents) const
  {
    parent_map::iterator i = parent_cache.find(rev);
    if (i == parent_cache.end())
      {
        db.get_revision_parents(rev, parents);
        parent_cache.insert(make_pair(rev, parents));
      }
    else
      {
        parents = i->second;
      }
  }
  virtual void get_children(revision_id const & rev, set<revision_id> & parents) const
  {
    // not required
    I(false);
  }
  virtual void get_height(revision_id const & rev, rev_height & h) const
  {
    height_map::iterator i = height_cache.find(rev);
    if (i == height_cache.end())
      {
        db.get_rev_height(rev, h);
        height_cache.insert(make_pair(rev, h));
      }
    else
      {
        h = i->second;
      }
  }
  
  database & db;
};
rev_height_graph::parent_map rev_height_graph::parent_cache;
rev_height_graph::height_map rev_height_graph::height_cache;

void
database::get_uncommon_ancestors(revision_id const & a,
                                 revision_id const & b,
                                 set<revision_id> & a_uncommon_ancs,
                                 set<revision_id> & b_uncommon_ancs)
{
  
  rev_height_graph graph(*this);
  ::get_uncommon_ancestors(a, b, graph, a_uncommon_ancs, b_uncommon_ancs);
}


u64
database::next_id_from_table(string const & table)
{
  transaction_guard guard(*this);
  results res;

  // We implement this as a fixed db var.

  fetch(res, one_col, any_rows,
        query(string("SELECT node FROM ") + table));

  u64 n;
  if (res.empty())
    {
      n = 1;
      execute (query(string("INSERT INTO ") + table + " VALUES(?)")
               % text(lexical_cast<string>(n)));
    }
  else
    {
      I(res.size() == 1);
      n = lexical_cast<u64>(res[0][0]);
      ++n;
      execute(query(string("UPDATE ") + table + " SET node = ?")
              % text(lexical_cast<string>(n)));

    }
  guard.commit();
  return n;
}

node_id
database::next_node_id()
{
  return static_cast<node_id>(next_id_from_table("next_roster_node_number"));
}

void
database::check_filename()
{
  N(!filename.empty(), F("no database specified"));
}


void
database::check_db_exists()
{
  switch (get_path_status(filename))
    {
    case path::nonexistent:
      N(false, F("database %s does not exist") % filename);
      break;
    case path::file:
      return;
    case path::directory:
      {
        system_path database_option;
        branch_name branch_option;
        rsa_keypair_id key_option;
        system_path keydir_option;
        if (workspace::get_ws_options_from_path(
                    filename,
                    database_option,
                    branch_option,
                    key_option,
                    keydir_option))
          {
            N(database_option.empty(),
                              F("You gave a database option of: \n"
                                "%s\n"
                                "That is actually a workspace.  Did you mean: \n"
                                "%s") % filename % database_option );
          }
        N(false, F("%s is a directory, not a database") % filename);
      }
      break;
    }
}

void
database::check_db_nonexistent()
{
  require_path_is_nonexistent(filename,
                              F("database %s already exists")
                              % filename);

  system_path journal(filename.as_internal() + "-journal");
  require_path_is_nonexistent(journal,
                              F("existing (possibly stale) journal file '%s' "
                                "has same stem as new database '%s'\n"
                                "cancelling database creation")
                              % journal % filename);

}

bool
database::database_specified()
{
  return !filename.empty();
}


void
database::open()
{
  I(!__sql);

  if (sqlite3_open(filename.as_external().c_str(), &__sql) == SQLITE_NOMEM)
    throw std::bad_alloc();

  I(__sql);
  assert_sqlite3_ok(__sql);
}

void
database::close()
{
  I(__sql);

  sqlite3_close(__sql);
  __sql = 0;

  I(!__sql);
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
