// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <fstream>
#include <iterator>
#include <list>
#include <set>
#include <sstream>
#include <vector>

#include <stdarg.h>

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <sqlite3.h>

#include "app_state.hh"
#include "cert.hh"
#include "cleanup.hh"
#include "constants.hh"
#include "database.hh"
#include "keys.hh"
#include "sanity.hh"
#include "schema_migration.hh"
#include "cert.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "xdelta.hh"

// defined in schema.sql, converted to header:
#include "schema.h"

// defined in views.sql, converted to header:
#include "views.h"

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// see file schema.sql for the text of the schema.

using boost::shared_ptr;
using boost::lexical_cast;
using namespace std;

int const one_row = 1;
int const one_col = 1;
int const any_rows = -1;
int const any_cols = -1;

extern "C" {
// some wrappers to ease migration
  int sqlite3_exec_printf(sqlite3*,const char *sqlFormat,sqlite3_callback,
                          void *,char **errmsg,...);
  int sqlite3_exec_vprintf(sqlite3*,const char *sqlFormat,sqlite3_callback,
                           void *,char **errmsg,va_list ap);
  const char *sqlite3_value_text_s(sqlite3_value *v);
  const char *sqlite3_column_text_s(sqlite3_stmt*, int iCol);
}

int sqlite3_exec_printf(sqlite3 * db,
                        char const * sqlFormat,
                        sqlite3_callback cb,
                        void * user_data,
                        char ** errmsg,
                        ...)
{ 
  va_list ap;
  va_start(ap, errmsg);
  int result = sqlite3_exec_vprintf(db, sqlFormat, cb,
                                    user_data, errmsg, ap);
  va_end(ap);
  return result;
}

int sqlite3_exec_vprintf(sqlite3 * db,
                         char const * sqlFormat,
                         sqlite3_callback cb,
                         void * user_data,
                         char ** errmsg,
                         va_list ap)
{ 
  char * formatted = sqlite3_vmprintf(sqlFormat, ap);
  int result = sqlite3_exec(db, formatted, cb, 
                            user_data, errmsg);
  sqlite3_free(formatted);
  return result;
}

database::database(fs::path const & fn) :
  filename(fn),
  // nb. update this if you change the schema. unfortunately we are not
  // using self-digesting schemas due to comment irregularities and
  // non-alphabetic ordering of tables in sql source files. we could create
  // a temporary db, write our intended schema into it, and read it back,
  // but this seems like it would be too rude. possibly revisit this issue.
  schema("c1e86588e11ad07fa53e5d294edc043ce1d4005a"),
  __sql(NULL),
  transaction_level(0)

{}

void 
database::check_schema()
{
  string db_schema_id;  
  calculate_schema_id (__sql, db_schema_id);
  N (schema == db_schema_id,
     F("database schemas do not match: "
       "wanted %s, got %s. try migrating database") 
     % schema % db_schema_id);
}

// sqlite3_value_text gives a const unsigned char * but most of the time
// we need a signed char
const char *
sqlite3_value_text_s(sqlite3_value *v)
{  
  return (const char *)(sqlite3_value_text(v));
}

const char *
sqlite3_column_text_s(sqlite3_stmt *stmt, int col)
{
  return (const char *)(sqlite3_column_text(stmt, col));
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
  decode_base64(base64<data>(string(sqlite3_value_text_s(args[0]))), decoded);
  sqlite3_result_blob(f, decoded().c_str(), decoded().size(), SQLITE_TRANSIENT);
}

void 
database::set_app(app_state * app)
{
  __app = app;
}

static void 
check_sqlite_format_version(fs::path const & filename)
{
  if (fs::exists(filename))
    {
      // sqlite 3 files begin with this constant string
      // (version 2 files begin with a different one)
      std::string version_string("SQLite format 3");

      std::ifstream file(filename.string().c_str());
      N(file, F("unable to probe database version in file %s") % filename.string());
      for (std::string::const_iterator i = version_string.begin();
           i != version_string.end(); ++i)
        {
          char c;
          file.get(c);
          N(c == *i, F("database is not an sqlite version 3 file, "
                       "try dump and reload"));            
        }
    }
}


static void
assert_sqlite3_ok(sqlite3 *s) 
{
  int errcode = sqlite3_errcode(s);

  if (errcode == SQLITE_OK) return;
  
  const char * errmsg = sqlite3_errmsg(s);

  ostringstream oss;
  oss << "sqlite errcode=" << errcode << " " << errmsg;
  
  throw oops(oss.str());
}

struct sqlite3 * 
database::sql(bool init)
{
  if (! __sql)
    {
      if (! init)
        {
          if (filename.string() == "")
            throw informative_failure(string("no database specified"));
          else if (! fs::exists(filename))
            throw informative_failure(string("database ") + filename.string() +
                                      string(" does not exist"));
        }
      N(filename.string() != "",
        F("need database name"));
      check_sqlite_format_version(filename);
      int error;
      error = sqlite3_open(filename.string().c_str(), &__sql);
      if (error)
        throw oops(string("could not open database: ") + filename.string() + 
                   (": " + string(sqlite3_errmsg(__sql))));
      if (init)
        {
          sqlite3_exec(__sql, schema_constant, NULL, NULL, NULL);
          assert_sqlite3_ok(__sql);
        }

      check_schema();
      install_functions(__app);
      install_views();
    }
  return __sql;
}

