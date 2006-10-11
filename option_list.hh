#define COPT(name, string, type, default_, description) \
  COPTVAR(type, name, default_)                         \
  COPTION(name, name, has_arg<type>(), string, description)
#define GOPT(name, string, type, default_, description) \
  GOPTVAR(type, name, default_)                         \
  GOPTION(name, name, has_arg<type>(), string, description)

GOPT(args, "", std::vector<utf8>, , "")
#ifdef option_bodies
{
  args.push_back(utf8(arg));
}
#endif

COPT(author, "author", utf8, , gettext_noop("override author for commit"))
#ifdef option_bodies
{
  author = arg;
}
#endif

COPT(automate_stdio_size, "automate-stdio-size", size_t, 1024,
     gettext_noop("block size in bytes for \"automate stdio\" output"))
#ifdef option_bodies
{
  automate_stdio_size = boost::lexical_cast<long>(arg);
  if (automate_stdio_size <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

COPT(bind, "bind", bind_opt, ,
     gettext_noop("address:port to listen on (default :4691)"))
#ifdef option_bodies
{
  bind.set(arg);
}
#endif
COPT(no_transport_auth, "no-transport-auth", bool, false,
     gettext_noop("disable transport authentication"))
#ifdef option_bodies
{
  no_transport_auth = true;
}
#endif
COPT(bind_stdio, "stdio", bool, false, gettext_noop("serve netsync on stdio"))
#ifdef option_bodies
{
  // Yes, this sets a field in the structure belonging to the "bind" option.
  bind.stdio = true;
}
#endif

COPTVAR(utf8, branch_name, )
COPTION(branch, branch, true, "branch,b",
        gettext_noop("select branch cert for operation"))
#ifdef option_bodies
{
  branch_name = utf8(arg);
}
#endif

COPT(brief, "brief", bool, false,
     gettext_noop("print a brief version of the normal output"))
#ifdef option_bodies
{
  brief = true;
}
#endif

GOPT(conf_dir, "confdir", system_path, get_default_confdir(),
     gettext_noop("set location of configuration directory"))
#ifdef option_bodies
{
  conf_dir = system_path(arg);
  if (!key_dir_given)
    key_dir = (conf_dir / "keys");
}
#endif

COPT(date, "date", boost::posix_time::ptime, ,
     gettext_noop("override date/time for commit"))
#ifdef option_bodies
{
  try
    {
      // boost::posix_time can parse "basic" ISO times, of the form
      // 20000101T120000, but not "extended" ISO times, of the form
      // 2000-01-01T12:00:00. So convert one to the other.
      string tmp = arg;
      string::size_type pos = 0;
      while ((pos = tmp.find_first_of("-:")) != string::npos)
        tmp.erase(pos, 1);
      date = boost::posix_time::from_iso_string(tmp);
    }
  catch (std::exception &e)
    {
      throw bad_arg_internal(e.what());
    }
}
#endif

GOPT(dbname, "db,d", system_path, , gettext_noop("set name of database"))
#ifdef option_bodies
{
  dbname = system_path(arg);
}
#endif

GOPTION(debug, debug, false, "debug",
        gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  global_sanity.set_debug();
}
#endif

COPT(depth, "depth", long, -1,
     gettext_noop("limit the number of levels of directories to descend"))
#ifdef option_bodies
{
  depth = boost::lexical_cast<long>(arg);
  if (depth < 0)
    throw bad_arg_internal(F("cannot be negative").str());
}
#endif


COPTSET(diff_options)

COPTVAR(std::string, external_diff_args, )
COPTION(diff_options, external_diff_args, true, "diff-args",
        gettext_noop("argument to pass external diff hook"))
#ifdef option_bodies
{
  external_diff_args = arg;
}
#endif

COPTVAR(diff_type, diff_format, unified_diff)
COPTION(diff_options, diff_context, false, "context",
        gettext_noop("use context diff format"))
#ifdef option_bodies
{
  diff_format = context_diff;
}
#endif
COPTION(diff_options, diff_external, false, "external",
        gettext_noop("use external diff hook for generating diffs"))
#ifdef option_bodies
{
  diff_format = external_diff;
}
#endif
COPTION(diff_options, diff_unified, false, "unified",
        gettext_noop("use unified diff format"))
#ifdef option_bodies
{
  diff_format = unified_diff;
}
#endif
COPTVAR(bool, no_show_encloser, false)
COPTION(diff_options, no_show_encloser, false, "no-show-encloser",
     gettext_noop("do not show the function containing each block of changes"))
#ifdef option_bodies
{
  no_show_encloser = true;
}
#endif

COPT(diffs, "diffs", bool, false, gettext_noop("print diffs along with logs"))
#ifdef option_bodies
{
  diffs = true;
}
#endif

COPTVAR(std::set<std::string>, attrs_to_drop, )
COPTION(drop_attr, drop_attr, true, "drop-attr",
        gettext_noop("when rosterifying, drop attrs entries with the given key"))
#ifdef option_bodies
{
  attrs_to_drop.insert(arg);
}
#endif

GOPTION(dump, dump, true, "dump",
        gettext_noop("file to dump debugging log to, on failure"))
#ifdef option_bodies
{
  global_sanity.filename = system_path(arg).as_external();
}
#endif

COPTVAR(std::vector<utf8>, exclude_patterns, )
COPTION(exclude, exclude, true, "exclude",
        gettext_noop("leave out anything described by its argument"))
#ifdef option_bodies
{
  exclude_patterns.push_back(utf8(arg));
}
#endif

COPT(execute, "execute,e", bool, false,
        gettext_noop("perform the associated file operation"))
#ifdef option_bodies
{
  execute = true;
}
#endif

GOPT(full_version, "full-version", bool, false,
     gettext_noop("print detailed version number, then exit"))
#ifdef option_bodies
{
  full_version = true;
}
#endif

GOPT(help, "help,h", bool, false, gettext_noop("display help message"))
#ifdef option_bodies
{
  help = true;
}
#endif

GOPTVAR(rsa_keypair_id, signing_key, )
GOPTION(key, key, true, "key,k", gettext_noop("set key for signatures"))
#ifdef option_bodies
{
  internalize_rsa_keypair_id(utf8(arg), signing_key);
}
#endif

GOPT(key_dir, "keydir", system_path, ,
     gettext_noop("set location of key store"))
#ifdef option_bodies
{
  key_dir = system_path(arg);
}
#endif

COPTVAR(std::vector<rsa_keypair_id>, keys_to_push, )
COPTION(key_to_push, key_to_push, true, "key-to-push",
        gettext_noop("push the specified key even if it hasn't signed anything"))
#ifdef option_bodies
{
  rsa_keypair_id keyid;
  internalize_rsa_keypair_id(utf8(arg), keyid);
  keys_to_push.push_back(keyid);
}
#endif

COPT(last, "last", long, -1,
     gettext_noop("limit log output to the last number of entries"))
#ifdef option_bodies
{
  last = boost::lexical_cast<long>(arg);
  if (last <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

GOPTION(log, log, true, "log", gettext_noop("file to write the log to"))
#ifdef option_bodies
{
  ui.redirect_log_to(system_path(arg));
}
#endif

COPTSET(messages)
COPTVAR(utf8, message, )
COPTVAR(utf8, msgfile, )
COPTION(messages, message, true, "message,m",
        gettext_noop("set commit changelog message"))
#ifdef option_bodies
{
  message = utf8(arg);
}
#endif
COPTION(messages, msgfile, true, "message-file",
        gettext_noop("set filename containing commit changelog message"))
#ifdef option_bodies
{
  msgfile = utf8(arg);
}
#endif

COPT(missing, "missing", bool, false,
     gettext_noop("perform the operations for files missing from workspace"))
#ifdef option_bodies
{
  missing = true;
}
#endif

COPT(next, "next", long, -1,
     gettext_noop("limit log output to the next number of entries"))
#ifdef option_bodies
{
  next = boost::lexical_cast<long>(arg);
  if (next <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

COPT(no_files, "no-files", bool, false,
     gettext_noop("exclude files when printing logs"))
#ifdef option_bodies
{
  no_files = true;
}
#endif

COPT(no_merges, "no-merges", bool, false,
     gettext_noop("exclude merges when printing logs"))
#ifdef option_bodies
{
  no_merges = true;
}
#endif

GOPT(norc, "norc", bool, false,
gettext_noop("do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"))
#ifdef option_bodies
{
  norc = true;
}
#endif

GOPT(nostd, "nostd", bool, false,
     gettext_noop("do not load standard lua hooks"))
#ifdef option_bodies
{
  nostd = true;
}
#endif

COPT(pidfile, "pid-file", system_path, ,
     gettext_noop("record process id of server"))
#ifdef option_bodies
{
  pidfile = system_path(arg);
}
#endif

GOPT(quiet, "quiet", bool, false,
     gettext_noop("suppress verbose, informational and progress messages"))
#ifdef option_bodies
{
  quiet = true;
  global_sanity.set_quiet();
  ui.set_tick_writer(new tick_write_nothing);
}
#endif

GOPT(extra_rcfiles, "rcfile", std::vector<utf8>, ,
     gettext_noop("load extra rc file"))
#ifdef option_bodies
{
  extra_rcfiles.push_back(utf8(arg));
}
#endif

GOPT(reallyquiet, "reallyquiet", bool, false,
gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  reallyquiet = true;
  global_sanity.set_reallyquiet();
  ui.set_tick_writer(new tick_write_nothing);
}
#endif

COPT(recursive, "recursive,R", bool, false,
     gettext_noop("also operate on the contents of any listed directories"))
#ifdef option_bodies
{
  recursive = true;
}
#endif

COPTVAR(std::vector<utf8>, revision_selectors, )
COPTION(revision, revision, true, "revision,r",
     gettext_noop("select revision id for operation"))
#ifdef option_bodies
{
  revision_selectors.push_back(arg);
}
#endif

GOPT(root, "root", system_path, current_root_path(),
     gettext_noop("limit search for workspace to specified root"))
#ifdef option_bodies
{
  root = system_path(arg);
}
#endif

COPT(set_default, "set-default", bool, false,
     gettext_noop("use the current arguments as the future default"))
#ifdef option_bodies
{
  set_default = true;
}
#endif

GOPT(ticker, "ticker", std::string, ,
     gettext_noop("set ticker style (count|dot|none)"))
#ifdef option_bodies
{
  ticker = arg;
  if (ticker == "none" || global_sanity.quiet)
    ui.set_tick_writer(new tick_write_nothing);
  else if (ticker == "dot")
    ui.set_tick_writer(new tick_write_dot);
  else if (ticker == "count")
    ui.set_tick_writer(new tick_write_count);
  else
    throw bad_arg_internal(F("argument must be 'none', 'dot', or 'count'").str());
}
#endif

COPT(unknown, "unknown", bool, false,
     gettext_noop("perform the operations for unknown files from workspace"))
#ifdef option_bodies
{
  unknown = true;
}
#endif

GOPT(verbose, "verbose", bool, false,
     gettext_noop("verbose completion output"))
#ifdef option_bodies
{
  verbose = true;
}
#endif

GOPT(version, "version", bool, false,
     gettext_noop("print version number, then exit"))
#ifdef option_bodies
{
  version = true;
}
#endif

GOPT(argfile, "xargs,@", std::vector<std::string>, ,
     gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
  argfile.push_back(arg);
}
#endif
