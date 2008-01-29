// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <boost/tokenizer.hpp>
#include "lexical_cast.hh"
#include "sqlite/sqlite3.h"
#include <cstring>

#include "sanity.hh"
#include "schema_migration.hh"
#include "key_store.hh"
#include "keys.hh"
#include "transforms.hh"
#include "ui.hh"

using std::string;

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

void
assert_sqlite3_ok(sqlite3 * db)
{
  int errcode = sqlite3_errcode(db);

  if (errcode == SQLITE_OK)
    return;

  char const * errmsg = sqlite3_errmsg(db);

  // first log the code so we can find _out_ what the confusing code
  // was... note that code does not uniquely identify the errmsg, unlike
  // errno's.
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

  // sometimes sqlite is not very helpful
  // so we keep a table of errors people have gotten and more helpful versions
  char const * auxiliary_message = "";
  switch (errcode)
    {
      // All memory-exhaustion conditions should give the same diagnostic.
    case SQLITE_NOMEM:
      throw std::bad_alloc();

      // These diagnostics generally indicate an operating-system-level
      // failure.  It would be nice to throw strerror(errno) in there but
      // we cannot assume errno is still valid by the time we get here.
    case SQLITE_IOERR:
    case SQLITE_CANTOPEN:
    case SQLITE_PROTOCOL:
      auxiliary_message
        = _("make sure database and containing directory are writeable\n"
            "and you have not run out of disk space");
      break;

      // These error codes may indicate someone is trying to load a database
      // so old that it's in sqlite 2's disk format (monotone 0.16 or
      // older).
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
      auxiliary_message 
        = _("(if this is a database last used by monotone 0.16 or older,\n"
            "you must follow a special procedure to make it usable again.\n"
            "see the file UPGRADE, in the distribution, for instructions.)");

    default:
      break;
    }

  // if the auxiliary message is empty, the \n will be stripped off too
  E(false, F("sqlite error: %s\n%s") % errmsg % auxiliary_message);
}


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

      sqlite3_prepare_v2(db, cmd, strlen(cmd), &s, &after);
      assert_sqlite3_ok(db);

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
      assert_sqlite3_ok(db);
      I(false);
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
      sqlite3_create_function(db, name, -1, SQLITE_UTF8, 0, fn, 0, 0);
      assert_sqlite3_ok(db);
    }

  private:
    sqlite3_stmt * stmt;
    int ncols;
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