void 
database::initialize()
{
  if (__sql)
    throw oops("cannot initialize database while it is open");

  N(!fs::exists(filename),
    F("could not initialize database: %s: already exists") 
    % filename.string());

  fs::path journal = mkpath(filename.string() + "-journal");
  N(!fs::exists(journal),
    F("existing (possibly stale) journal file '%s' "
      "has same stem as new database '%s'")
    % journal.string() % filename.string());

  sqlite3 *s = sql(true);
  I(s != NULL);
}


struct 
dump_request
{
  dump_request() {};
  struct sqlite3 *sql;
  string table_name;
  ostream *out;
};

static int 
dump_row_cb(void *data, int n, char **vals, char **cols)
{
  dump_request *dump = reinterpret_cast<dump_request *>(data);
  I(dump != NULL);
  I(vals != NULL);
  I(dump->out != NULL);
  *(dump->out) << F("INSERT INTO %s VALUES(") % dump->table_name;
  for (int i = 0; i < n; ++i)
    {
      if (i != 0)
        *(dump->out) << ',';

      if (vals[i] == NULL)
        *(dump->out) << "NULL";
      else
        {
          *(dump->out) << "'";
          for (char *cp = vals[i]; *cp; ++cp)
            {
              if (*cp == '\'')
                *(dump->out) << "''";
              else
                *(dump->out) << *cp;
            }
          *(dump->out) << "'";
        }
    }
  *(dump->out) << ");\n";  
  return 0;
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
  if (string(vals[1]) == "table")
    {
      *(dump->out) << vals[2] << ";\n";
      dump->table_name = string(vals[0]);
      sqlite3_exec_printf(dump->sql, "SELECT * FROM '%q'", 
                         dump_row_cb, data, NULL, vals[0]);
    }
  return 0;
}

void 
database::dump(ostream & out)
{
  dump_request req;
  req.out = &out;
  req.sql = sql();
  out << "BEGIN TRANSACTION;\n";
  int res = sqlite3_exec(req.sql,
                        "SELECT name, type, sql FROM sqlite_master "
                        "WHERE type='table' AND sql NOT NULL "
                        "ORDER BY substr(type,2,1), name",
                        dump_table_cb, &req, NULL);
  I(res == SQLITE_OK);
  out << "COMMIT;\n";
}

void 
database::load(istream & in)
{
  char buf[constants::bufsz];
  string tmp;

  N(filename.string() != "",
    F("need database name"));
  int error = sqlite3_open(filename.string().c_str(), &__sql);
  if (error)
    throw oops(string("could not open database: ") + filename.string() + 
               (string(sqlite3_errmsg(__sql))));
  
  while(in)
    {
      in.read(buf, constants::bufsz);
      tmp.append(buf, in.gcount());
    }

  sqlite3_exec(__sql, tmp.c_str(), NULL, NULL, NULL);
  assert_sqlite3_ok(__sql);
}


