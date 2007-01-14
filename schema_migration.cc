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

// Wrappers around the bare sqlite3 API.  We do not use sqlite3_exec because
// we want the better error handling that sqlite3_prepare_v2 gives us.

namespace
{
  struct sql
  {
    sql(sqlite3 * db, int cols, char const *cmd, char const **afterp = 0)
      : stmt(0), ncols(cols)
    {
      sqlite3_stmt * s;

      char const * after;
      L(FL("executing SQL '%s'") % cmd);

      if (sqlite3_prepare_v2(db, cmd, strlen(cmd), &s, &after))
        error(db);

      I(s);
      if (afterp)
        *afterp = after;
      else
        I(*after == 0);
      I(sqlite3_column_count(s) == ncols);
      stmt = s;
    }
    ~sql()
    {
      if (stmt)
        sqlite3_finalize(stmt);
    }

    bool step()
    {
      int res = sqlite3_step(stmt);
      if (res == SQLITE_ROW)
        return true;
      if (res == SQLITE_DONE)
        {
          L(FL("success"));
          return false;
        }
      // Diagnostics from sqlite3_result_error show up in sqlite3_errmsg
      // only after sqlite3_finalize or sqlite3_reset are called on the
      // stmt object.  See SQLite ticket #1640.
      sqlite3 * db = sqlite3_db_handle(stmt);
      sqlite3_finalize(stmt);
      stmt = 0;
      error(db);
    }
    int column_int(int col)
    {
      I(col >= 0 && col < ncols);
      return sqlite3_column_int(stmt, col);
    }
    string column_string(int col)
    {
      I(col >= 0 && col < ncols);
      return string(reinterpret_cast<char const *>
                    (sqlite3_column_text(stmt, col)));
    }
    bool column_nonnull(int col)
    {
      I(col >= 0 && col < ncols);
      return sqlite3_column_type(stmt, col) != SQLITE_NULL;
    }

    // convenience for executing a sequence of sql statements,
    // none of which returns any rows.
    static void exec(sqlite3 * db, char const * cmd)
    {
      do
        {
          sql stmt(db, 0, cmd, &cmd);
          I(stmt.step() == false);
        }
      while (*cmd != '\0');
    }

    // convenience for evaluating an expression that returns a single number.
    static int value(sqlite3 * db, char const * cmd)
    {
      sql stmt(db, 1, cmd);

      I(stmt.step() == true);
      int res = stmt.column_int(0);
      I(stmt.step() == false);

      return res;
    }

    // convenience for making functions
    static void create_function(sqlite3 * db, char const * name,
                                void (*fn)(sqlite3_context *,
                                           int, sqlite3_value **))
    {
      if (sqlite3_create_function(db, name, -1, SQLITE_UTF8, 0, fn, 0, 0))
        error(db);
    }

  private:
    sqlite3_stmt * stmt;
    int ncols;

    static void NORETURN
    error(sqlite3 * db)
    {
      // note: useful error messages should be kept consistent with
      // assert_sqlite3_ok() in database.cc
      char const * errmsg = sqlite3_errmsg(db);
      int errcode = sqlite3_errcode(db);

      L(FL("sqlite error: %d: %s") % errcode % errmsg);

      // Check the string to see if it looks like an informative_failure
      // thrown from within an SQL extension function, caught, and turned
      // into a call to sqlite3_result_error.  (Extension functions have to
      // do this to avoid corrupting sqlite's internal state.)  If it is,
      // rethrow it rather than feeding it to E(), lest we get "error:
      // sqlite error: error: " ugliness.
      char const *pfx = _("error: ");
      if (!std::strncmp(errmsg, pfx, strlen(pfx)))
        throw informative_failure(errmsg);
  
      char const * auxiliary_message = "";
      switch (errcode)
        {
        case SQLITE_ERROR: // ??? take this out - 3.3.9 seems to generate
                           // it mostly for logic errors in the SQL,
                           // not environmental problems
        case SQLITE_IOERR:
        case SQLITE_CANTOPEN:
        case SQLITE_PROTOCOL:
          auxiliary_message
            = _("make sure database and containing directory are writeable\n"
                "and you have not run out of disk space");
          break;
        default: break;
        }

      E(false, F("sqlite error: %s\n%s") % errmsg % auxiliary_message);
    }
  };

