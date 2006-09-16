GOPT(args, "", std::vector<utf8>, true, "")
#ifdef option_bodies
{
  args.push_back(utf8(arg));
}
#endif

COPT(author, "author", std::string, true,
     gettext_noop("override author for commit"))
#ifdef option_bodies
{
  author = arg;
}
#endif

COPT(automate_stdio_size, "automate-stdio-size", long, true,
     gettext_noop("block size in bytes for \"automate stdio\" output"))
#ifdef option_bodies
{
  automate_stdio_size = boost::lexical_cast<long>(arg);
}
#endif

COPT(bind, "bind", std::string, true,
     gettext_noop("address:port to listen on (default :4691)"))
#ifdef option_bodies
{
  bind = arg;
}
#endif

COPT(branch_name, "branch,b", std::string, true,
     gettext_noop("select branch cert for operation"))
#ifdef option_bodies
{
  branch_name = arg;
}
#endif

COPT(brief, "brief", bool, false,
     gettext_noop("print a brief version of the normal output"))
#ifdef option_bodies
{
  brief = true;
}
#endif

GOPT(conf_dir, "confdir", system_path, true,
     gettext_noop("set location of configuration directory"))
#ifdef option_bodies
{
  conf_dir = system_path(arg);
  if (!key_dir_given)
    key_dir = (conf_dir / "keys");
}
#endif

COPT(context_diff, "context", bool, false,
     gettext_noop("use context diff format"))
#ifdef option_bodies
{
  context_diff = true;
}
#endif

COPT(date, "date", std::string, true,
     gettext_noop("override date/time for commit"))
#ifdef option_bodies
{
  date = arg;
}
#endif

GOPT(dbname, "db,d", system_path, true, gettext_noop("set name of database"))
#ifdef option_bodies
{
  dbname = system_path(arg);
}
#endif

GOPT(debug, "debug", bool, false,
     gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  debug = true;
  global_sanity.set_debug();
}
#endif

COPT(depth, "depth", long, true,
     gettext_noop("limit the number of levels of directories to descend"))
#ifdef option_bodies
{
  depth = boost::lexical_cast<long>(arg);
}
#endif

COPT(external_diff_args, "diff-args", std::string, true,
     gettext_noop("argument to pass external diff hook"))
#ifdef option_bodies
{
  external_diff_args = arg;
}
#endif

COPT(diffs, "diffs", bool, false, gettext_noop("print diffs along with logs"))
#ifdef option_bodies
{
  diffs = true;
}
#endif

COPT(drop_attr, "drop-attr", std::vector<std::string>, true,
     gettext_noop("when rosterifying, drop attrs entries with the given key"))
#ifdef option_bodies
{
  drop_attr.push_back(arg);
}
#endif

GOPT(dump, "dump", system_path, true,
     gettext_noop("file to dump debugging log to, on failure"))
#ifdef option_bodies
{
  dump = system_path(arg);
  global_sanity.filename = system_path(dump).as_external();
}
#endif

COPT(exclude, "exclude", std::vector<std::string>, true,
     gettext_noop("leave out anything described by its argument"))
#ifdef option_bodies
{
  exclude.push_back(arg);
}
#endif

COPT(execute, "execute,e", bool, false,
     gettext_noop("perform the associated file operation"))
#ifdef option_bodies
{
  execute = true;
}
#endif

COPT(external_diff, "external", bool, false,
     gettext_noop("use external diff hook for generating diffs"))
#ifdef option_bodies
{
  external_diff = true;
}
#endif

GOPT(full_version, "full-version", bool, false,
     gettext_noop("print detailed version number, then exit"))
#ifdef option_bodies
{
  full_version = true;
}
#endif

GOPT(help, "help,h", bool, false,
     gettext_noop("display help message"))
#ifdef option_bodies
{
  help = true;
}
#endif

GOPT(signing_key, "key,k", rsa_keypair_id, true,
     gettext_noop("set key for signatures"))
#ifdef option_bodies
{
  internalize_rsa_keypair_id(utf8(arg), signing_key);
}
#endif

GOPT(key_dir, "keydir", system_path, true,
     gettext_noop("set location of key store"))
#ifdef option_bodies
{
  key_dir = system_path(arg);
}
#endif

COPT(key_to_push, "key-to-push", std::vector<std::string>, true,
     gettext_noop("push the specified key even if it hasn't signed anything"))
#ifdef option_bodies
{
  key_to_push.push_back(arg);
}
#endif

COPT(last, "last", long, true,
     gettext_noop("limit log output to the last number of entries"))
#ifdef option_bodies
{
  last = boost::lexical_cast<long>(arg);
}
#endif

GOPT(log, "log", system_path, true,
     gettext_noop("file to write the log to"))
#ifdef option_bodies
{
  log = system_path(arg);
  ui.redirect_log_to(log);
}
#endif

COPT(message, "message,m", std::string, true,
     gettext_noop("set commit changelog message"))
#ifdef option_bodies
{
  message = arg;
}
#endif

COPT(msgfile, "message-file", system_path, true,
     gettext_noop("set filename containing commit changelog message"))
#ifdef option_bodies
{
  msgfile = system_path(arg);
}
#endif

COPT(missing, "missing", bool, false,
     gettext_noop("perform the operations for files missing from workspace"))
#ifdef option_bodies
{
  missing = true;
}
#endif

COPT(next, "next", long, true,
     gettext_noop("limit log output to the next number of entries"))
#ifdef option_bodies
{
  next = boost::lexical_cast<long>(arg);
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

COPT(no_show_encloser, "no-show-encloser", bool, false,
     gettext_noop("do not show the function containing each block of changes"))
#ifdef option_bodies
{
  no_show_encloser = true;
}
#endif

GOPT(nostd, "nostd", bool, false,
     gettext_noop("do not load standard lua hooks"))
#ifdef option_bodies
{
  nostd = true;
}
#endif

COPT(no_transport_auth, "no-transport-auth", bool, false,
     gettext_noop("disable transport authentication"))
#ifdef option_bodies
{
  no_transport_auth = true;
}
#endif

COPT(pidfile, "pid-file", system_path, true,
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

GOPT(rcfile, "rcfile", std::vector<std::string>, true,
     gettext_noop("load extra rc file"))
#ifdef option_bodies
{
  rcfile.push_back(arg);
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

COPT(revision, "revision,r", std::vector<std::string>, true,
     gettext_noop("select revision id for operation"))
#ifdef option_bodies
{
  revision.push_back(arg);
}
#endif

GOPT(root, "root", std::string, true,
     gettext_noop("limit search for workspace to specified root"))
#ifdef option_bodies
{
  root = arg;
}
#endif

COPT(set_default, "set-default", bool, false,
     gettext_noop("use the current arguments as the future default"))
#ifdef option_bodies
{
  set_default = true;
}
#endif

COPT(stdio, "stdio", bool, false, gettext_noop("serve netsync on stdio"))
#ifdef option_bodies
{
  stdio = true;
}
#endif

GOPT(ticker, "ticker", std::string, true,
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
    help = true;
}
#endif

COPT(unified_diff, "unified", bool, false,
     gettext_noop("use unified diff format"))
#ifdef option_bodies
{
  unified_diff = true;
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

GOPT(argfile, "xargs,@", std::vector<std::string>, true,
     gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
  argfile.push_back(arg);
}
#endif
