#include "base.hh"
#include "cmd.hh"

#include "diff_patch.hh"
#include "netcmd.hh"
#include "globish.hh"
#include "keys.hh"
#include "key_store.hh"
#include "cert.hh"
#include "revision.hh"
#include "ui.hh"
#include "uri.hh"
#include "vocab_cast.hh"
#include "platform-wrapped.hh"
#include "app_state.hh"
#include "project.hh"
#include "work.hh"

#include <fstream>

using std::ifstream;
using std::ofstream;
using std::map;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

static const var_key default_server_key(var_domain("database"),
                                        var_name("default-server"));
static const var_key default_include_pattern_key(var_domain("database"),
                                                 var_name("default-include-pattern"));
static const var_key default_exclude_pattern_key(var_domain("database"),
                                                 var_name("default-exclude-pattern"));

static char const ws_internal_db_file_name[] = "mtn.db";

static void
extract_address(options & opts, database & db,
                args_vector const & args, utf8 & addr)
{
  if (args.size() >= 1)
    {
      addr = idx(args, 0);
      if (!db.var_exists(default_server_key) || opts.set_default)
        {
          P(F("setting default server to %s") % addr());
          db.set_var(default_server_key, var_value(addr()));
        }
    }
  else
    {
      N(db.var_exists(default_server_key),
        F("no server given and no default server set"));
      var_value addr_value;
      db.get_var(default_server_key, addr_value);
      addr = utf8(addr_value());
      L(FL("using default server address: %s") % addr());
    }
}

static void
find_key(options & opts,
         lua_hooks & lua,
         database & db,
         key_store & keys,
         utf8 const & addr,
         globish const & include,
         globish const & exclude,
         bool needed = true)
{
  if (!opts.signing_key().empty())
    return;

  rsa_keypair_id key;
  uri u;
  utf8 host(addr);

  parse_uri(addr(), u);
  if (!u.host.empty())
    host = utf8(u.host);
  if (needed
      && (key().empty()
          || !lua.hook_get_netsync_key(host, include, exclude, key)))
    get_user_key(opts, lua, db, keys, key);

  opts.signing_key = key;
}

static void
find_key_if_needed(options & opts,
                   lua_hooks & lua,
                   database & db,
                   key_store & keys,
                   utf8 const & addr,
                   globish const & include,
                   globish const & exclude,
                   bool needed = true)
{
  uri u;
  parse_uri(addr(), u);

  if (lua.hook_use_transport_auth(u))
    find_key(opts, lua, db, keys, addr, include, exclude, needed);
}

static void
extract_patterns(options & opts, database & db, args_vector const & args,
                 globish & include_pattern, globish & exclude_pattern)
{
  if (args.size() >= 2 || opts.exclude_given)
    {
      E(args.size() >= 2, F("no branch pattern given"));

      include_pattern = globish(args.begin() + 1, args.end());
      exclude_pattern = globish(opts.exclude_patterns);

      if (!db.var_exists(default_include_pattern_key)
          || opts.set_default)
        {
          P(F("setting default branch include pattern to '%s'") % include_pattern);
          db.set_var(default_include_pattern_key, var_value(include_pattern()));
        }
      if (!db.var_exists(default_exclude_pattern_key)
          || opts.set_default)
        {
          P(F("setting default branch exclude pattern to '%s'") % exclude_pattern);
          db.set_var(default_exclude_pattern_key, var_value(exclude_pattern()));
        }
    }
  else
    {
      N(db.var_exists(default_include_pattern_key),
        F("no branch pattern given and no default pattern set"));
      var_value pattern_value;
      db.get_var(default_include_pattern_key, pattern_value);
      include_pattern = globish(pattern_value());
      L(FL("using default branch include pattern: '%s'") % include_pattern);
      if (db.var_exists(default_exclude_pattern_key))
        {
          db.get_var(default_exclude_pattern_key, pattern_value);
          exclude_pattern = globish(pattern_value());
        }
      else
        exclude_pattern = globish();
      L(FL("excluding: %s") % exclude_pattern);
    }
}

CMD(push, "push", "", CMD_REF(network),
    N_("[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Pushes branches to a netsync server"),
    N_("This will push all branches that match the pattern given in PATTERN "
       "to the netsync server at the address ADDRESS."),
    options::opts::set_default | options::opts::exclude |
    options::opts::key_to_push)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(app.opts, db, args, addr);
  extract_patterns(app.opts, db, args, include_pattern, exclude_pattern);
  find_key_if_needed(app.opts, app.lua, db, keys,
                     addr, include_pattern, exclude_pattern);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(app.opts, app.lua, project, keys,
                       client_voice, source_role, uris,
                       include_pattern, exclude_pattern);
}