  struct transaction
  {
    transaction(sqlite3 * s) : db(s), committed(false)
    {
      sql::exec(db, "BEGIN EXCLUSIVE");
    }
    void commit()
    {
      I(committed == false);
      committed = true;
    }
    ~transaction()
    {
      if (committed)
        sql::exec(db, "COMMIT");
      else
        sql::exec(db, "ROLLBACK");
    }
  private:
    sqlite3 * db;
    bool committed;
  };
}

// SQL extension functions.

// sqlite3_value_text returns unsigned char const *, which is inconvenient
inline char const *
sqlite3_value_cstr(sqlite3_value * arg)
{
  return reinterpret_cast<char const *>(sqlite3_value_text(arg));
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

static void
sqlite3_unbase64_fn(sqlite3_context *f, int nargs, sqlite3_value ** args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unbase64()", -1);
      return;
    }
  data decoded;

  // This operation may throw informative_failure.  We must intercept that
  // and turn it into a call to sqlite3_result_error, or rollback will fail.
  try
    {
      decode_base64(base64<data>(string(sqlite3_value_cstr(args[0]))), decoded);
    }
  catch (informative_failure & e)
    {
      sqlite3_result_error(f, e.what(), -1);
      return;
    }
  sqlite3_result_blob(f, decoded().c_str(), decoded().size(), SQLITE_TRANSIENT);
}

// Here are all of the migration steps.  Almost all of them can be expressed
// entirely as a series of SQL statements; those statements are packaged
// into a long, continued string constant for the step.  One step requires a
// function instead.

char const migrate_merge_url_and_group[] =
  // migrate the posting_queue table
  "ALTER TABLE posting_queue RENAME TO tmp;"
  "CREATE TABLE posting_queue"
  "  ( url not null,   -- URL we are going to send this to\n"
  "    content not null -- the packets we're going to send\n"
  "  );"
  "INSERT INTO posting_queue"
  "  SELECT (url || '/' || groupname), content FROM tmp;"
  "DROP TABLE tmp;"
  
  // migrate the incoming_queue table
  "ALTER TABLE incoming_queue RENAME TO tmp;"
  "CREATE TABLE incoming_queue "
  "  ( url not null,    -- URL we got this bundle from\n"
  "    content not null -- the packets we're going to read\n"
  "  );"
  "INSERT INTO incoming_queue"
  "  SELECT (url || '/' || groupname), content FROM tmp;"
  "DROP TABLE tmp;"

  // migrate the sequence_numbers table
  "ALTER TABLE sequence_numbers RENAME TO tmp;"
  "CREATE TABLE sequence_numbers "
  "  ( url primary key, -- URL to read from\n"
  "    major not null,  -- 0 in news servers, may be higher in depots\n"
  "    minor not null   -- last article / packet sequence number we got\n"
  "  );"
  "INSERT INTO sequence_numbers"
  "  SELECT (url || '/' || groupname), major, minor FROM tmp;"
  "DROP TABLE tmp;"


  // migrate the netserver_manifests table
  "ALTER TABLE netserver_manifests RENAME TO tmp;"
  "CREATE TABLE netserver_manifests"
  "  ( url not null, -- url of some server\n"
  "    manifest not null, -- manifest which exists on url\n"
  "    unique(url, manifest)"
  "  );"
  "INSERT INTO netserver_manifests"
  "  SELECT (url || '/' || groupname), manifest FROM tmp;"

  "DROP TABLE tmp;"
  ;