void 
database::debug(string const & sql, ostream & out)
{
  results res;
  fetch(res, any_cols, any_rows, sql.c_str());
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

unsigned long 
database::get_statistic(string const & query)
{
  results res;
  fetch(res, 1, 1, query.c_str());
  return lexical_cast<unsigned long>(res[0][0]);
}

void 
database::info(ostream & out)
{
  string id;
  calculate_schema_id(sql(), id);
  out << "schema version  : " << id << endl;
  out << "full manifests  : " << get_statistic("SELECT COUNT(*) FROM manifests") << endl;
  out << "manifest deltas : " << get_statistic("SELECT COUNT(*) FROM manifest_deltas") << endl;
  out << "full files      : " << get_statistic("SELECT COUNT(*) FROM files") << endl;
  out << "file deltas     : " << get_statistic("SELECT COUNT(*) FROM file_deltas") << endl;
}

void 
database::version(ostream & out)
{
  string id;
  calculate_schema_id(sql(), id);
  out << "database schema version: " << id << endl;
}

void 
database::migrate()
{  
  N(filename.string() != "",
    F("need database name"));
  int error = sqlite3_open(filename.string().c_str(), &__sql);
  if (error)
    throw oops(string("could not open database: ") + filename.string() + 
               (": " + string(sqlite3_errmsg(__sql))));
  migrate_monotone_schema(__sql);
  sqlite3_close(__sql);
}

void 
database::rehash()
{
  transaction_guard guard(*this);
  ticker mcerts("mcerts", "m", 1);
  ticker fcerts("fcerts", "f", 1);
  ticker pubkeys("pubkeys", "+", 1);
  ticker privkeys("privkeys", "!", 1);
  
  {
    // rehash all mcerts
    results res;
    vector<cert> certs;
    fetch(res, 5, any_rows, 
          "SELECT id, name, value, keypair, signature "
          "FROM manifest_certs");
    results_to_certs(res, certs);
    execute("DELETE FROM manifest_certs");
    for(vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
      {
        put_cert(*i, "manifest_certs");
        ++mcerts;
      }
  }

  {
    // rehash all pubkeys
    results res;
    fetch(res, 2, any_rows, "SELECT id, keydata FROM public_keys");
    execute("DELETE FROM public_keys");
    for (size_t i = 0; i < res.size(); ++i)
      {
        hexenc<id> tmp;
        key_hash_code(rsa_keypair_id(res[i][0]), base64<rsa_pub_key>(res[i][1]), tmp);
        execute("INSERT INTO public_keys VALUES(?, ?, ?)", 
                tmp().c_str(), res[i][0].c_str(), res[i][1].c_str());
        ++pubkeys;
      }
  }

{
    // rehash all privkeys
    results res;
    fetch(res, 2, any_rows, "SELECT id, keydata FROM private_keys");
    execute("DELETE FROM private_keys");
    for (size_t i = 0; i < res.size(); ++i)
      {
        hexenc<id> tmp;
        key_hash_code(rsa_keypair_id(res[i][0]), base64< arc4<rsa_priv_key> >(res[i][1]), tmp);
        execute("INSERT INTO private_keys VALUES(?, ?, ?)", 
                tmp().c_str(), res[i][0].c_str(), res[i][1].c_str());
        ++privkeys;
      }
  }

  guard.commit();
}

void 
database::ensure_open()
{
  sqlite3 *s = sql();
  I(s != NULL);
}

database::~database() 
{
  L(F("statement cache statistics\n"));
  L(F("prepared %d statements\n") % statement_cache.size());

  for (map<string, statement>::const_iterator i = statement_cache.begin(); 
       i != statement_cache.end(); ++i)
    {
      L(F("%d executions of %s\n") % i->second.count % i->first);
      sqlite3_finalize(i->second.stmt);
    }

  if (__sql)
    {
      sqlite3_close(__sql);
      __sql = 0;
    }
}

void 
database::execute(char const * query, ...)
{
  results res;
  va_list args;
  va_start(args, query);
  fetch(res, 0, 0, query, args);
  va_end(args);
}

void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                char const * query, ...)
{
  va_list args;
  va_start(args, query);
  fetch(res, want_cols, want_rows, query, args);
  va_end(args);
}

void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                char const * query, 
                va_list args)
{
  int nrow;
  int ncol;
  int rescode;

  res.clear();
  res.resize(0);

  map<string, statement>::iterator i = statement_cache.find(query);
  if (i == statement_cache.end()) 
    {
      statement_cache.insert(make_pair(query, statement()));
      i = statement_cache.find(query);
      I(i != statement_cache.end());

      const char * tail;
      sqlite3_prepare(sql(), query, -1, &i->second.stmt, &tail);
      assert_sqlite3_ok(sql());
      L(F("prepared statement %s\n") % query);
      I(*tail == 0); // no support for multiple statements
    }

  string ctx = string("db query [") + string(query) + "]: ";

  ncol = sqlite3_column_count(i->second.stmt);

  if (want_cols != any_cols && ncol != want_cols)
    throw oops((F("%s wanted %d columns, got %s")
                % ctx % want_cols % ncol).str());

  // bind parameters for this execution

  int params = sqlite3_bind_parameter_count(i->second.stmt);

  L(F("binding %d parameters for %s\n") % params % query);

  for (int param = 1; param <= params; param++)
    {
      char *value = va_arg(args, char *);
      // nb: transient will not be good for inserts with large data blobs
      // however, it's no worse than the previous '%q' stuff in this regard
      L(F("binding %d with value '%s'\n") % param % value);
      sqlite3_bind_text(i->second.stmt, param, value, -1, SQLITE_TRANSIENT);
      assert_sqlite3_ok(sql());
    }

  // execute and process results

  nrow = 0;
  for (rescode = sqlite3_step(i->second.stmt); rescode == SQLITE_ROW; 
       rescode = sqlite3_step(i->second.stmt))
    {
      vector<string> row;
      for (int col = 0; col < ncol; col++) 
        {
          const char * value = sqlite3_column_text_s(i->second.stmt, col);
          if (!value) throw oops(ctx + "null result value");
          row.push_back(value);
          //L(F("row %d col %d value='%s'\n") % nrow % col % value);
        }
      res.push_back(row);
    }
  
  if (rescode != SQLITE_DONE)
    assert_sqlite3_ok(sql());

  sqlite3_reset(i->second.stmt);
  assert_sqlite3_ok(sql());

  nrow = res.size();

  i->second.count++;

  if (want_rows != any_rows && nrow != want_rows)
    throw oops((F("%s wanted %d rows, got %s") % ctx % want_rows % nrow).str());
}

// general application-level logic

void 
database::set_filename(fs::path const & file)
{
  if (__sql)
    {
      throw oops("cannot change filename to " + file.string() + " while db is open");
    }
  filename = file;
}

void 
database::begin_transaction() 
{
  if (transaction_level == 0)
    execute("BEGIN");
  transaction_level++;
}

void 
database::commit_transaction()
{
  if (transaction_level == 1)
    execute("COMMIT");
  transaction_level--;
}

void 
database::rollback_transaction()
{
  if (transaction_level == 1)
    execute("ROLLBACK");
  transaction_level--;
}