static void
sqlite3_unhex_fn(sqlite3_context *f, int nargs, sqlite3_value **args)
{
  if (nargs != 1)
    {
      sqlite3_result_error(f, "need exactly 1 arg to unhex()", -1);
      return;
    }
  data decoded;

  try
    {
      decode_hexenc(hexenc<data>(string(sqlite3_value_cstr(args[0]))), decoded);
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
// into a long, continued string constant for the step.  A few require a
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
// while-loop into a callback function like unbase64().  However, we'd have
// to get the key_store in there somehow, and besides I think it's clearer
// this way.
static void
migrate_to_external_privkeys(sqlite3 * db, key_store & keys)
{
  {
    sql stmt(db, 3,
             "SELECT private_keys.id, private_keys.keydata, public_keys.keydata"
             "  FROM private_keys LEFT OUTER JOIN public_keys"
             "  ON private_keys.id = public_keys.id");

    while (stmt.step())
      {
        rsa_keypair_id ident(stmt.column_string(0));
        base64< old_arc4_rsa_priv_key > old_priv(stmt.column_string(1));

        keypair kp;
        migrate_private_key(keys, ident, old_priv, kp);
        MM(kp.pub);

        if (stmt.column_nonnull(2))
          {
            base64< rsa_pub_key > pub(stmt.column_string(2));
            MM(pub);
            N(keys_match(ident, pub, ident, kp.pub),
              F("public and private keys for %s don't match") % ident);
          }
        P(F("moving key '%s' from database to %s")
          % ident % keys.get_key_dir());
        keys.put_key_pair(ident, kp);
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

char const migrate_add_heights_index[] =
  "CREATE INDEX heights__height ON heights (height);"
  ;

char const migrate_to_binary_hashes[] =
  "UPDATE files             SET id=unhex(id);"
  "UPDATE file_deltas       SET id=unhex(id), base=unhex(base);"
  "UPDATE revisions         SET id=unhex(id);"
  "UPDATE revision_ancestry SET parent=unhex(parent), child=unhex(child);"
  "UPDATE heights           SET revision=unhex(revision);"
  "UPDATE rosters           SET id=unhex(id);"
  "UPDATE roster_deltas     SET id=unhex(id), base=unhex(base);"
  "UPDATE public_keys       SET hash=unhex(hash);"

  // revision_certs also gets a new index, so we recreate the
  // table completely.
  "ALTER TABLE revision_certs RENAME TO tmp;\n"
  "CREATE TABLE revision_certs"
	"  ( hash not null unique,   -- hash of remaining fields separated by \":\"\n"
	"    id not null,            -- joins with revisions.id\n"
	"    name not null,          -- opaque string chosen by user\n"
	"    value not null,         -- opaque blob\n"
	"    keypair not null,       -- joins with public_keys.id\n"
	"    signature not null,     -- RSA/SHA1 signature of \"[name@id:val]\"\n"
	"    unique(name, value, id, keypair, signature)\n"
	"  );"
  "INSERT INTO revision_certs SELECT unhex(hash), unhex(id), name, value, keypair, signature FROM tmp;"
  "DROP TABLE tmp;"
  "CREATE INDEX revision_certs__id ON revision_certs (id);"

  // We altered a comment on this table, thus we need to recreated it.
  // Additionally, this is the only schema change, so that we get another
  // schema hash to upgrade to.
  "ALTER TABLE branch_epochs RENAME TO tmp;"
  "CREATE TABLE branch_epochs"
	"  ( hash not null unique,         -- hash of remaining fields separated by \":\"\n"
	"    branch not null unique,       -- joins with revision_certs.value\n"
	"    epoch not null                -- random binary id\n"
	"  );"
  "INSERT INTO branch_epochs SELECT unhex(hash), branch, unhex(epoch) FROM tmp;"
  "DROP TABLE tmp;"

  // To be able to migrate from pre-roster era, we also need to convert
  // these deprecated tables
  "UPDATE manifests         SET id=unhex(id);"
  "UPDATE manifest_deltas   SET id=unhex(id), base=unhex(base);"
  "UPDATE manifest_certs    SET id=unhex(id), hash=unhex(hash);"
  ;

// this is a function because it has to refer to the numeric constant
// defined in schema_migration.hh.
static void
migrate_add_ccode(sqlite3 * db, key_store &)
{
  string cmd = "PRAGMA user_version = ";
  cmd += boost::lexical_cast<string>(mtn_creator_code);
  sql::exec(db, cmd.c_str());
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
dump(enum upgrade_regime const & regime, string & out)
{
  switch (regime)
    {
    case upgrade_changesetify:  out = "upgrade_changesetify"; break;
    case upgrade_rosterify:     out = "upgrade_rosterify"; break;
    case upgrade_regen_caches:  out = "upgrade_regen_caches"; break;
    case upgrade_none:          out = "upgrade_none"; break;
    default: out = (FL("upgrade_regime(%d)") % regime).str(); break;
    }
}

typedef void (*migrator_cb)(sqlite3 *, key_store &);

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

  { "48fd5d84f1e5a949ca093e87e5ac558da6e5956d",
    0, migrate_add_ccode, upgrade_none },

  { "fe48b0804e0048b87b4cea51b3ab338ba187bdc2",
    migrate_add_heights_index, 0, upgrade_none },

  { "7ca81b45279403419581d7fde31ed888a80bd34e",
    migrate_to_binary_hashes, 0, upgrade_none },

  // The last entry in this table should always be the current
  // schema ID, with 0 for the migrators.
  { "212dd25a23bfd7bfe030ab910e9d62aa66aa2955", 0, 0, upgrade_none }
};
const size_t n_migration_events = (sizeof migration_events
                                   / sizeof migration_events[0]);

// unfortunately, this has to be aware of the migration_events array and its
// limits, lest we crash trying to print the garbage on either side.
static void
dump(struct migration_event const * const & mref, string & out)
{
  struct migration_event const * m = mref;
  ptrdiff_t i = m - migration_events;
  if (m == 0)
    out = "invalid migration event (null pointer)";
  else if (i < 0 || static_cast<size_t>(i) >= n_migration_events)
    out = (FL("invalid migration event, index %ld/%lu")
           % i % n_migration_events).str();
  else
    {
      char const * type;
      if (m->migrator_sql)
        type = "SQL only";
      else if (m->migrator_func)
        type = "codeful";
      else
        type = "none (current)";

      string regime;
      dump(m->regime, regime);

      out = (FL("migration %ld/%lu: %s, %s, from %s")
             % i % n_migration_events % type % regime % m->id).str();
    }
}

// The next several functions are concerned with calculating the schema hash
// and determining whether a database is usable (with or without migration).
static void
calculate_schema_id(sqlite3 * db, string & ident)
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

  u32 code = sql::value(db, "PRAGMA user_version");
  if (code != 0)
    {
      schema += " PRAGMA user_version = ";
      schema += boost::lexical_cast<string>(code);
    }
  hexenc<id> tid;
  calculate_ident(data(schema), tid);
  ident = tid();
}

// Look through the migration_events table and return a pointer to the entry
// corresponding to database DB, or null if it isn't there (i.e. if the
// database schema is not one we know).
static migration_event const *
find_migration(sqlite3 * db)
{
  string id;
  calculate_schema_id(db, id);

  for (migration_event const *m = migration_events + n_migration_events - 1;
       m >= migration_events; m--)
    if (m->id == id)
      return m;

  return 0;
}

// This enumerates the possible mismatches between the monotone executable
// and its database.
enum schema_mismatch_case
  {
    SCHEMA_MATCHES = 0,
    SCHEMA_MIGRATION_NEEDED,
    SCHEMA_TOO_NEW,
    SCHEMA_NOT_MONOTONE,
    SCHEMA_EMPTY
  };
static void dump(schema_mismatch_case const & cat, std::string & out)
{
  switch (cat)
    {
    case SCHEMA_MATCHES: out = "SCHEMA_MATCHES"; break;
    case SCHEMA_MIGRATION_NEEDED: out = "SCHEMA_MIGRATION_NEEDED"; break;
    case SCHEMA_TOO_NEW: out = "SCHEMA_TOO_NEW"; break;
    case SCHEMA_NOT_MONOTONE: out = "SCHEMA_NOT_MONOTONE"; break;
    case SCHEMA_EMPTY: out = "SCHEMA_EMPTY"; break;
    default: out = (FL("schema_mismatch_case(%d)") % cat).str(); break;
    }
}


static schema_mismatch_case
classify_schema(sqlite3 * db, migration_event const * m = 0)
{
  if (!m)
    m = find_migration(db);

  if (m)
    {
      if (m->migrator_sql || m->migrator_func)
        return SCHEMA_MIGRATION_NEEDED;
      else
        return SCHEMA_MATCHES;
    }
  else
    {
      // Distinguish an utterly empty database, such as is created by
      // "mtn db load < /dev/null", or by the sqlite3 command line utility
      // if you don't give it anything to do.
      if (sql::value(db, "SELECT COUNT(*) FROM sqlite_master") == 0)
        return SCHEMA_EMPTY;

      // monotone started setting this value in database headers only with
      // version 0.33, but all previous versions' databases are recognized
      // by their schema hashes.

      u32 code = sql::value(db, "PRAGMA user_version");
      if (code != mtn_creator_code)
        return SCHEMA_NOT_MONOTONE;

      return SCHEMA_TOO_NEW;
    }
}

string
describe_sql_schema(sqlite3 * db)
{
  I(db != NULL);
  string hash;
  calculate_schema_id(db, hash);

  switch (classify_schema(db))
    {
    case SCHEMA_MATCHES:
      return (F("%s (usable)") % hash).str();
    case SCHEMA_MIGRATION_NEEDED:
      return (F("%s (migration needed)") % hash).str();
    case SCHEMA_TOO_NEW:
      return (F("%s (too new, cannot use)") % hash).str();
    case SCHEMA_NOT_MONOTONE:
      return (F("%s (not a monotone database)") % hash).str();
    case SCHEMA_EMPTY:
      return (F("%s (database has no tables!)") % hash).str();
    default:
      I(false);
    }
}

// Provide sensible diagnostics for a database schema whose hash we do not
// recognize.  (Shared between check_sql_schema and migrate_sql_schema.)
static void
diagnose_unrecognized_schema(schema_mismatch_case cat,
                             system_path const & filename)
{
  N(cat != SCHEMA_EMPTY,
    F("cannot use the empty sqlite database %s\n"
      "(monotone databases must be created with '%s db init')")
    % filename % ui.prog_name);

  N(cat != SCHEMA_NOT_MONOTONE,
    F("%s does not appear to be a monotone database\n")
    % filename);

  N(cat != SCHEMA_TOO_NEW,
    F("%s appears to be a monotone database, but this version of\n"
      "monotone does not recognize its schema.\n"
      "you probably need a newer version of monotone.")
    % filename);
}

// check_sql_schema is called by database.cc on open, to determine whether
// the schema is up to date.  If it returns at all, the schema is indeed
// up to date (otherwise it throws a diagnostic).
void
check_sql_schema(sqlite3 * db, system_path const & filename)
{
  I(db != NULL);
 
  schema_mismatch_case cat = classify_schema(db);
 
  diagnose_unrecognized_schema(cat, filename);
 
  N(cat != SCHEMA_MIGRATION_NEEDED,
    F("database %s is laid out according to an old schema\n"
      "try '%s db migrate' to upgrade\n"
      "(this is irreversible; you may want to make a backup copy first)")
    % filename % ui.prog_name);
}

void
migrate_sql_schema(sqlite3 * db, system_path const & filename,
                   key_store & keys)
{
  I(db != NULL);

  upgrade_regime regime = upgrade_none; MM(regime);
  
  // Take an exclusive lock on the database before we try to read anything
  // from it.  If we don't take this lock until the beginning of the
  // "migrating data" phase, two simultaneous "db migrate" processes could
  // race through the "calculating migration" phase; then one of them would
  // wait for the other to finish all the migration steps, and trip over the
  // invariant check inside the for loop.
  {
    transaction guard(db);

    P(F("calculating migration..."));

    migration_event const *m; MM(m);
    schema_mismatch_case cat; MM(cat);
    m = find_migration(db);
    cat = classify_schema(db, m);

    diagnose_unrecognized_schema(cat, filename);

    // We really want 'db migrate' on an up-to-date schema to be a no-op
    // (no vacuum or anything, even), so that automated scripts can fire
    // one off optimistically and not have to worry about getting their
    // administrators to do it by hand.
    if (cat == SCHEMA_MATCHES)
      {
        P(F("no migration performed; database schema already up-to-date"));
        return;
      }

    sql::create_function(db, "sha1", sqlite_sha1_fn);
    sql::create_function(db, "unbase64", sqlite3_unbase64_fn);
    sql::create_function(db, "unhex", sqlite3_unhex_fn);

    P(F("migrating data..."));

    for (;;)
      {
        // confirm that we are where we ought to be
        string id; MM(id);
        calculate_schema_id(db, id);

        I(id == m->id);
        I(!m->migrator_sql || !m->migrator_func);

        if (m->migrator_sql)
          sql::exec(db, m->migrator_sql);
        else if (m->migrator_func)
          m->migrator_func(db, keys);
        else
          break;

        regime = std::min(regime, m->regime);

        m++;
        I(m < migration_events + n_migration_events);
        P(F("migrated to schema %s") % m->id);
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
test_migration_step(sqlite3 * db, system_path const & filename,
                    key_store & keys, string const & schema)
{
  I(db != NULL);
  sql::create_function(db, "sha1", sqlite_sha1_fn);
  sql::create_function(db, "unbase64", sqlite3_unbase64_fn);

  transaction guard(db);

  migration_event const *m;
  for (m = migration_events + n_migration_events - 1;
       m >= migration_events; m--)
    if (schema == m->id)
      break;

  N(m >= migration_events,
    F("cannot test migration from unknown schema %s") % schema);

  N(m->migrator_sql || m->migrator_func,
    F("schema %s is up to date") % schema);

  L(FL("testing migration from %s to %s\n in database %s")
    % schema % m[1].id % filename);

  if (m->migrator_sql)
    sql::exec(db, m->migrator_sql);
  else
    m->migrator_func(db, keys);

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