char const migrate_add_hashes_and_merkle_trees[] = 
  // add the column to manifest_certs
  "ALTER TABLE manifest_certs RENAME TO tmp;"
  "CREATE TABLE manifest_certs"
  "  ( hash not null unique, -- hash of remaining fields separated by \":\"\n"
  "    id not null,          -- joins with manifests.id or manifest_deltas.id\n"
  "    name not null,        -- opaque string chosen by user\n"
  "    value not null,       -- opaque blob\n"
  "    keypair not null,     -- joins with public_keys.id\n"
  "    signature not null,   -- RSA/SHA1 signature of \"[name@id:val]\"\n"
  "    unique(name, id, value, keypair, signature)"
  "  );"
  "INSERT INTO manifest_certs"
  "  SELECT sha1(':', id, name, value, keypair, signature),"
  "         id, name, value, keypair, signature"
  "         FROM tmp;"
  "DROP TABLE tmp;"

  // add the column to file_certs
  "ALTER TABLE file_certs RENAME TO tmp;"
  "CREATE TABLE file_certs"
  "  ( hash not null unique,   -- hash of remaining fields separated by \":\"\n"
  "    id not null,            -- joins with files.id or file_deltas.id\n"
  "    name not null,          -- opaque string chosen by user\n"
  "    value not null,         -- opaque blob\n"
  "    keypair not null,       -- joins with public_keys.id\n"
  "    signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
  "    unique(name, id, value, keypair, signature)"
  "  );"
  "INSERT INTO file_certs"
  "  SELECT sha1(':', id, name, value, keypair, signature),"
  "         id, name, value, keypair, signature"
  "         FROM tmp;"
  "DROP TABLE tmp;"

  // add the column to public_keys
  "ALTER TABLE public_keys RENAME TO tmp;"
  "CREATE TABLE public_keys"
  "  ( hash not null unique,   -- hash of remaining fields separated by \":\"\n"
  "    id primary key,         -- key identifier chosen by user\n"
  "    keydata not null        -- RSA public params\n"
  "  );"
  "INSERT INTO public_keys SELECT sha1(':',id,keydata), id, keydata FROM tmp;"
  "DROP TABLE tmp;"

  // add the column to private_keys
  "ALTER TABLE private_keys RENAME TO tmp;"
  "CREATE TABLE private_keys"
  "  ( hash not null unique, -- hash of remaining fields separated by \":\"\n"
  "    id primary key,       -- as in public_keys (same identifiers, in fact)\n"
  "    keydata not null      -- encrypted RSA private params\n"
  "  );"
  "INSERT INTO private_keys SELECT sha1(':',id,keydata), id, keydata FROM tmp;"
  "DROP TABLE tmp;"

  // add the merkle tree stuff
  "CREATE TABLE merkle_nodes"
  "  ( type not null,        -- \"key\", \"mcert\", \"fcert\", \"manifest\"\n"
  "    collection not null,  -- name chosen by user\n"
  "    level not null,       -- tree level this prefix encodes\n"
  "    prefix not null,      -- label identifying node in tree\n"
  "    body not null,        -- binary, base64'ed node contents\n"
  "    unique(type, collection, level, prefix)"
  ");"
  ;

char const migrate_to_revisions[] =
  "DROP TABLE schema_version;"
  "DROP TABLE posting_queue;"
  "DROP TABLE incoming_queue;"
  "DROP TABLE sequence_numbers;"
  "DROP TABLE file_certs;"
  "DROP TABLE netserver_manifests;"
  "DROP TABLE merkle_nodes;"

  "CREATE TABLE merkle_nodes"
  "  ( type not null,          -- \"key\", \"mcert\", \"fcert\", \"rcert\"\n"
  "    collection not null,    -- name chosen by user\n"
  "    level not null,         -- tree level this prefix encodes\n"
  "    prefix not null,        -- label identifying node in tree\n"
  "    body not null,          -- binary, base64'ed node contents\n"
  "    unique(type, collection, level, prefix)"
  "  );"

  "CREATE TABLE revision_certs"
  "  ( hash not null unique,   -- hash of remaining fields separated by \":\"\n"
  "    id not null,            -- joins with revisions.id\n"
  "    name not null,          -- opaque string chosen by user\n"
  "    value not null,         -- opaque blob\n"
  "    keypair not null,       -- joins with public_keys.id\n"
  "    signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
  "    unique(name, id, value, keypair, signature)"
  "  );"

  "CREATE TABLE revisions"
  "  ( id primary key,      -- SHA1(text of revision)\n"
  "    data not null        -- compressed, encoded contents of a revision\n"
  "  );"

  "CREATE TABLE revision_ancestry"
  "  ( parent not null,     -- joins with revisions.id\n"
  "    child not null,      -- joins with revisions.id\n"
  "    unique(parent, child)"
  "  );"
  ;

char const migrate_to_epochs[] =
  "DROP TABLE merkle_nodes;"
  "CREATE TABLE branch_epochs\n"
  "  ( hash not null unique,   -- hash of remaining fields separated by \":\"\n"
  "    branch not null unique, -- joins with revision_certs.value\n"
  "    epoch not null          -- random hex-encoded id\n"
  "  );"
  ;

char const migrate_to_vars[] = 
  "CREATE TABLE db_vars\n"
  "  ( domain not null,      -- scope of application of a var\n"
  "    name not null,        -- var key\n"
  "    value not null,       -- var value\n"
  "    unique(domain, name)"
  "  );"
  ;

char const migrate_add_indexes[] =
  "CREATE INDEX revision_ancestry__child ON revision_ancestry (child);"
  "CREATE INDEX revision_certs__id ON revision_certs (id);"
  "CREATE INDEX revision_certs__name_value ON revision_certs (name, value);"
  ;

