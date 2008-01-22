#include "base.hh"
#include "cmd.hh"

#include "diff_patch.hh"
#include "netcmd.hh"
#include "globish.hh"
#include "keys.hh"
#include "cert.hh"
#include "revision.hh"
#include "ui.hh"
#include "uri.hh"
#include "vocab_cast.hh"
#include "platform-wrapped.hh"
#include "app_state.hh"

#include <fstream>

using std::ifstream;
using std::ofstream;
using std::map;
using std::set;
using std::string;
using std::vector;

static const var_key default_server_key(var_domain("database"),
                                        var_name("default-server"));
static const var_key default_include_pattern_key(var_domain("database"),
                                                 var_name("default-include-pattern"));
static const var_key default_exclude_pattern_key(var_domain("database"),
                                                 var_name("default-exclude-pattern"));

static char const ws_internal_db_file_name[] = "mtn.db";

static void
extract_address(args_vector const & args,
                utf8 & addr,
                app_state & app)
{
  if (args.size() >= 1)
    {
      addr = idx(args, 0);
      if (!app.db.var_exists(default_server_key) || app.opts.set_default)
        {
          P(F("setting default server to %s") % addr());
          app.db.set_var(default_server_key, var_value(addr()));
        }
    }
  else
    {
      N(app.db.var_exists(default_server_key),
        F("no server given and no default server set"));
      var_value addr_value;
      app.db.get_var(default_server_key, addr_value);
      addr = utf8(addr_value());
      L(FL("using default server address: %s") % addr());
    }
}

static void
find_key(utf8 const & addr,
         globish const & include,
         globish const & exclude,
         app_state & app,
         bool needed = true)
{
  if (app.opts.signing_key() != "")
    return;

  rsa_keypair_id key;
  uri u;
  utf8 host(addr);

  parse_uri(addr(), u);
  if (!u.host.empty())
    host = utf8(u.host);
  if (!app.lua.hook_get_netsync_key(host, include, exclude, key)
      || key() == "")
    {
      if (needed)
        {
          get_user_key(key, app.db);
        }
    }
  app.opts.signing_key = key;
}

static void
find_key_if_needed(utf8 const & addr,
                   globish const & include,
                   globish const & exclude,
                   app_state & app,
                   bool needed = true)
{
      uri u;
      parse_uri(addr(), u);

      if (app.lua.hook_use_transport_auth(u))
        {
          find_key(addr, include, exclude, app, needed);
        }
}