bool 
database::exists(hexenc<id> const & ident,
                      string const & table)
{
  results res;
  string query = "SELECT id FROM " + table + " WHERE id = ?";
  fetch(res, one_col, any_rows, query.c_str(), ident().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}


bool 
database::delta_exists(hexenc<id> const & ident,
                       string const & table)
{
  results res;
  string query = "SELECT id FROM " + table + " WHERE id = ?";
  fetch(res, one_col, any_rows, query.c_str(), ident().c_str());
  return res.size() > 0;
}

bool 
database::delta_exists(hexenc<id> const & ident,
                       hexenc<id> const & base,
                       string const & table)
{
  results res;
  string query = "SELECT id FROM " + table + " WHERE id = ? AND base = ?";
  fetch(res, one_col, any_rows, query.c_str(), 
        ident().c_str(), base().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}

int 
database::count(string const & table)
{
  results res;
  string query = "SELECT COUNT(*) FROM " + table;
  fetch(res, one_col, one_row, query.c_str());
  return lexical_cast<int>(res[0][0]);  
}

void 
database::get(hexenc<id> const & ident,
              base64< gzip<data> > & dat,
              string const & table)
{
  results res;
  string query = "SELECT data FROM " + table + " WHERE id = ?";
  fetch(res, one_col, one_row, query.c_str(), ident().c_str());

  // consistency check
  base64<gzip<data> > rdata(res[0][0]);
  hexenc<id> tid;
  calculate_ident(rdata, tid);
  I(tid == ident);

  dat = rdata;
}

void 
database::get_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    base64< gzip<delta> > & del,
                    string const & table)
{
  I(ident() != "");
  I(base() != "");
  results res;
  string query = "SELECT delta FROM " + table + " WHERE id = ? AND base = ?";
  fetch(res, one_col, one_row, query.c_str(), 
        ident().c_str(), base().c_str());
  del = res[0][0];
}

void 
database::put(hexenc<id> const & ident,
              base64< gzip<data> > const & dat,
              string const & table)
{
  // consistency check
  I(ident() != "");
  hexenc<id> tid;
  calculate_ident(dat, tid);
  I(tid == ident);
  
  string insert = "INSERT INTO " + table + " VALUES(?, ?)";
  execute(insert.c_str(), 
          ident().c_str(), dat().c_str());
}


void 
database::put_delta(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    base64<gzip<delta> > const & del,
                    string const & table)
{
  // nb: delta schema is (id, base, delta)
  I(ident() != "");
  I(base() != "");

  string insert = "INSERT INTO " + table + " VALUES(?, ?, ?)";
  
  execute(insert.c_str(), 
          ident().c_str(), base().c_str(), del().c_str());
}

void 
database::get_version(hexenc<id> const & ident,
                      base64< gzip<data> > & dat,
                      string const & data_table,
                      string const & delta_table)
{
  I(ident() != "");
  if (exists(ident, data_table))
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
      
      L(F("reconstructing %s in %s\n") % ident % delta_table);
      I(delta_exists(ident, delta_table));
      
      // nb: an edge map goes in the direction of the
      // delta, *not* the direction we discover things in,
      // i.e. each map is of the form [newid] -> [oldid]

      typedef map< hexenc<id>, hexenc<id> > edgemap;
      list< shared_ptr<edgemap> > paths;

      set< hexenc<id> > frontier, cycles;
      frontier.insert(ident);

      bool found_root = false;
      hexenc<id> root("");

      string delta_query = "SELECT base FROM " + delta_table + " WHERE id = ?";

      while (! found_root)
        {
          set< hexenc<id> > next_frontier;
          shared_ptr<edgemap> frontier_map(new edgemap());

          I(!frontier.empty());

          for (set< hexenc<id> >::const_iterator i = frontier.begin();
               i != frontier.end(); ++i)
            {
              if (exists(*i, data_table))
                {
                  root = *i;
                  found_root = true;
                  break;
                }
              else
                {
                  cycles.insert(*i);
                  results res;
                  
                  fetch(res, one_col, any_rows, 
                        delta_query.c_str(), (*i)().c_str());

                  for (size_t k = 0; k < res.size(); ++k)
                    {
                      hexenc<id> const nxt(res[k][0]);

                      if (cycles.find(nxt) != cycles.end())
                        throw oops("cycle in table '" + delta_table + "', at node " 
                                   + (*i)() + " <- " + nxt());

                      next_frontier.insert(nxt);

                      if (frontier_map->find(nxt) == frontier_map->end())
                        {
                          L(F("inserting edge: %s <- %s\n") % (*i) % nxt);
                          frontier_map->insert(make_pair(nxt, *i));
                        }
                      else
                        L(F("skipping merge edge %s <- %s\n") % (*i) % nxt);
                    }
                }
            }
          if (!found_root)
            {
              frontier = next_frontier;
              paths.push_front(frontier_map);
            }
        }

      // path built, now all we need to do is follow it back

      I(found_root);
      I(root() != "");
      base64< gzip<data> > begin_packed;
      data begin;      
      get(root, begin_packed, data_table);
      unpack(begin_packed, begin);
      hexenc<id> curr = root;

      boost::shared_ptr<delta_applicator> app = new_piecewise_applicator();
      app->begin(begin());
      
      for (list< shared_ptr<edgemap> >::const_iterator p = paths.begin();
           p != paths.end(); ++p)
        {
          shared_ptr<edgemap> i = *p;
          I(i->find(curr) != i->end());
          hexenc<id> const nxt = i->find(curr)->second;

          L(F("following delta %s -> %s\n") % curr % nxt);
          base64< gzip<delta> > del_packed;
          get_delta(nxt, curr, del_packed, delta_table);
          delta del;
          unpack(del_packed, del);
          apply_delta (app, del());
          app->next();
          curr = nxt;
        }

      string tmp;
      app->finish(tmp);
      data end(tmp);

      hexenc<id> final;
      calculate_ident(end, final);
      I(final == ident);
      pack(end, dat);
    }
}


void 
database::drop(hexenc<id> const & ident, 
               string const & table)
{
  string drop = "DELETE FROM " + table + " WHERE id = ?";
  execute(drop.c_str(), ident().c_str());
}