// There is, perhaps, an argument for turning the logic inside the
// while-loop into a callback function like unbase64().  We would then not
// need a special case for this step in the master migration loop.  However,
// we'd have to get the app_state in there somehow, we might in the future
// need to do other things that can't be easily expressed in pure SQL, and
// besides I think it's clearer this way.
static void
migrate_to_external_privkeys(sqlite3 * db, app_state &app)
{
  {
    sql stmt(db, 3,
             "SELECT private_keys.id, private_keys.keydata, public_keys.keydata"
             "  FROM private_keys LEFT OUTER JOIN public_keys"
             "  ON private_keys.id = public_keys.id");

    while (stmt.step())
      {
        rsa_keypair_id ident = stmt.column_string(0);
        base64< arc4<rsa_priv_key> > old_priv = stmt.column_string(1);

        keypair kp;
        migrate_private_key(app, ident, old_priv, kp);
        MM(kp.pub);

        if (stmt.column_nonnull(2))
          {
            base64< rsa_pub_key > pub = stmt.column_string(2);
            MM(pub);
            N(keys_match(ident, pub, ident, kp.pub),
              F("public and private keys for %s don't match") % ident);
          }
        P(F("moving key '%s' from database to %s")
          % ident % app.keys.get_key_dir());
        app.keys.put_key_pair(ident, kp);
      }
  }

  sql::exec(db, "DROP TABLE private_keys;");
}

char const migrate_add_rosters[] =
  "CREATE TABLE rosters"
  "  ( id primary key,   -- strong hash of the roster\n"
  "    data not null     -- compressed, encoded contents of the roster\n"
  "  );"

  "CREATE TABLE roster_deltas"
  "  ( id not null,      -- strong hash of the roster\n"
  "    base not null,    -- joins with either rosters.id or roster_deltas.id\n"
  "    delta not null,   -- rdiff to construct current from base\n"
  "    unique(id, base)"
  "  );"

  "CREATE TABLE revision_roster"
  "  ( rev_id primary key, -- joins with revisions.id\n"
  "    roster_id not null -- joins with either rosters.id or roster_deltas.id\n"
  "  );"

  "CREATE TABLE next_roster_node_number"
  "  ( node primary key        -- only one entry in this table, ever\n"
  "  );"
  ;

// I wish I had a form of ALTER TABLE COMMENT on sqlite3
char const migrate_files_BLOB[] = 
  // change the encoding of file(_delta)s
  "ALTER TABLE files RENAME TO tmp;"
  "CREATE TABLE files"
  "  ( id primary key,   -- strong hash of file contents\n"
  "    data not null     -- compressed contents of a file\n"
  "  );"
  "INSERT INTO files SELECT id, unbase64(data) FROM tmp;"
  "DROP TABLE tmp;"

  "ALTER TABLE file_deltas RENAME TO tmp;"
  "CREATE TABLE file_deltas"
  "  ( id not null,      -- strong hash of file contents\n"
  "    base not null,    -- joins with files.id or file_deltas.id\n"
  "    delta not null,   -- compressed rdiff to construct current from base\n"
  "    unique(id, base)"
  "  );"
  "INSERT INTO file_deltas SELECT id, base, unbase64(delta) FROM tmp;"
  "DROP TABLE tmp;"

  // migrate other contents which are accessed by get|put_version
  "UPDATE manifests       SET data=unbase64(data);"
  "UPDATE manifest_deltas SET delta=unbase64(delta);"
  "UPDATE rosters         SET data=unbase64(data) ;"
  "UPDATE roster_deltas   SET delta=unbase64(delta);"
  "UPDATE db_vars         SET value=unbase64(value), name=unbase64(name);"
  "UPDATE public_keys     SET keydata=unbase64(keydata);"
  "UPDATE revision_certs  SET value=unbase64(value),"
  "                           signature=unbase64(signature);"
  "UPDATE manifest_certs  SET value=unbase64(value),"
  "                           signature=unbase64(signature);"
  "UPDATE revisions       SET data=unbase64(data);"
  "UPDATE branch_epochs   SET branch=unbase64(branch);"
  ;