CMD(pull, "pull", "", CMD_REF(network),
    N_("[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Pulls branches from a netsync server"),
    N_("This pulls all branches that match the pattern given in PATTERN "
       "from the netsync server at the address ADDRESS."),
    options::opts::set_default | options::opts::exclude)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(app.opts, db, args, addr);
  extract_patterns(app.opts, db, args, include_pattern, exclude_pattern);
  find_key_if_needed(app.opts, app.lua, db, keys,
                     addr, include_pattern, exclude_pattern, false);

  if (app.opts.signing_key() == "")
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(app.opts, app.lua, project, keys,
                       client_voice, sink_role, uris,
                       include_pattern, exclude_pattern);
}

CMD(sync, "sync", "", CMD_REF(network),
    N_("[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Synchronizes branches with a netsync server"),
    N_("This synchronizes branches that match the pattern given in PATTERN "
       "with the netsync server at the address ADDRESS."),
    options::opts::set_default | options::opts::exclude |
    options::opts::key_to_push)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(app.opts, db, args, addr);
  extract_patterns(app.opts, db, args, include_pattern, exclude_pattern);
  find_key_if_needed(app.opts, app.lua, db, keys,
                     addr, include_pattern, exclude_pattern, false);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(app.opts, app.lua, project, keys,
                       client_voice, source_and_sink_role, uris,
                       include_pattern, exclude_pattern);
}

class dir_cleanup_helper
{
public:
  dir_cleanup_helper(system_path const & new_dir, bool i_db) :
                  commited(false), internal_db(i_db), dir(new_dir) {}
  ~dir_cleanup_helper()
  {
    if (!commited && directory_exists(dir))
      {
#ifdef WIN32
        if (!internal_db)
          delete_dir_recursive(dir);
#else
        delete_dir_recursive(dir);
#endif /* WIN32 */
      }
  }
  void commit(void)
  {
    commited = true;
  }
private:
  bool commited;
  bool internal_db;
  system_path dir;
};

