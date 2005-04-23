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
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"
#include "xdelta.hh"
#include "epoch.hh"

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
  int sqlite3_get_table_vprintf(sqlite3*,const char *sqlFormat,char ***resultp,
                                int *nrow,int *ncolumn,char **errmsg,va_list ap);
  const char *sqlite3_value_text_s(sqlite3_value *v);
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

int sqlite3_get_table_vprintf(sqlite3 * db,
                              char const * sqlFormat,
                              char *** resultp,
                              int * nrow,
                              int * ncolumn,
                              char ** errmsg,
                              va_list ap)
{ 
  char * formatted = sqlite3_vmprintf(sqlFormat, ap);
  int result = sqlite3_get_table(db, formatted, resultp, 
                                 nrow, ncolumn, errmsg);
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
  schema("e372b508bea9b991816d1c74680f7ae10d2a6d94"),
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

static void
sqlite3_unpack_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unpack()", -1);
      return;
    }
  data unpacked;
  unpack(base64< gzip<data> >(string(sqlite3_value_text_s(args[0]))), unpacked);
  sqlite3_result_blob(f, unpacked().c_str(), unpacked().size(), SQLITE_TRANSIENT);
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
      N(!fs::is_directory(filename), 
        F("database %s is a directory\n") % filename.string());
 
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
          N(c == *i, F("database %s is not an sqlite version 3 file, "
                       "try dump and reload") % filename.string());            
        }
    }
}