char const migrate_rosters_no_hash[] =
  "DROP TABLE rosters;"
  "DROP TABLE roster_deltas;"
  "DROP TABLE revision_roster;"

  "CREATE TABLE rosters"
  "  ( id primary key,    -- a revision id\n"
  "    checksum not null, -- checksum of 'data', to protect against"
  "                          disk corruption\n"
  "    data not null      -- compressed, encoded contents of the roster\n"
  "  );"

  "CREATE TABLE roster_deltas"
  "  ( id primary key,    -- a revision id\n"
  "    checksum not null, -- checksum of 'delta', to protect against"
  "                          disk corruption\n"
  "    base not null,     -- joins with either rosters.id or roster_deltas.id\n"
  "    delta not null     -- rdiff to construct current from base\n"
  "  );"
  ;

char const migrate_add_heights[] =
  "CREATE TABLE heights"
  "  ( revision not null,	-- joins with revisions.id\n"
  "    height not null,	-- complex height, array of big endian u32 integers\n"
  "    unique(revision, height)"
  "  );"
  ;

// these must be listed in order so that ones listed earlier override ones
// listed later
enum upgrade_regime
  {
    upgrade_changesetify,
    upgrade_rosterify,
    upgrade_regen_caches,
    upgrade_none, 
  };

typedef void (*migrator_cb)(sqlite3 *, app_state &);

// Exactly one of migrator_sql and migrator_func should be non-null in
// all entries in migration_events, except the very last.
struct migration_event
{
  char const * id;
  char const * migrator_sql;
  migrator_cb migrator_func;
  upgrade_regime regime;
};

// IMPORTANT: whenever you modify this to add a new schema version, you must
// also add a new migration test for the new schema version.  See
// tests/schema_migration for details.

const migration_event migration_events[] = {
  { "edb5fa6cef65bcb7d0c612023d267c3aeaa1e57a",
    migrate_merge_url_and_group, 0, upgrade_none },

  { "f042f3c4d0a4f98f6658cbaf603d376acf88ff4b",
    migrate_add_hashes_and_merkle_trees, 0, upgrade_none },

  { "8929e54f40bf4d3b4aea8b037d2c9263e82abdf4",
    migrate_to_revisions, 0, upgrade_changesetify },

  { "c1e86588e11ad07fa53e5d294edc043ce1d4005a",
    migrate_to_epochs, 0, upgrade_none },

  { "40369a7bda66463c5785d160819ab6398b9d44f4",
    migrate_to_vars, 0, upgrade_none },

  { "e372b508bea9b991816d1c74680f7ae10d2a6d94",
    migrate_add_indexes, 0, upgrade_none },

  { "1509fd75019aebef5ac3da3a5edf1312393b70e9",
    0, migrate_to_external_privkeys, upgrade_none },

  { "bd86f9a90b5d552f0be1fa9aee847ea0f317778b",
    migrate_add_rosters, 0, upgrade_rosterify },

  { "1db80c7cee8fa966913db1a463ed50bf1b0e5b0e",
    migrate_files_BLOB, 0, upgrade_none },

  { "9d2b5d7b86df00c30ac34fe87a3c20f1195bb2df",
    migrate_rosters_no_hash, 0, upgrade_regen_caches },

  { "ae196843d368d042f475e3dadfed11e9d7f9f01e",
    migrate_add_heights, 0, upgrade_regen_caches },

  // The last entry in this table should always be the current
  // schema ID, with 0 for the migrators.

  { "48fd5d84f1e5a949ca093e87e5ac558da6e5956d", 0, 0, upgrade_none }
};
const size_t n_migration_events = (sizeof migration_events
                                   / sizeof migration_events[0]);

void
calculate_schema_id(sqlite3 *db, string & ident)
{
  sql stmt(db, 1,
           "SELECT sql FROM sqlite_master "
           "WHERE (type = 'table' OR type = 'index') "
           // filter out NULL statements, because
           // those are auto-generated indices (for
           // UNIQUE constraints, etc.).
           "AND sql IS NOT NULL "
           "AND name not like 'sqlite_stat%' "
           "ORDER BY name");

  string schema;
  using boost::char_separator;
  typedef boost::tokenizer<char_separator<char> > tokenizer;
  char_separator<char> sep(" \r\n\t", "(),;");

  while (stmt.step())
    {
      string table_schema(stmt.column_string(0));
      tokenizer tokens(table_schema, sep);
      for (tokenizer::iterator i = tokens.begin(); i != tokens.end(); i++)
        {
          if (schema.size() != 0)
            schema += " ";
          schema += *i;
        }
    }

  hexenc<id> tid;
  calculate_ident(data(schema), tid);
  ident = tid();
}