CMD(clone, "clone", "", CMD_REF(network),
    N_("ADDRESS[:PORTNUMBER] [DIRECTORY]"),
    N_("Checks out a revision from a remote database into a directory"),
    N_("If a revision is given, that's the one that will be checked out.  "
       "Otherwise, it will be the head of the branch supplied.  "
       "If no directory is given, the branch name will be used as directory"),
    options::opts::exclude | options::opts::branch | options::opts::revision)
{
  if (args.size() < 1 || args.size() > 2 || app.opts.revision_selectors.size() > 1)
    throw usage(execid);

  revision_id ident;
  system_path workspace_dir;
  utf8 addr = idx(args, 0);

  N(app.opts.branch_given && !app.opts.branchname().empty(),
    F("you must specify a branch to clone"));

  if (args.size() == 1)
    {
      // No checkout dir specified, use branch name for dir.
      workspace_dir = system_path(app.opts.branchname());
    }
  else
    {
      // Checkout to specified dir.
      workspace_dir = system_path(idx(args, 1));
    }

  require_path_is_nonexistent
    (workspace_dir, F("clone destination directory '%s' already exists") % workspace_dir);

  // remember the initial working dir so that relative file://
  // db URIs will work
  system_path start_dir(get_current_working_dir());

  bool internal_db = !app.opts.dbname_given || app.opts.dbname.empty();

  dir_cleanup_helper remove_on_fail(workspace_dir, internal_db);

  // paths.cc's idea of the current workspace root is wrong at this point
  if (internal_db)
    app.opts.dbname = system_path(workspace_dir
                                  / bookkeeping_root_component
                                  / ws_internal_db_file_name);

  // must do this after setting dbname so that _MTN/options is written
  // correctly
  workspace::create_workspace(app.opts, app.lua, workspace_dir);

  database db(app);
  if (get_path_status(db.get_filename()) == path::nonexistent)
    db.initialize();

  db.ensure_open();

  if (!db.var_exists(default_server_key) || app.opts.set_default)
    {
      P(F("setting default server to %s") % addr);
      db.set_var(default_server_key, var_value(addr()));
    }

  key_store keys(app);
  project_t project(db);
  globish include_pattern(app.opts.branchname());
  globish exclude_pattern(app.opts.exclude_patterns);

  find_key_if_needed(app.opts, app.lua, db, keys,
                     addr, include_pattern, exclude_pattern, false);

  if (app.opts.signing_key() == "")
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  if (!db.var_exists(default_include_pattern_key)
      || app.opts.set_default)
    {
      P(F("setting default branch include pattern to '%s'") % include_pattern);
      db.set_var(default_include_pattern_key, var_value(include_pattern()));
    }

  if (app.opts.exclude_given)
    {
      if (!db.var_exists(default_exclude_pattern_key)
          || app.opts.set_default)
        {
          P(F("setting default branch exclude pattern to '%s'") % exclude_pattern);
          db.set_var(default_exclude_pattern_key, var_value(exclude_pattern()));
        }
    }

  // make sure we're back in the original dir so that file: URIs work
  change_current_working_dir(start_dir);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(app.opts, app.lua, project, keys,
                       client_voice, sink_role, uris,
                       include_pattern, exclude_pattern);

  change_current_working_dir(workspace_dir);

  transaction_guard guard(db, false);

  if (app.opts.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.opts.branchname().empty(),
        F("use --revision or --branch to specify what to checkout"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branchname, heads,
                               app.opts.ignore_suspend_certs);
      N(heads.size() > 0,
        F("branch '%s' is empty") % app.opts.branchname);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branchname);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(project, *i));
          P(F("choose one with '%s checkout -r<id>'") % ui.prog_name);
          E(false, F("branch %s has multiple heads") % app.opts.branchname);
        }
      ident = *(heads.begin());
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app, project, idx(app.opts.revision_selectors, 0)(), ident);

      guess_branch(app.opts, project, ident);
      I(!app.opts.branchname().empty());

      N(project.revision_is_in_branch(ident, app.opts.branchname),
        F("revision %s is not a member of branch %s")
        % ident % app.opts.branchname);
    }

  shared_ptr<roster_t> empty_roster = shared_ptr<roster_t>(new roster_t());
  roster_t current_roster;

  L(FL("checking out revision %s to directory %s") % ident % workspace_dir);
  db.get_roster(ident, current_roster);

  workspace work(app);
  revision_t workrev;
  make_revision_for_workspace(ident, cset(), workrev);
  work.put_work_rev(workrev);

  cset checkout;
  make_cset(*empty_roster, current_roster, checkout);

  content_merge_checkout_adaptor wca(db);

  work.perform_content_update(db, checkout, wca, false);

  work.update_any_attrs(db);
  work.maybe_update_inodeprints(db);
  guard.commit();
  remove_on_fail.commit();
}

struct pid_file
{
  explicit pid_file(system_path const & p)
    : path(p)
  {
    if (path.empty())
      return;
    require_path_is_nonexistent(path, F("pid file '%s' already exists") % path);
    file.open(path.as_external().c_str());
    E(file.is_open(), F("failed to create pid file '%s'") % path);
    file << get_process_id() << '\n';
    file.flush();
  }

  ~pid_file()
  {
    if (path.empty())
      return;
    pid_t pid;
    ifstream(path.as_external().c_str()) >> pid;
    if (pid == get_process_id()) {
      file.close();
      delete_file(path);
    }
  }

private:
  ofstream file;
  system_path path;
};

CMD_NO_WORKSPACE(serve, "serve", "", CMD_REF(network), "",
                 N_("Serves the database to connecting clients"),
                 "",
                 options::opts::bind | options::opts::pidfile |
                 options::opts::bind_stdio | options::opts::no_transport_auth )
{
  if (!args.empty())
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db);
  pid_file pid(app.opts.pidfile);

  db.ensure_open();

  if (app.opts.use_transport_auth)
    {
      N(app.lua.hook_persist_phrase_ok(),
	F("need permission to store persistent passphrase "
          "(see hook persist_phrase_ok())"));

      if (!app.opts.bind_uris.empty())
        find_key(app.opts, app.lua, db, keys,
                 *app.opts.bind_uris.begin(), globish("*"), globish(""));
      else
        find_key(app.opts, app.lua, db, keys,
                 utf8(), globish("*"), globish(""));
    }
  else if (!app.opts.bind_stdio)
    W(F("The --no-transport-auth option is usually only used "
        "in combination with --stdio"));

  run_netsync_protocol(app.opts, app.lua, project, keys,
                       server_voice, source_and_sink_role, app.opts.bind_uris,
                       globish("*"), globish(""));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