static void
extract_patterns(args_vector const & args,
                 globish & include_pattern, globish & exclude_pattern,
                 app_state & app)
{
  if (args.size() >= 2 || app.opts.exclude_given)
    {
      E(args.size() >= 2, F("no branch pattern given"));

      include_pattern = globish(args.begin() + 1, args.end());
      exclude_pattern = globish(app.opts.exclude_patterns);

      if (!app.db.var_exists(default_include_pattern_key)
          || app.opts.set_default)
        {
          P(F("setting default branch include pattern to '%s'") % include_pattern);
          app.db.set_var(default_include_pattern_key, var_value(include_pattern()));
        }
      if (!app.db.var_exists(default_exclude_pattern_key)
          || app.opts.set_default)
        {
          P(F("setting default branch exclude pattern to '%s'") % exclude_pattern);
          app.db.set_var(default_exclude_pattern_key, var_value(exclude_pattern()));
        }
    }
  else
    {
      N(app.db.var_exists(default_include_pattern_key),
        F("no branch pattern given and no default pattern set"));
      var_value pattern_value;
      app.db.get_var(default_include_pattern_key, pattern_value);
      include_pattern = globish(pattern_value());
      L(FL("using default branch include pattern: '%s'") % include_pattern);
      if (app.db.var_exists(default_exclude_pattern_key))
        {
          app.db.get_var(default_exclude_pattern_key, pattern_value);
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
  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(args, addr, app);
  extract_patterns(args, include_pattern, exclude_pattern, app);
  find_key_if_needed(addr, include_pattern, exclude_pattern, app);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(client_voice, source_role, uris,
                       include_pattern, exclude_pattern,
                       app.db, app.get_project(), app.keys, app.lua, app.opts);
}

CMD(pull, "pull", "", CMD_REF(network),
    N_("[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Pulls branches from a netsync server"),
    N_("This pulls all branches that match the pattern given in PATTERN "
       "from the netsync server at the address ADDRESS."),
    options::opts::set_default | options::opts::exclude)
{
  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(args, addr, app);
  extract_patterns(args, include_pattern, exclude_pattern, app);
  find_key_if_needed(addr, include_pattern, exclude_pattern, app, false);

  if (app.opts.signing_key() == "")
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(client_voice, sink_role, uris,
                       include_pattern, exclude_pattern,
                       app.db, app.get_project(), app.keys, app.lua, app.opts);
}

CMD(sync, "sync", "", CMD_REF(network),
    N_("[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Synchronizes branches with a netsync server"),
    N_("This synchronizes branches that match the pattern given in PATTERN "
       "with the netsync server at the address ADDRESS."),
    options::opts::set_default | options::opts::exclude |
    options::opts::key_to_push)
{
  utf8 addr;
  globish include_pattern, exclude_pattern;
  extract_address(args, addr, app);
  extract_patterns(args, include_pattern, exclude_pattern, app);
  find_key_if_needed(addr, include_pattern, exclude_pattern, app);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(client_voice, source_and_sink_role, uris,
                       include_pattern, exclude_pattern,
                       app.db, app.get_project(), app.keys, app.lua, app.opts);
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

   // remember the initial working dir so that relative file:// db URIs will work
  system_path start_dir(get_current_working_dir());

  bool internal_db = !app.opts.dbname_given || app.opts.dbname.empty();

  dir_cleanup_helper remove_on_fail(workspace_dir, internal_db);
  app.create_workspace(workspace_dir);

  if (internal_db)
    app.set_database(system_path(bookkeeping_root / ws_internal_db_file_name));
  else
    app.set_database(app.opts.dbname);

  if (get_path_status(app.db.get_filename()) == path::nonexistent)
    app.db.initialize();

  app.db.ensure_open();

  if (!app.db.var_exists(default_server_key) || app.opts.set_default)
    {
      P(F("setting default server to %s") % addr);
      app.db.set_var(default_server_key, var_value(addr()));
    }

  globish include_pattern(app.opts.branchname());
  globish exclude_pattern(app.opts.exclude_patterns);

  find_key_if_needed(addr, include_pattern, exclude_pattern,
                     app, false);

  if (app.opts.signing_key() == "")
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  if (!app.db.var_exists(default_include_pattern_key)
      || app.opts.set_default)
    {
      P(F("setting default branch include pattern to '%s'") % include_pattern);
      app.db.set_var(default_include_pattern_key, var_value(include_pattern()));
    }

  if (app.opts.exclude_given)
    {
      if (!app.db.var_exists(default_exclude_pattern_key)
          || app.opts.set_default)
        {
          P(F("setting default branch exclude pattern to '%s'") % exclude_pattern);
          app.db.set_var(default_exclude_pattern_key, var_value(exclude_pattern()));
        }
    }

  // make sure we're back in the original dir so that file: URIs work
  change_current_working_dir(start_dir);

  std::list<utf8> uris;
  uris.push_back(addr);

  run_netsync_protocol(client_voice, sink_role, uris,
                       include_pattern, exclude_pattern,
                       app.db, app.get_project(), app.keys, app.lua, app.opts);

  change_current_working_dir(workspace_dir);

  transaction_guard guard(app.db, false);

  if (app.opts.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.opts.branchname().empty(),
        F("use --revision or --branch to specify what to checkout"));

      set<revision_id> heads;
      app.get_project().get_branch_heads(app.opts.branchname, heads);
      N(heads.size() > 0,
        F("branch '%s' is empty") % app.opts.branchname);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branchname);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(app.db, app.get_project(), *i));
          P(F("choose one with '%s checkout -r<id>'") % ui.prog_name);
          E(false, F("branch %s has multiple heads") % app.opts.branchname);
        }
      ident = *(heads.begin());
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app, idx(app.opts.revision_selectors, 0)(), ident);

      guess_branch(ident, app.db, app.get_project());
      I(!app.opts.branchname().empty());

      N(app.get_project().revision_is_in_branch(ident, app.opts.branchname),
        F("revision %s is not a member of branch %s")
        % ident % app.opts.branchname);
    }

  shared_ptr<roster_t> empty_roster = shared_ptr<roster_t>(new roster_t());
  roster_t current_roster;

  L(FL("checking out revision %s to directory %s") % ident % workspace_dir);
  app.db.get_roster(ident, current_roster);

  revision_t workrev;
  make_revision_for_workspace(ident, cset(), workrev);
  app.work.put_work_rev(workrev);

  cset checkout;
  make_cset(*empty_roster, current_roster, checkout);

  content_merge_checkout_adaptor wca(app.db);

  app.work.perform_content_update(checkout, wca, false);

  app.work.update_any_attrs();
  app.work.maybe_update_inodeprints();
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

  pid_file pid(app.opts.pidfile);

  if (app.opts.use_transport_auth)
    {
      if (!app.opts.bind_uris.empty())
        find_key(*app.opts.bind_uris.begin(), globish("*"), globish(""), app);
      else
        find_key(utf8(), globish("*"), globish(""), app);

      N(app.lua.hook_persist_phrase_ok(),
	F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
      require_password(app.opts.signing_key, app.keys);
    }
  else if (!app.opts.bind_stdio)
    W(F("The --no-transport-auth option is usually only used in combination with --stdio"));

  app.db.ensure_open();

  run_netsync_protocol(server_voice, source_and_sink_role, app.opts.bind_uris,
                       globish("*"), globish(""),
                       app.db, app.get_project(), app.keys, app.lua, app.opts);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