void 
database::put_version(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      base64< gzip<delta> > const & del,
                      string const & data_table,
                      string const & delta_table)
{

  base64< gzip<data> > old_data, new_data;
  base64< gzip<delta> > reverse_delta;
  
  get_version(old_id, old_data, data_table, delta_table);
  patch(old_data, del, new_data);
  diff(new_data, old_data, reverse_delta);
      
  transaction_guard guard(*this);
  if (exists(old_id, data_table))
    {
      // descendent of a head version replaces the head, therefore old head
      // must be disposed of
      drop(old_id, data_table);
    }
  put(new_id, new_data, data_table);
  put_delta(old_id, new_id, reverse_delta, delta_table);
  guard.commit();
}

void 
database::put_reverse_version(hexenc<id> const & new_id,
                              hexenc<id> const & old_id,
                              base64< gzip<delta> > const & reverse_del,
                              string const & data_table,
                              string const & delta_table)
{
  base64< gzip<data> > old_data, new_data;
  
  get_version(new_id, new_data, data_table, delta_table);
  patch(new_data, reverse_del, old_data);
  hexenc<id> check;
  calculate_ident(old_data, check);
  I(old_id == check);
      
  transaction_guard guard(*this);
  put_delta(old_id, new_id, reverse_del, delta_table);
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
database::manifest_version_exists(manifest_id const & id)
{
  return delta_exists(id.inner(), "manifest_deltas") 
    || exists(id.inner(), "manifests");
}

bool 
database::revision_exists(revision_id const & id)
{
  return exists(id.inner(), "revisions");
}


void 
database::get_file_version(file_id const & id,
                           file_data & dat)
{
  base64< gzip<data> > tmp;
  get_version(id.inner(), tmp, "files", "file_deltas");
  dat = tmp;
}

void 
database::get_manifest_version(manifest_id const & id,
                               manifest_data & dat)
{
  base64< gzip<data> > tmp;
  get_version(id.inner(), tmp, "manifests", "manifest_deltas");
  dat = tmp;
}

void 
database::get_manifest(manifest_id const & id,
                       manifest_map & mm)
{
  manifest_data mdat;
  get_manifest_version(id, mdat);
  read_manifest_map(mdat, mm);
}


void 
database::put_file(file_id const & id,
                   file_data const & dat)
{
  put(id.inner(), dat.inner(), "files");
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
database::put_file_reverse_version(file_id const & new_id,
                                   file_id const & old_id,                                 
                                   file_delta const & del)
{
  put_reverse_version(new_id.inner(), old_id.inner(), del.inner(), 
                      "files", "file_deltas");
}


void 
database::put_manifest(manifest_id const & id,
                       manifest_data const & dat)
{
  put(id.inner(), dat.inner(), "manifests");
}

void 
database::put_manifest_version(manifest_id const & old_id,
                               manifest_id const & new_id,
                               manifest_delta const & del)
{
  put_version(old_id.inner(), new_id.inner(), del.inner(), 
              "manifests", "manifest_deltas");
}

void 
database::put_manifest_reverse_version(manifest_id const & new_id,
                                       manifest_id const & old_id,                                 
                                       manifest_delta const & del)
{
  put_reverse_version(new_id.inner(), old_id.inner(), del.inner(), 
                      "manifests", "manifest_deltas");
}


void 
database::get_revision_ancestry(std::multimap<revision_id, revision_id> & graph)
{
  results res;
  graph.clear();
  fetch(res, 2, any_rows, 
        "SELECT parent,child FROM revision_ancestry");
  for (size_t i = 0; i < res.size(); ++i)
    graph.insert(std::make_pair(revision_id(res[i][0]),
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
        "SELECT parent FROM revision_ancestry WHERE child = ?",
        id.inner()().c_str());
  for (size_t i = 0; i < res.size(); ++i)
    parents.insert(revision_id(res[i][0]));
}

void 
database::get_revision_children(revision_id const & id,
                                set<revision_id> & children)
{
  I(!null_id(id));
  results res;
  children.clear();
  fetch(res, one_col, any_rows, 
        "SELECT child FROM revision_ancestry WHERE parent = ?",
        id.inner()().c_str());
  for (size_t i = 0; i < res.size(); ++i)
    children.insert(revision_id(res[i][0]));
}

void 
database::get_revision_manifest(revision_id const & rid,
                               manifest_id & mid)
{
  revision_set rev;
  get_revision(rid, rev);
  mid = rev.new_manifest;
}

void 
database::get_revision(revision_id const & id,
                       revision_set & rev)
{
  revision_data d;
  get_revision(id, d);
  read_revision_set(d, rev);
}

void 
database::get_revision(revision_id const & id,
                       revision_data & dat)
{
  I(!null_id(id));
  results res;
  fetch(res, one_col, one_row, 
        "SELECT data FROM revisions WHERE id = ?",
        id.inner()().c_str());

  dat = revision_data(res[0][0]);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(dat, tmp);
    I(id == tmp);
  }
}

void 
database::put_revision(revision_id const & new_id,
                       revision_set const & rev)
{

  I(!null_id(new_id));
  I(!revision_exists(new_id));
  revision_data d;

  write_revision_set(rev, d);
  revision_id tmp;
  calculate_ident(d, tmp);
  I(tmp == new_id);

  transaction_guard guard(*this);

  execute("INSERT INTO revisions VALUES(?, ?)", 
          new_id.inner()().c_str(), 
          d.inner()().c_str());

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      execute("INSERT INTO revision_ancestry VALUES(?, ?)", 
              edge_old_revision(e).inner()().c_str(),
              new_id.inner()().c_str());
    }

  guard.commit();
}

void 
database::put_revision(revision_id const & new_id,
                       revision_data const & dat)
{
  revision_set rev;
  read_revision_set(dat, rev);
  put_revision(new_id, rev);
}


void 
database::delete_existing_revs_and_certs()
{
  execute("DELETE FROM revisions");
  execute("DELETE FROM revision_ancestry");
  execute("DELETE FROM revision_certs");
}


// crypto key management

void 
database::get_key_ids(string const & pattern,
                      vector<rsa_keypair_id> & pubkeys,
                      vector<rsa_keypair_id> & privkeys)
{
  pubkeys.clear();
  privkeys.clear();
  results res;

  if (pattern != "")
    fetch(res, one_col, any_rows, 
          "SELECT id FROM public_keys WHERE id GLOB ?",
          pattern.c_str());
  else
    fetch(res, one_col, any_rows, 
          "SELECT id FROM public_keys");

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(res[i][0]);

  if (pattern != "")
    fetch(res, one_col, any_rows, 
          "SELECT id FROM private_keys WHERE id GLOB ?",
          pattern.c_str());
  else
    fetch(res, one_col, any_rows, 
          "SELECT id FROM private_keys");

  for (size_t i = 0; i < res.size(); ++i)
    privkeys.push_back(res[i][0]);
}

void 
database::get_private_keys(vector<rsa_keypair_id> & privkeys)
{
  privkeys.clear();
  results res;
  fetch(res, one_col, any_rows,  "SELECT id FROM private_keys");
  for (size_t i = 0; i < res.size(); ++i)
    privkeys.push_back(res[i][0]);
}

bool 
database::public_key_exists(hexenc<id> const & hash)
{
  results res;
  fetch(res, one_col, any_rows, 
        "SELECT id FROM public_keys WHERE hash = ?",
        hash().c_str());
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
        "SELECT id FROM public_keys WHERE id = ?",
        id().c_str());
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1) 
    return true;
  return false;
}