// Look through the migration_events table and return a pointer to the
// entry corresponding to schema ID, or null if it isn't there (i.e. if
// the database schema is not one we know).
static migration_event const *
schema_to_migration(string const & id)
{
  migration_event const *p;
  for (p = migration_events + n_migration_events - 1;
       p >= migration_events; p--)
    if (p->id == id)
      return p;

  return 0;
}

// Provide sensible diagnostics for a database schema whose hash we do not
// recognize.
static void NORETURN
diagnose_unrecognized_schema(sqlite3 * db, system_path const & filename,
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
  int n = sql::value(db,
                     "SELECT COUNT(*) FROM sqlite_master "
                     "WHERE type = 'table' AND sql IS NOT NULL "
                     "AND (name = 'files' OR name = 'file_deltas'"
                     "     OR name = 'manifests' OR name = 'manifest_deltas')");
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
check_sql_schema(sqlite3 * db, system_path const & filename)
{
  I(db != NULL);
 
  string id;
  calculate_schema_id(db, id);
 
  migration_event const *m = schema_to_migration(id);
 
  if (m == 0)
    diagnose_unrecognized_schema(db, filename, id);
 
  N(m->migrator_sql == 0 && m->migrator_func == 0,
    F("database %s is laid out according to an old schema, %s\n"
      "try '%s db migrate' to upgrade\n"
      "(this is irreversible; you may want to make a backup copy first)")
    % filename % id % ui.prog_name);
}

void
migrate_sql_schema(sqlite3 * db, app_state & app)
{
  I(db != NULL);
  sql::create_function(db, "sha1", sqlite_sha1_fn);
  sql::create_function(db, "unbase64", sqlite3_unbase64_fn);

  upgrade_regime regime = upgrade_none;
  
  // Take an exclusive lock on the database before we try to read anything
  // from it.  If we don't take this lock until the beginning of the
  // "migrating data" phase, two simultaneous "db migrate" processes could
  // race through the "calculating migration" phase; then one of them would
  // wait for the other to finish all the migration steps, and trip over the
  // invariant check inside the for loop.
  {
    transaction guard(db);

    string init;
    calculate_schema_id(db, init);

    P(F("calculating migration for schema %s") % init);

    migration_event const *m = schema_to_migration(init);

    if (m == 0)
      diagnose_unrecognized_schema(db, app.db.get_filename(), init);

    // We really want 'db migrate' on an up-to-date schema to be a no-op
    // (no vacuum or anything, even), so that automated scripts can fire
    // one off optimistically and not have to worry about getting their
    // administrators to do it by hand.
    if (m->migrator_func == 0 && m->migrator_sql == 0)
      {
        P(F("no migration performed; database schema already up-to-date"));
        return;
      }

    P(F("migrating data"));

    for (;;)
      {
        // confirm that we are where we ought to be
        string curr;
        calculate_schema_id(db, curr);
        I(curr == m->id);
        I(!m->migrator_sql || !m->migrator_func);

        if (m->migrator_sql)
          sql::exec(db, m->migrator_sql);
        else if (m->migrator_func)
          m->migrator_func(db, app);
        else
          break;

        regime = std::min(regime, m->regime);

        m++;
        I(m < migration_events + n_migration_events);
      }

    P(F("committing changes to database"));
    guard.commit();
  }

  P(F("optimizing database"));
  sql::exec(db, "VACUUM");

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

// test_migration_step runs the migration step from SCHEMA to its successor,
// *without* validating that the database actually conforms to that schema
// first.  the point of this is to test error recovery from conditions that
// are not accessible through normal malformed dumps (because the schema
// conformance check will reject them).

void
test_migration_step(sqlite3 * db, app_state & app, string const & schema)
{
  I(db != NULL);
  sql::create_function(db, "sha1", sqlite_sha1_fn);
  sql::create_function(db, "unbase64", sqlite3_unbase64_fn);

  transaction guard(db);

  migration_event const *m = schema_to_migration(schema);
  N(m, F("cannot test migration from unknown schema %s") % schema);

  N(m->migrator_sql || m->migrator_func, F("schema %s is up to date") % schema);

  L(FL("testing migration from %s to %s\n in database %s")
    % schema % m[1].id % app.db.get_filename());

  if (m->migrator_sql)
    sql::exec(db, m->migrator_sql);
  else
    m->migrator_func(db, app);

  // in the unlikely event that we get here ...
  P(F("successful migration to schema %s") % m[1].id);
  guard.commit();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
