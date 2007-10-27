#define OPT(name, string, type, default_, description)			\
  OPTVAR(name, type, name, default_)					\
  OPTION(name, name, has_arg<type >(), string, description)

#define GOPT(name, string, type, default_, description)			\
  OPTVAR(globals, type, name, default_)					\
  OPTION(globals, name, has_arg<type >(), string, description)

OPTSET(globals)

OPTVAR(globals, args_vector, args, )
OPTION(globals, positionals, true, "--", "")
#ifdef option_bodies
{
  args.push_back(arg_type(arg));
}
#endif

OPT(author, "author", utf8, , gettext_noop("override author for commit"))
#ifdef option_bodies
{
  author = utf8(arg);
}
#endif

OPT(automate_stdio_size, "automate-stdio-size", size_t, 32768,
     gettext_noop("block size in bytes for \"automate stdio\" output"))
#ifdef option_bodies
{
  automate_stdio_size = boost::lexical_cast<long>(arg);
  if (automate_stdio_size <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPTSET(bind_opts)
OPTVAR(bind_opts, std::list<utf8>, bind_uris, )
OPTVAR(bind_opts, bool, bind_stdio, false)
OPTVAR(bind_opts, bool, use_transport_auth, true)

OPTION(bind_opts, bind, true, "bind",
       gettext_noop("address:port to listen on (default :4691)"))
#ifdef option_bodies
{
  bind_uris.push_back(utf8(arg));
  bind_stdio = false;
}
#endif
OPTION(bind_opts, no_transport_auth, false, "no-transport-auth",
       gettext_noop("disable transport authentication"))
#ifdef option_bodies
{
  use_transport_auth = false;
}
#endif
OPTION(bind_opts, bind_stdio, false, "stdio",
       gettext_noop("serve netsync on stdio"))
#ifdef option_bodies
{
  bind_stdio = true;
}
#endif

OPTVAR(branch, branch_name, branchname, )
OPTION(branch, branch, true, "branch,b",
        gettext_noop("select branch cert for operation"))
#ifdef option_bodies
{
  branchname = branch_name(arg);
}
#endif

OPT(brief, "brief", bool, false,
     gettext_noop("print a brief version of the normal output"))
#ifdef option_bodies
{
  brief = true;
}
#endif

OPT(revs_only, "revs-only", bool, false,
     gettext_noop("annotate using full revision ids only"))
#ifdef option_bodies
{
  revs_only = true;
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

OPT(date, "date", date_t, ,
     gettext_noop("override date/time for commit"))
#ifdef option_bodies
{
  try
    {
      date = date_t::from_string(arg);
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

OPTION(globals, debug, false, "debug",
        gettext_noop("print debug log to stderr while running"))
#ifdef option_bodies
{
  global_sanity.set_debug();
}
#endif

OPT(depth, "depth", long, -1,
     gettext_noop("limit the number of levels of directories to descend"))
#ifdef option_bodies
{
  depth = boost::lexical_cast<long>(arg);
  if (depth < 0)
    throw bad_arg_internal(F("cannot be negative").str());
}
#endif


OPTSET(diff_options)

OPTVAR(diff_options, std::string, external_diff_args, )
OPTION(diff_options, external_diff_args, true, "diff-args",
        gettext_noop("argument to pass external diff hook"))
#ifdef option_bodies
{
  external_diff_args = arg;
}
#endif

OPTVAR(diff_options, diff_type, diff_format, unified_diff)
OPTION(diff_options, diff_context, false, "context",
        gettext_noop("use context diff format"))
#ifdef option_bodies
{
  diff_format = context_diff;
}
#endif
OPTION(diff_options, diff_external, false, "external",
        gettext_noop("use external diff hook for generating diffs"))
#ifdef option_bodies
{
  diff_format = external_diff;
}
#endif
OPTION(diff_options, diff_unified, false, "unified",
        gettext_noop("use unified diff format"))
#ifdef option_bodies
{
  diff_format = unified_diff;
}
#endif
OPTVAR(diff_options, bool, no_show_encloser, false)
OPTION(diff_options, no_show_encloser, false, "no-show-encloser",
     gettext_noop("do not show the function containing each block of changes"))
#ifdef option_bodies
{
  no_show_encloser = true;
}
#endif

OPT(diffs, "diffs", bool, false, gettext_noop("print diffs along with logs"))
#ifdef option_bodies
{
  diffs = true;
}
#endif

OPTVAR(drop_attr, std::set<std::string>, attrs_to_drop, )
OPTION(drop_attr, drop_attr, true, "drop-attr",
        gettext_noop("when rosterifying, drop attrs entries with the given key"))
#ifdef option_bodies
{
  attrs_to_drop.insert(arg);
}
#endif

OPT(dryrun, "dry-run", bool, false,
     gettext_noop("don't perform the operation, just show what would have happened"))
#ifdef option_bodies
{
  dryrun = true;
}
#endif

OPTION(globals, dump, true, "dump",
        gettext_noop("file to dump debugging log to, on failure"))
#ifdef option_bodies
{
  global_sanity.set_dump_path(system_path(arg).as_external());
}
#endif

OPTVAR(exclude, args_vector, exclude_patterns, )
OPTION(exclude, exclude, true, "exclude",
        gettext_noop("leave out anything described by its argument"))
#ifdef option_bodies
{
  exclude_patterns.push_back(arg_type(arg));
}
#endif

OPT(bookkeep_only, "bookkeep-only", bool, false,
        gettext_noop("only update monotone's internal bookkeeping, not the filesystem"))
#ifdef option_bodies
{
  bookkeep_only = true;
}
#endif

GOPT(ssh_sign, "ssh-sign", std::string, "yes",
     gettext_noop("sign with ssh-agent, 'yes' to sign with ssh if key found, 'no' to force monotone to sign, 'check' to sign with both and compare"))
#ifdef option_bodies
{
  ssh_sign = arg;
}
#endif

OPT(full, "full", bool, false,
     gettext_noop("print detailed version number"))
#ifdef option_bodies
{
  full = true;
}
#endif

GOPT(help, "help,h", bool, false, gettext_noop("display help message"))
#ifdef option_bodies
{
  help = true;
}
#endif

OPTVAR(include, args_vector, include_patterns, )
OPTION(include, include, true, "include",
        gettext_noop("include anything described by its argument"))
#ifdef option_bodies
{
  include_patterns.push_back(arg_type(arg));
}
#endif

GOPT(ignore_suspend_certs, "ignore-suspend-certs", bool, false,
     gettext_noop("do not ignore revisions marked as suspended"))
#ifdef option_bodies
{
  ignore_suspend_certs = true;
}
#endif


OPTVAR(key, rsa_keypair_id, signing_key, )
OPTION(globals, key, true, "key,k", gettext_noop("set key for signatures"))
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

OPTVAR(key_to_push, std::vector<rsa_keypair_id>, keys_to_push, )
OPTION(key_to_push, key_to_push, true, "key-to-push",
        gettext_noop("push the specified key even if it hasn't signed anything"))
#ifdef option_bodies
{
  rsa_keypair_id keyid;
  internalize_rsa_keypair_id(utf8(arg), keyid);
  keys_to_push.push_back(keyid);
}
#endif

OPT(last, "last", long, -1,
     gettext_noop("limit log output to the last number of entries"))
#ifdef option_bodies
{
  last = boost::lexical_cast<long>(arg);
  if (last <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPTION(globals, log, true, "log", gettext_noop("file to write the log to"))
#ifdef option_bodies
{
  ui.redirect_log_to(system_path(arg));
}
#endif

OPTSET(messages)
OPTVAR(messages, std::vector<std::string>, message, )
OPTVAR(messages, utf8, msgfile, )
OPTION(messages, message, true, "message,m",
        gettext_noop("set commit changelog message"))
#ifdef option_bodies
{
  message.push_back(arg);
}
#endif
OPTION(messages, msgfile, true, "message-file",
        gettext_noop("set filename containing commit changelog message"))
#ifdef option_bodies
{
  msgfile = utf8(arg);
}
#endif

OPT(missing, "missing", bool, false,
     gettext_noop("perform the operations for files missing from workspace"))
#ifdef option_bodies
{
  missing = true;
}
#endif

OPT(next, "next", long, -1,
     gettext_noop("limit log output to the next number of entries"))
#ifdef option_bodies
{
  next = boost::lexical_cast<long>(arg);
  if (next <= 0)
    throw bad_arg_internal(F("cannot be zero or negative").str());
}
#endif

OPT(no_files, "no-files", bool, false,
     gettext_noop("exclude files when printing logs"))
#ifdef option_bodies
{
  no_files = true;
}
#endif

OPT(no_graph, "no-graph", bool, false,
     gettext_noop("do not use ASCII graph to display ancestry"))
#ifdef option_bodies
{
  no_graph = true;
}
#endif

OPT(no_ignore, "no-respect-ignore", bool, false,
     gettext_noop("do not ignore any files"))
#ifdef option_bodies
{
  no_ignore = true;
}
#endif

OPT(no_merges, "no-merges", bool, false,
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
 
OPT(pidfile, "pid-file", system_path, ,
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
  ui.set_tick_write_nothing();
}
#endif

GOPT(extra_rcfiles, "rcfile", args_vector, ,
     gettext_noop("load extra rc file"))
#ifdef option_bodies
{
  extra_rcfiles.push_back(arg_type(arg));
}
#endif

GOPT(reallyquiet, "reallyquiet", bool, false,
gettext_noop("suppress warning, verbose, informational and progress messages"))
#ifdef option_bodies
{
  reallyquiet = true;
  global_sanity.set_reallyquiet();
  ui.set_tick_write_nothing();
}
#endif

OPT(recursive, "recursive,R", bool, false,
     gettext_noop("also operate on the contents of any listed directories"))
#ifdef option_bodies
{
  recursive = true;
}
#endif

OPTVAR(revision, args_vector, revision_selectors, )
OPTION(revision, revision, true, "revision,r",
     gettext_noop("select revision id for operation"))
#ifdef option_bodies
{
  revision_selectors.push_back(arg_type(arg));
}
#endif

GOPT(root, "root", std::string, ,
     gettext_noop("limit search for workspace to specified root"))
#ifdef option_bodies
{
  root = arg;
}
#endif

OPT(set_default, "set-default", bool, false,
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
  if (ticker == "none" || global_sanity.quiet_p())
    ui.set_tick_write_nothing();
  else if (ticker == "dot")
    ui.set_tick_write_dot();
  else if (ticker == "count")
    ui.set_tick_write_count();
  else
    throw bad_arg_internal(F("argument must be 'none', 'dot', or 'count'").str());
}
#endif

OPT(from, "from", args_vector, , gettext_noop("revision(s) to start logging at"))
#ifdef option_bodies
{
  from.push_back(arg_type(arg));
}
#endif

OPT(to, "to", args_vector, , gettext_noop("revision(s) to stop logging at"))
#ifdef option_bodies
{
  to.push_back(arg_type(arg));
}
#endif

OPT(unknown, "unknown", bool, false,
     gettext_noop("perform the operations for unknown files from workspace"))
#ifdef option_bodies
{
  unknown = true;
}

#endif

OPT(verbose, "verbose", bool, false,
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

OPTION(globals, xargs, true, "xargs,@",
       gettext_noop("insert command line arguments taken from the given file"))
#ifdef option_bodies
{
}
#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