bool 
database::private_key_exists(rsa_keypair_id const & id)
{
  results res;
  fetch(res, one_col, any_rows,
        "SELECT id FROM private_keys WHERE id = ?",
        id().c_str());
  I((res.size() == 1) || (res.size() == 0));
  if (res.size() == 1)
    return true;
  return false;
}

bool 
database::key_exists(rsa_keypair_id const & id)
{
  return public_key_exists(id) || private_key_exists(id);
}

void 
database::get_pubkey(hexenc<id> const & hash, 
                     rsa_keypair_id & id,
                     base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, 2, one_row, 
        "SELECT id, keydata FROM public_keys WHERE hash = ?", 
        hash().c_str());
  id = res[0][0];
  pub_encoded = res[0][1];
}

void 
database::get_key(rsa_keypair_id const & pub_id, 
                  base64<rsa_pub_key> & pub_encoded)
{
  results res;
  fetch(res, one_col, one_row, 
        "SELECT keydata FROM public_keys WHERE id = ?", 
        pub_id().c_str());
  pub_encoded = res[0][0];
}

void 
database::get_key(rsa_keypair_id const & priv_id, 
                  base64< arc4<rsa_priv_key> > & priv_encoded)
{
  results res;
  fetch(res, one_col, one_col, 
        "SELECT keydata FROM private_keys WHERE id = ?", 
        priv_id().c_str());
  priv_encoded = res[0][0];
}


void 
database::put_key(rsa_keypair_id const & pub_id, 
                  base64<rsa_pub_key> const & pub_encoded)
{
  hexenc<id> thash;
  key_hash_code(pub_id, pub_encoded, thash);
  execute("INSERT INTO public_keys VALUES(?, ?, ?)", 
          thash().c_str(), pub_id().c_str(), pub_encoded().c_str());
}

void 
database::put_key(rsa_keypair_id const & priv_id, 
                  base64< arc4<rsa_priv_key> > const & priv_encoded)
{
  
  hexenc<id> thash;
  key_hash_code(priv_id, priv_encoded, thash);
  execute("INSERT INTO private_keys VALUES(?, ?, ?)", 
          thash().c_str(), priv_id().c_str(), priv_encoded().c_str());
}

void 
database::put_key_pair(rsa_keypair_id const & id, 
                       base64<rsa_pub_key> const & pub_encoded,
                       base64< arc4<rsa_priv_key> > const & priv_encoded)
{
  transaction_guard guard(*this);
  put_key(id, pub_encoded);
  put_key(id, priv_encoded);
  guard.commit();
}

void
database::delete_private_key(rsa_keypair_id const & pub_id)
{
  execute("DELETE FROM private_keys WHERE id = ?",
          pub_id().c_str());
}

// cert management

bool 
database::cert_exists(cert const & t,
                      string const & table)
{
  results res;
  string query = 
    "SELECT id FROM " + table + " WHERE id = ? "
    "AND name = ? "
    "AND value = ? " 
    "AND keypair = ? "
    "AND signature = ?";
    
  fetch(res, 1, any_rows, query.c_str(),
        t.ident().c_str(),
        t.name().c_str(),
        t.value().c_str(),
        t.key().c_str(),
        t.sig().c_str());
  I(res.size() == 0 || res.size() == 1);
  return res.size() == 1;
}

void 
database::put_cert(cert const & t,
                   string const & table)
{
  hexenc<id> thash;
  cert_hash_code(t, thash);

  string insert = "INSERT INTO " + table + " VALUES(?, ?, ?, ?, ?, ?) ";

  execute(insert.c_str(), 
          thash().c_str(),
          t.ident().c_str(),
          t.name().c_str(), 
          t.value().c_str(),
          t.key().c_str(),
          t.sig().c_str());
}