struct sqlite3 * 
database::sql(bool init)
{
  if (! __sql)
    {
      N(!filename.empty(), F("no database specified"));

      if (! init)
        {
          N(fs::exists(filename), 
            F("database %s does not exist") % filename.string());
          N(!fs::is_directory(filename), 
            F("database %s is a directory") % filename.string());
        }

      check_sqlite_format_version(filename);
      int error;
      error = sqlite3_open(filename.string().c_str(), &__sql);
      if (error)
        throw oops(string("could not open database: ") + filename.string() + 
                   (": " + string(sqlite3_errmsg(__sql))));
      if (init)
        execute(schema_constant);

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
  N(!fs::exists(filename),
    F("cannot create %s; it already exists\n") % filename.string());
  int error = sqlite3_open(filename.string().c_str(), &__sql);
  if (error)
    throw oops(string("could not open database: ") + filename.string() + 
               (string(sqlite3_errmsg(__sql))));
  
  while(in)
    {
      in.read(buf, constants::bufsz);
      tmp.append(buf, in.gcount());
    }

  execute(tmp.c_str());
}


void 
database::debug(string const & sql, ostream & out)
{
  results res;
  // "%s" construction prevents interpretation of %-signs in the query string
  // as formatting commands.
  fetch(res, any_cols, any_rows, "%s", sql.c_str());
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

void 
database::info(ostream & out)
{
  string id;
  calculate_schema_id(sql(), id);
  unsigned long space = 0, tmp;
  out << "schema version    : " << id << endl;

  out << "counts:" << endl;
  out << "  full manifests  : " << count("manifests") << endl;
  out << "  manifest deltas : " << count("manifest_deltas") << endl;
  out << "  full files      : " << count("files") << endl;
  out << "  file deltas     : " << count("file_deltas") << endl;
  out << "  revisions       : " << count("revisions") << endl;
  out << "  ancestry edges  : " << count("revision_ancestry") << endl;
  out << "  certs           : " << count("revision_certs") << endl;

  out << "bytes:" << endl;
  // FIXME: surely there is a less lame way to do this, that doesn't require
  // updating every time the schema changes?
  tmp = space_usage("manifests", "id || data");
  space += tmp;
  out << "  full manifests  : " << tmp << endl;

  tmp = space_usage("manifest_deltas", "id || base || delta");
  space += tmp;
  out << "  manifest deltas : " << tmp << endl;

  tmp = space_usage("files", "id || data");
  space += tmp;
  out << "  full files      : " << tmp << endl;

  tmp = space_usage("file_deltas", "id || base || delta");
  space += tmp;
  out << "  file deltas     : " << tmp << endl;

  tmp = space_usage("revisions", "id || data");
  space += tmp;
  out << "  revisions       : " << tmp << endl;

  tmp = space_usage("revision_ancestry", "parent || child");
  space += tmp;
  out << "  cached ancestry : " << tmp << endl;

  tmp = space_usage("revision_certs", "hash || id || name || value || keypair || signature");
  space += tmp;
  out << "  certs           : " << tmp << endl;

  out << "  total           : " << space << endl;
}

void 
database::version(ostream & out)
{
  string id;
  N(filename.string() != "",
    F("need database name"));
  int error = sqlite3_open(filename.string().c_str(), &__sql);
  if (error)
    {
      sqlite3_close(__sql);
      throw oops(string("could not open database: ") + filename.string() + 
                 (": " + string(sqlite3_errmsg(__sql))));
    }
  calculate_schema_id(__sql, id);
  sqlite3_close(__sql);
  out << "database schema version: " << id << endl;
}

void 
database::migrate()
{  
  N(filename.string() != "",
    F("need database name"));
  int error = sqlite3_open(filename.string().c_str(), &__sql);
  if (error)
    {
      sqlite3_close(__sql);
      throw oops(string("could not open database: ") + filename.string() + 
                 (": " + string(sqlite3_errmsg(__sql))));
    }
  migrate_monotone_schema(__sql);
  sqlite3_close(__sql);
}

void 
database::rehash()
{
  transaction_guard guard(*this);
  ticker mcerts("mcerts", "m", 1);
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
        execute("INSERT INTO public_keys VALUES('%q', '%q', '%q')", 
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
        execute("INSERT INTO private_keys VALUES('%q', '%q', '%q')", 
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
  if (__sql)
    {
      sqlite3_close(__sql);
      __sql = 0;
    }
}

static void 
assert_sqlite3_ok(int res)
{
  switch (res)
    {      
    case SQLITE_OK: 
      break;

    case SQLITE_ERROR:
      throw oops("SQL error or missing database");
      break;

    case SQLITE_INTERNAL:
      throw oops("An internal logic error in SQLite");
      break;

    case SQLITE_PERM:
      throw oops("Access permission denied");
      break;

    case SQLITE_ABORT:
      throw oops("Callback routine requested an abort");
      break;

    case SQLITE_BUSY:
      throw oops("The database file is locked");
      break;

    case SQLITE_LOCKED:
      throw oops("A table in the database is locked");
      break;

    case SQLITE_NOMEM:
      throw oops("A malloc() failed");
      break;

    case SQLITE_READONLY:
      throw oops("Attempt to write a readonly database");
      break;

    case SQLITE_INTERRUPT:
      throw oops("Operation terminated by sqlite3_interrupt()");
      break;

    case SQLITE_IOERR:
      throw oops("Some kind of disk I/O error occurred");
      break;

    case SQLITE_CORRUPT:
      throw oops("The database disk image is malformed");
      break;

    case SQLITE_NOTFOUND:
      throw oops("(Internal Only) Table or record not found");
      break;

    case SQLITE_FULL:
      throw oops("Insertion failed because database (or filesystem) is full");
      break;

    case SQLITE_CANTOPEN:
      throw oops("Unable to open the database file");
      break;

    case SQLITE_PROTOCOL:
      throw oops("database lock protocol error");
      break;

    case SQLITE_EMPTY:
      throw oops("(Internal Only) database table is empty");
      break;

    case SQLITE_SCHEMA:
      throw oops("The database schema changed");
      break;

    case SQLITE_TOOBIG:
      throw oops("Too much data for one row of a table");
      break;

    case SQLITE_CONSTRAINT:
      throw oops("Abort due to contraint violation");
      break;

    case SQLITE_MISMATCH:
      throw oops("Data type mismatch");
      break;

    case SQLITE_MISUSE:
      throw oops("Library used incorrectly");
      break;

    case SQLITE_NOLFS:
      throw oops("Uses OS features not supported on host");
      break;

    case SQLITE_AUTH:
      throw oops("Authorization denied");
      break;

    default:
      throw oops(string("Unknown DB result code: ") + lexical_cast<string>(res));
      break;
    }
}

void 
database::execute(char const * query, ...)
{
  va_list ap;
  int res;
  char * errmsg = NULL;

  va_start(ap, query);

  // log it
  char * formatted = sqlite3_vmprintf(query, ap);
  string qq(formatted);
  if (qq.size() > constants::db_log_line_sz) 
    qq = qq.substr(0, constants::db_log_line_sz) + string(" ...");
  L(F("db.execute(\"%s\")\n") % qq);
  sqlite3_free(formatted);

  va_end(ap);
  va_start(ap, query);

  // do it
  res = sqlite3_exec_vprintf(sql(), query, NULL, NULL, &errmsg, ap);

  va_end(ap);

  if (errmsg)
    throw oops(string("sqlite exec error ") + errmsg);

  assert_sqlite3_ok(res);

}

void 
database::fetch(results & res, 
                int const want_cols, 
                int const want_rows, 
                char const * query, ...)
{
  char ** result = NULL;
  int nrow;
  int ncol;
  char * errmsg = NULL;
  int rescode;

  va_list ap;
  res.clear();
  res.resize(0);
  va_start(ap, query);

  // log it
  char * formatted = sqlite3_vmprintf(query, ap);
  string qq(formatted);
  if (qq.size() > constants::log_line_sz) 
    qq = qq.substr(0, constants::log_line_sz) + string(" ...");
  L(F("db.fetch(\"%s\")\n") % qq);
  sqlite3_free(formatted);

  va_end(ap);
  va_start(ap, query);

  // do it
  rescode = sqlite3_get_table_vprintf(sql(), query, &result, &nrow, &ncol, &errmsg, ap);

  va_end(ap);

  cleanup_ptr<char **, void> 
    result_guard(result, &sqlite3_free_table);

  string ctx = string("db query [") + string(query) + "]: ";

  if (errmsg)
    throw oops(ctx + string("sqlite error ") + errmsg);
  assert_sqlite3_ok(rescode);

  if (want_cols == 0 && ncol == 0) return;
  if (want_rows == 0 && nrow == 0) return;
  if (want_cols == any_rows && ncol == 0) return;
  if (want_rows == any_rows && nrow == 0) return;

  if (want_cols != any_cols &&
      ncol != want_cols)
    throw oops((F("%s wanted %d columns, got %s")
                % ctx % want_cols % ncol).str());

  if (want_rows != any_rows &&
      nrow != want_rows)
    throw oops((F("%s wanted %d rows, got %s")
                % ctx % want_rows % nrow).str());

  if (!result)
    throw oops(ctx + "null result set");

  for (int i = 0; i < ncol; ++i) 
    if (!result[i])
      throw oops(ctx + "null column name");

  for (int row = 0; row < nrow; ++row) 
    {
      vector<string> rowvec;
      for (int col = 0; col < ncol; ++col)
        {
          int i = ((1 + row) * ncol) + col;
          if (!result[i])
            throw oops(ctx + "null result value");
          else
            rowvec.push_back(result[i]);
        }
      res.push_back(rowvec);
    }
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
  fetch(res, one_col, any_rows, 
        "SELECT id FROM '%q' WHERE id = '%q'",
        table.c_str(), ident().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}


bool 
database::delta_exists(hexenc<id> const & ident,
                       string const & table)
{
  results res;
  fetch(res, one_col, any_rows, 
        "SELECT id FROM '%q' WHERE id = '%q'",
        table.c_str(), ident().c_str());
  return res.size() > 0;
}

bool 
database::delta_exists(hexenc<id> const & ident,
                       hexenc<id> const & base,
                       string const & table)
{
  results res;
  fetch(res, one_col, any_rows, 
        "SELECT id FROM '%q' WHERE id = '%q' AND base = '%q'",
        table.c_str(), ident().c_str(), base().c_str());
  I((res.size() == 1) || (res.size() == 0));
  return res.size() == 1;
}

unsigned long
database::count(string const & table)
{
  results res;
  fetch(res, one_col, one_row, 
        "SELECT COUNT(*) FROM '%q'", 
        table.c_str());
  return lexical_cast<unsigned long>(res[0][0]);  
}

unsigned long
database::space_usage(string const & table, string const & concatenated_columns)
{
  results res;
  fetch(res, one_col, one_row,
        "SELECT SUM(LENGTH(%s)) FROM '%q'",
        concatenated_columns.c_str(), table.c_str());
  return lexical_cast<unsigned long>(res[0][0]);
}

void
database::get_ids(string const & table, set< hexenc<id> > & ids) 
{
  results res;

  fetch(res, one_col, any_rows, "SELECT id FROM %q", table.c_str());

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
  results res;
  fetch(res, one_col, one_row,
        "SELECT data FROM '%q' WHERE id = '%q'", 
        table.c_str(), ident().c_str());

  // consistency check
  base64<gzip<data> > rdata(res[0][0]);
  data rdata_unpacked;
  unpack(rdata, rdata_unpacked);

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
  fetch(res, one_col, one_row,
        "SELECT delta FROM '%q' WHERE id = '%q' AND base = '%q'", 
        table.c_str(), ident().c_str(), base().c_str());

  base64<gzip<delta> > del_packed = res[0][0];
  unpack(del_packed, del);
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
  I(tid == ident);

  base64<gzip<data> > dat_packed;
  pack(dat, dat_packed);
  
  execute("INSERT INTO '%q' VALUES('%q', '%q')", 
          table.c_str(), ident().c_str(), dat_packed().c_str());
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

  base64<gzip<delta> > del_packed;
  pack(del, del_packed);
  
  execute("INSERT INTO '%q' VALUES('%q', '%q', '%q')", 
          table.c_str(), 
          ident().c_str(), base().c_str(), del_packed().c_str());
}

// static ticker cache_hits("vcache hits", "h", 1);

struct version_cache
{
  size_t capacity;
  size_t use;
  std::map<hexenc<id>, data> cache;  

  version_cache() : capacity(constants::db_version_cache_sz), use(0) 
  {
    srand(time(NULL));
  }

  void put(hexenc<id> const & ident, data const & dat)
  {
    while (!cache.empty() 
           && use + dat().size() > capacity)
      {      
        std::string key = (F("%08.8x%08.8x%08.8x%08.8x%08.8x") 
                           % rand() % rand() % rand() % rand() % rand()).str();
        std::map<hexenc<id>, data>::const_iterator i;
        i = cache.lower_bound(hexenc<id>(key));
        if (i == cache.end())
          {
            // we can't find a random entry, probably there's only one
            // entry and we missed it. delete first entry instead.
            i = cache.begin();
          }
        I(i != cache.end());
        I(use >= i->second().size());
        L(F("version cache expiring %s\n") % i->first);
        use -= i->second().size();          
        cache.erase(i->first);
      }
    cache.insert(std::make_pair(ident, dat));
    use += dat().size();
  }

  bool exists(hexenc<id> const & ident)
  {
    std::map<hexenc<id>, data>::const_iterator i;
    i = cache.find(ident);
    return i != cache.end();
  }

  bool get(hexenc<id> const & ident, data & dat)
  {
    std::map<hexenc<id>, data>::const_iterator i;
    i = cache.find(ident);
    if (i == cache.end())
      return false;
    // ++cache_hits;
    L(F("version cache hit on %s\n") % ident);
    dat = i->second;
    return true;
  }
};

static version_cache vcache;

void 
database::get_version(hexenc<id> const & ident,
                      data & dat,
                      string const & data_table,
                      string const & delta_table)
{
  I(ident() != "");

  if (vcache.get(ident, dat))
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

      while (! found_root)
        {
          set< hexenc<id> > next_frontier;
          shared_ptr<edgemap> frontier_map(new edgemap());

          I(!frontier.empty());

          for (set< hexenc<id> >::const_iterator i = frontier.begin();
               i != frontier.end(); ++i)
            {
              if (vcache.exists(*i) || exists(*i, data_table))
                {
                  root = *i;
                  found_root = true;
                  break;
                }
              else
                {
                  cycles.insert(*i);
                  results res;
                  fetch(res, one_col, any_rows, "SELECT base from '%q' WHERE id = '%q'",
                        delta_table.c_str(), (*i)().c_str());
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
      data begin;      

      if (vcache.exists(root))
        {
          I(vcache.get(root, begin));
        }
      else
        {
          get(root, begin, data_table);
        }

      hexenc<id> curr = root;

      boost::shared_ptr<delta_applicator> app = new_piecewise_applicator();
      app->begin(begin());
      
      for (list< shared_ptr<edgemap> >::const_iterator p = paths.begin();
           p != paths.end(); ++p)
        {
          shared_ptr<edgemap> i = *p;
          I(i->find(curr) != i->end());
          hexenc<id> const nxt = i->find(curr)->second;

          if (!vcache.exists(curr))
            {
              string tmp;
              app->finish(tmp);
              vcache.put(curr, tmp);
            }


          L(F("following delta %s -> %s\n") % curr % nxt);
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
  vcache.put(ident, dat);
}


void 
database::drop(hexenc<id> const & ident, 
               string const & table)
{
  execute("DELETE FROM '%q' WHERE id = '%q'",  
          table.c_str(),
          ident().c_str());
}

void 
database::put_version(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      delta const & del,
                      string const & data_table,
                      string const & delta_table)
{

  data old_data, new_data;
  delta reverse_delta;
  
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
                              delta const & reverse_del,
                              string const & data_table,
                              string const & delta_table)
{
  data old_data, new_data;
  
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
database::get_file_ids(set<file_id> & ids) 
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("files", tmp);
  get_ids("file_deltas", tmp);
  ids.insert(tmp.begin(), tmp.end());
}

void 
database::get_manifest_ids(set<manifest_id> & ids) 
{
  ids.clear();
  set< hexenc<id> > tmp;
  get_ids("manifests", tmp);
  get_ids("manifest_deltas", tmp);
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
        "SELECT parent FROM revision_ancestry WHERE child = '%q'",
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
        "SELECT child FROM revision_ancestry WHERE parent = '%q'",
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
        "SELECT data FROM revisions WHERE id = '%q'",
        id.inner()().c_str());

  base64<gzip<data> > rdat_packed;
  rdat_packed = base64<gzip<data> >(res[0][0]);
  data rdat;
  unpack(rdat_packed, rdat);

  // verify that we got a revision with the right id
  {
    revision_id tmp;
    calculate_ident(rdat, tmp);
    I(id == tmp);
  }

  dat = rdat;
}

void 
database::put_revision(revision_id const & new_id,
                       revision_set const & rev)
{

  I(!null_id(new_id));
  I(!revision_exists(new_id));
  revision_data d;

  rev.check_sane();

  write_revision_set(rev, d);
  revision_id tmp;
  calculate_ident(d, tmp);
  I(tmp == new_id);

  base64<gzip<data> > d_packed;
  pack(d.inner(), d_packed);

  transaction_guard guard(*this);

  execute("INSERT INTO revisions VALUES('%q', '%q')", 
          new_id.inner()().c_str(), 
          d_packed().c_str());

  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      execute("INSERT INTO revision_ancestry VALUES('%q', '%q')", 
              edge_old_revision(e).inner()().c_str(),
              new_id.inner()().c_str());
    }

  check_sane_history(new_id, constants::verify_depth, *__app);

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
  execute("DELETE from revisions");
  execute("DELETE from revision_ancestry");
  execute("DELETE from revision_certs");
}

/// Deletes one revision from the local database. 
/// @see kill_rev_locally
void
database::delete_existing_rev_and_certs(revision_id const & rid){

  //check that the revision exists and doesn't have any children
  I(revision_exists(rid));
  set<revision_id> children;
  get_revision_children(rid, children);
  I(!children.size());

  // perform the actual SQL transactions to kill rev rid here
  L(F("Killing revision %s locally\n") % rid);
  execute("DELETE from revision_certs WHERE id = '%s'",rid.inner()().c_str());
  execute("DELETE from revision_ancestry WHERE child = '%s'",
          rid.inner()().c_str());
  execute("DELETE from revisions WHERE id = '%s'",rid.inner()().c_str());
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
          "SELECT id from public_keys WHERE id GLOB '%q'",
          pattern.c_str());
  else
    fetch(res, one_col, any_rows, 
          "SELECT id from public_keys");

  for (size_t i = 0; i < res.size(); ++i)
    pubkeys.push_back(res[i][0]);

  if (pattern != "")
    fetch(res, one_col, any_rows, 
          "SELECT id from private_keys WHERE id GLOB '%q'",
          pattern.c_str());
  else
    fetch(res, one_col, any_rows, 
          "SELECT id from private_keys");

  for (size_t i = 0; i < res.size(); ++i)
    privkeys.push_back(res[i][0]);
}

void 
database::get_keys(string const & table, vector<rsa_keypair_id> & keys)
{
  keys.clear();
  results res;
  fetch(res, one_col, any_rows,  "SELECT id from '%q'", table.c_str());
  for (size_t i = 0; i < res.size(); ++i)
    keys.push_back(res[i][0]);
}

void 
database::get_private_keys(vector<rsa_keypair_id> & keys)
{
  get_keys("private_keys", keys);
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
        "SELECT id FROM public_keys WHERE hash = '%q'",
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
        "SELECT id FROM public_keys WHERE id = '%q'",
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
        "SELECT id FROM private_keys WHERE id = '%q'",
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
        "SELECT id, keydata FROM public_keys where hash = '%q'", 
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
        "SELECT keydata FROM public_keys where id = '%q'", 
        pub_id().c_str());
  pub_encoded = res[0][0];
}

void 
database::get_key(rsa_keypair_id const & priv_id, 
                  base64< arc4<rsa_priv_key> > & priv_encoded)
{
  results res;
  fetch(res, one_col, one_col, 
        "SELECT keydata FROM private_keys where id = '%q'", 
        priv_id().c_str());
  priv_encoded = res[0][0];
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
  execute("INSERT INTO public_keys VALUES('%q', '%q', '%q')", 
          thash().c_str(), pub_id().c_str(), pub_encoded().c_str());
}

void 
database::put_key(rsa_keypair_id const & priv_id, 
                  base64< arc4<rsa_priv_key> > const & priv_encoded)
{
  hexenc<id> thash;
  key_hash_code(priv_id, priv_encoded, thash);
  E(!private_key_exists(priv_id),
    F("another key with name '%s' already exists") % priv_id);
  execute("INSERT INTO private_keys VALUES('%q', '%q', '%q')", 
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
  execute("DELETE FROM private_keys WHERE id = '%q'",
          pub_id().c_str());
}

void
database::delete_public_key(rsa_keypair_id const & pub_id)
{
  execute("DELETE FROM public_keys WHERE id = '%q'",
          pub_id().c_str());
}

// cert management

bool 
database::cert_exists(cert const & t,
                      string const & table)
{
  results res;
  fetch(res, 1, any_rows,
        "SELECT id FROM '%q' WHERE id = '%q' "
        "AND name = '%q' AND value = '%q' " 
        "AND keypair = '%q' AND signature = '%q' ",
        table.c_str(),
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
  execute("INSERT INTO '%q' VALUES('%q', '%q', '%q', '%q', '%q', '%q')", 
          table.c_str(),
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


  I(sqlite3_create_function(sql(), "unpack", -1, 
                           SQLITE_UTF8, NULL,
                           &sqlite3_unpack_fn, 
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
      execute("DROP VIEW '%q'", res[i][0].c_str());
    }
  // register any views we're going to use
  execute(views_constant);
  */
}

void 
database::get_certs(vector<cert> & certs,                       
                    string const & table)
{
  results res;
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature FROM '%q' ",
        table.c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    vector<cert> & certs,                       
                    string const & table)
{
  results res;
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature FROM '%q' "
        "WHERE id = '%q'", 
        table.c_str(),  
        ident().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature "
        "FROM '%q' WHERE name = '%q'", 
        table.c_str(),  
        name().c_str());
  results_to_certs(res, certs);
}


void 
database::get_certs(hexenc<id> const & ident, 
                    cert_name const & name,           
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature "
        "FROM '%q' "
        "WHERE id = '%q' AND name = '%q'", 
        table.c_str(),  
        ident().c_str(),
        name().c_str());
  results_to_certs(res, certs);
}

void 
database::get_certs(cert_name const & name,
                    base64<cert_value> const & val, 
                    vector<cert> & certs,
                    string const & table)
{
  results res;
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature "
        "FROM '%q' "
        "WHERE name = '%q' AND value = '%q'", 
        table.c_str(),  
        name().c_str(),
        val().c_str());
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
  fetch(res, 5, any_rows, 
        "SELECT id, name, value, keypair, signature "
        "FROM '%q' "
        "WHERE id = '%q' AND name = '%q' AND value = '%q'", 
        table.c_str(),  
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

void database::get_revision_cert_index(std::vector< std::pair<hexenc<id>,
                                       std::pair<revision_id, rsa_keypair_id> > > & idx)
{
  results res;
  fetch(res, 3, any_rows, 
        "SELECT hash, id, keypair "
        "FROM 'revision_certs'");

  idx.clear();
  idx.resize(res.size());
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      idx.push_back(std::make_pair(hexenc<id>((*i)[0]), 
                                   std::make_pair(revision_id((*i)[1]),
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
        "WHERE hash = '%q'", 
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
        "WHERE hash = '%q'", 
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
        "WHERE hash = '%q'", 
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
        "WHERE hash = '%q'", 
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

  fetch(res, 1, any_rows,
        "SELECT id FROM revisions WHERE id GLOB '%q*'",
        partial.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(revision_id(res[i][0]));  
}


void 
database::complete(string const & partial,
                   set<manifest_id> & completions)
{
  results res;
  completions.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM manifests WHERE id GLOB '%q*'",
        partial.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(manifest_id(res[i][0]));  
  
  res.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM manifest_deltas WHERE id GLOB '%q*'",
        partial.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(manifest_id(res[i][0]));  
}

void 
database::complete(string const & partial,
                   set<file_id> & completions)
{
  results res;
  completions.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM files WHERE id GLOB '%q*'",
        partial.c_str());

  for (size_t i = 0; i < res.size(); ++i)
    completions.insert(file_id(res[i][0]));  
  
  res.clear();

  fetch(res, 1, any_rows,
        "SELECT id FROM file_deltas WHERE id GLOB '%q*'",
        partial.c_str());

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
  if (limit.empty())
    {
      lim += "SELECT id FROM revision_certs";
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

// epochs 

void 
database::get_epochs(std::map<cert_value, epoch_data> & epochs)
{
  epochs.clear();
  results res;
  fetch(res, 2, any_rows, "SELECT branch, epoch FROM branch_epochs");
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {      
      base64<cert_value> encoded(idx(*i, 0));
      cert_value decoded;
      decode_base64(encoded, decoded);
      I(epochs.find(decoded) == epochs.end());
      epochs.insert(std::make_pair(decoded, epoch_data(idx(*i, 1))));
    }
}

void
database::get_epoch(epoch_id const & eid,
                    cert_value & branch, epoch_data & epo)
{
  I(epoch_exists(eid));
  results res;
  fetch(res, 2, any_rows,
        "SELECT branch, epoch FROM branch_epochs"
        " WHERE hash = '%q'",
        eid.inner()().c_str());
  I(res.size() == 1);
  base64<cert_value> encoded(idx(idx(res, 0), 0));
  decode_base64(encoded, branch);
  epo = epoch_data(idx(idx(res, 0), 1));
}

bool
database::epoch_exists(epoch_id const & eid)
{
  results res;
  fetch(res, one_col, any_rows,
        "SELECT hash FROM branch_epochs WHERE hash = '%q'",
        eid.inner()().c_str());
  I(res.size() == 1 || res.size() == 0);
  return res.size() == 1;
}

void 
database::set_epoch(cert_value const & branch, epoch_data const & epo)
{
  epoch_id eid;
  base64<cert_value> encoded;
  encode_base64(branch, encoded);
  epoch_hash_code(branch, epo, eid);
  I(epo.inner()().size() == constants::epochlen);
  execute("INSERT OR REPLACE INTO branch_epochs VALUES('%q', '%q', '%q')", 
          eid.inner()().c_str(), encoded().c_str(), epo.inner()().c_str());
}

void 
database::clear_epoch(cert_value const & branch)
{
  base64<cert_value> encoded;
  encode_base64(branch, encoded);
  execute("DELETE FROM branch_epochs WHERE branch = '%q'", encoded().c_str());
}

// vars

void
database::get_vars(std::map<var_key, var_value> & vars)
{
  vars.clear();
  results res;
  fetch(res, 3, any_rows, "SELECT domain, name, value FROM db_vars");
  for (results::const_iterator i = res.begin(); i != res.end(); ++i)
    {
      var_domain domain(idx(*i, 0));
      base64<var_name> name_encoded(idx(*i, 1));
      var_name name;
      decode_base64(name_encoded, name);
      base64<var_value> value_encoded(idx(*i, 2));
      var_value value;
      decode_base64(value_encoded, value);
      I(vars.find(std::make_pair(domain, name)) == vars.end());
      vars.insert(std::make_pair(std::make_pair(domain, name), value));
    }
}

void
database::get_var(var_key const & key, var_value & value)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  std::map<var_key, var_value> vars;
  get_vars(vars);
  std::map<var_key, var_value>::const_iterator i = vars.find(key);
  I(i != vars.end());
  value = i->second;
}

bool
database::var_exists(var_key const & key)
{
  // FIXME: sillyly inefficient.  Doesn't really matter, though.
  std::map<var_key, var_value> vars;
  get_vars(vars);
  std::map<var_key, var_value>::const_iterator i = vars.find(key);
  return i != vars.end();
}

void
database::set_var(var_key const & key, var_value const & value)
{
  base64<var_name> name_encoded;
  encode_base64(key.second, name_encoded);
  base64<var_value> value_encoded;
  encode_base64(value, value_encoded);
  execute("INSERT OR REPLACE INTO db_vars VALUES('%q', '%q', '%q')",
          key.first().c_str(),
          name_encoded().c_str(),
          value_encoded().c_str());
}

void
database::clear_var(var_key const & key)
{
  base64<var_name> name_encoded;
  encode_base64(key.second, name_encoded);
  execute("DELETE FROM db_vars WHERE domain = '%q' AND name = '%q'",
          key.first().c_str(), name_encoded().c_str());
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