void 
database::results_to_certs(results const & res,
                           vector<cert> & certs)
{
  certs.clear();
  for (size_t i = 0; i < res.size(); ++i)
    {
      cert t;
      t = cert(hexenc<id>(res[i][0]), 
              cert_name(res[i][1]),
              base64<cert_value>(res[i][2]),
              rsa_keypair_id(res[i][3]),
              base64<rsa_sha1_signature>(res[i][4]));
      certs.push_back(t);
    }
}

void
database::install_functions(app_state * app)
{
  // register any functions we're going to use
  I(sqlite3_create_function(sql(), "unbase64", -1, 
                           SQLITE_UTF8, NULL,
			   &sqlite3_unbase64_fn, 
                           NULL, NULL) == 0);
}

void
database::install_views()
{
  // we don't currently use any views. re-enable this code if you find a
  // compelling reason to use views.

  /*
  // delete any existing views
  results res;
  fetch(res, one_col, any_rows,
        "SELECT name FROM sqlite_master WHERE type='view'");

  for (size_t i = 0; i < res.size(); ++i)
    {
      string drop = "DROP VIEW " + res[i][0];
      execute(drop.c_str());
    }
  // register any views we're going to use
  execute(views_constant);
  */
}

void 
database::get_certs(hexenc<id> const & ident, 
                    vector<cert> & certs,                       
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE id = ?";

  fetch(res, 5, any_rows, query.c_str(), ident().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE name = ?";
  fetch(res, 5, any_rows, query.c_str(), name().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table +
    " WHERE id = ? AND name = ?";

  fetch(res, 5, any_rows, query.c_str(), 
        ident().c_str(), name().c_str());
  results_to_certs(res, certs);
}

void 
database::get_certs(cert_name const & name,
                    base64<cert_value> const & val, 
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE name = ? AND value = ?";

  fetch(res, 5, any_rows, query.c_str(), 
        name().c_str(), val().c_str());
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
  string query = 
    "SELECT id, name, value, keypair, signature FROM " + table + 
    " WHERE id = ? AND name = ? AND value = ?";

  fetch(res, 5, any_rows, query.c_str(),
        ident().c_str(),
        name().c_str(),
        value().c_str());
  results_to_certs(res, certs);
}



bool 
database::revision_cert_exists(revision<cert> const & cert)
{ 
  return cert_exists(cert.inner(), "revision_certs"); 
}

bool 
database::manifest_cert_exists(manifest<cert> const & cert)
{ 
  return cert_exists(cert.inner(), "manifest_certs"); 
}

void 
database::put_manifest_cert(manifest<cert> const & cert)
{ 
  put_cert(cert.inner(), "manifest_certs"); 
}

void 
database::put_revision_cert(revision<cert> const & cert)
{ 
  put_cert(cert.inner(), "revision_certs"); 
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
database::get_revision_cert(hexenc<id> const & hash,
                           revision<cert> & c)
{
  results res;
  vector<cert> certs;
  fetch(res, 5, one_row, 
        "SELECT id, name, value, keypair, signature "
        "FROM revision_certs "
        "WHERE hash = ?", 
        hash().c_str());
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
        "SELECT id "
        "FROM revision_certs "
        "WHERE hash = ?", 
        hash().c_str());
  I(res.size() == 0 || res.size() == 1);
  return (res.size() == 1);
}

bool 
database::manifest_cert_exists(hexenc<id> const & hash)
{
  results res;
  vector<cert> certs;
  fetch(res, one_col, any_rows, 
        "SELECT id "
        "FROM manifest_certs "
        "WHERE hash = ?", 
        hash().c_str());
  I(res.size() == 0 || res.size() == 1);
  return (res.size() == 1);
}

void 
database::get_manifest_cert(hexenc<id> const & hash,
                            manifest<cert> & c)
{
  results res;
  vector<cert> certs;
  fetch(res, 5, one_row, 
        "SELECT id, name, value, keypair, signature "
        "FROM manifest_certs "
        "WHERE hash = ?", 
        hash().c_str());
  results_to_certs(res, certs);
  I(certs.size() == 1);
  c = manifest<cert>(certs[0]);
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

void 
database::get_manifest_certs(manifest_id const & id, 
                             cert_name const & name, 
                             vector< manifest<cert> > & ts)
{
  vector<cert> certs;
  get_certs(id.inner(), name, certs, "manifest_certs");
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
        "SELECT id FROM revisions WHERE id GLOB ?",
        pattern.c_str());
  
  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0]));  
}


void 
database::complete(string const & partial,
                   set<manifest_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        "SELECT id FROM manifests WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(manifest_id(res[i][0]));  
  
  res.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM manifest_deltas WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(manifest_id(res[i][0]));  
}

void 
database::complete(string const & partial,
                   set<file_id> & completions)
{
  results res;
  completions.clear();

  string pattern = partial + "*";

  fetch(res, 1, any_rows,
        "SELECT id FROM files WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));  
  
  res.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM file_deltas WHERE id GLOB ?",
        pattern.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));  
}

using commands::selector_type;

static void selector_to_certname(selector_type ty,
                                 string & s)
{
  switch (ty)
    {
    case commands::sel_author:
      s = author_cert_name;
      break;
    case commands::sel_branch:
      s = branch_cert_name;
      break;
    case commands::sel_date:
      s = date_cert_name;
      break;
    case commands::sel_tag:
      s = tag_cert_name;
      break;
    case commands::sel_ident:
    case commands::sel_unknown:
      I(false); // don't do this.
      break;
    }
}

void database::complete(selector_type ty,
                        string const & partial,
                        vector<pair<selector_type, string> > const & limit,
                        set<string> & completions)
{
  completions.clear();

  // step 1: the limit is transformed into an SQL select statement which
  // selects a set of IDs from the manifest_certs table which match the
  // limit.  this is done by building an SQL select statement for each term
  // in the limit and then INTERSECTing them all.

  string lim = "(";
  bool first_limit = true;
  for (vector<pair<selector_type, string> >::const_iterator i = limit.begin();
       i != limit.end(); ++i)
    {
      if (first_limit)
        first_limit = false;
      else
        lim += " INTERSECT ";
      
      if (i->first == commands::sel_ident)
        {
          lim += "SELECT id FROM revision_certs ";
          lim += (F("WHERE id GLOB '%s*'") 
                  % i->second).str();
        }
      else if (i->first == commands::sel_unknown)
        {
          lim += "SELECT id FROM revision_certs ";
          lim += (F(" WHERE (name='%s' OR name='%s' OR name='%s')")
                  % author_cert_name 
                  % tag_cert_name 
                  % branch_cert_name).str();
          lim += (F(" AND unbase64(value) glob '*%s*'")
                  % i->second).str();     
        }
      else
        {
          string certname;
          selector_to_certname(i->first, certname);
          lim += "SELECT id FROM revision_certs ";
          lim += (F("WHERE name='%s' AND unbase64(value) glob '*%s*'")
                  % certname % i->second).str();
        }
    }
  lim += ")";
  
  // step 2: depending on what we've been asked to disambiguate, we
  // will complete either some idents, or cert values, or "unknown"
  // which generally means "author, tag or branch"

  string query;
  if (ty == commands::sel_ident)
    {
      query = (F("SELECT id FROM %s") % lim).str();
    }
  else 
    {
      query = "SELECT value FROM revision_certs WHERE";
      if (ty == commands::sel_unknown)
        {               
          query += 
            (F(" (name='%s' OR name='%s' OR name='%s')")
             % author_cert_name 
             % tag_cert_name 
             % branch_cert_name).str();
        }
      else
        {
          string certname;
          selector_to_certname(ty, certname);
          query += 
            (F(" (name='%s')") % certname).str();
        }
        
      query += (F(" AND (unbase64(value) GLOB '*%s*')") % partial).str();
      query += (F(" AND (id IN %s)") % lim).str();
    }

  results res;
  fetch(res, one_col, any_rows, query.c_str());
  for (size_t i = 0; i < res.size(); ++i)
    {
      if (ty == commands::sel_ident)
        completions.insert(res[i][0]);
      else
        {
          base64<data> row_encoded(res[i][0]);
          data row_decoded;
          decode_base64(row_encoded, row_decoded);
          completions.insert(row_decoded());
        }
    }
}


// merkle nodes

bool 
database::merkle_node_exists(string const & type,
                             utf8 const & collection, 
                             size_t level,
                             hexenc<prefix> const & prefix)
{
  results res;
  std::ostringstream oss;
  oss << level;
  fetch(res, one_col, one_row, 
        "SELECT COUNT(*) "
        "FROM merkle_nodes "
        "WHERE type = ? "
        "AND collection = ? "
        "AND level = ? "
        "AND prefix = ? ",
        type.c_str(), 
        collection().c_str(), 
        oss.str().c_str(), 
        prefix().c_str());
  size_t n_nodes = lexical_cast<size_t>(res[0][0]);
  I(n_nodes == 0 || n_nodes == 1);
  return n_nodes == 1;
}

void 
database::get_merkle_node(string const & type,
                          utf8 const & collection, 
                          size_t level,
                          hexenc<prefix> const & prefix,
                          base64<merkle> & node)
{
  results res;
  std::ostringstream oss;
  oss << level;
  fetch(res, one_col, one_row, 
        "SELECT body "
        "FROM merkle_nodes "
        "WHERE type = ? "
        "AND collection = ? "
        "AND level = ? "
        "AND prefix = ?",
        type.c_str(), 
        collection().c_str(), 
        oss.str().c_str(), 
        prefix().c_str());
  node = res[0][0];
}

void 
database::put_merkle_node(string const & type,
                          utf8 const & collection, 
                          size_t level,
                          hexenc<prefix> const & prefix,                                       
                          base64<merkle> const & node)
{
  std::ostringstream oss;
  oss << level;
  execute("INSERT OR REPLACE "
          "INTO merkle_nodes "
          "VALUES (?, ?, ?, ?, ?)",
          type.c_str(), 
          collection().c_str(), 
          oss.str().c_str(), 
          prefix().c_str(), 
          node().c_str());
}

void 
database::erase_merkle_nodes(string const & type,
                             utf8 const & collection)
{
  execute("DELETE FROM merkle_nodes "
          "WHERE type = ? "
          "AND collection = ?",
          type.c_str(), collection().c_str());
}

// transaction guards

transaction_guard::transaction_guard(database & d) : committed(false), db(d) 
{
  db.begin_transaction();
}
transaction_guard::~transaction_guard()
{
  if (committed)
    db.commit_transaction();
  else
    db.rollback_transaction();
}

void 
transaction_guard::commit()
{
  committed = true;
}

