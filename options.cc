
#include <string>

#include "i18n.h"
#include "options.hh"

using std::string;

namespace option
{
  namespace po = boost::program_options;
  using boost::program_options::value;
  using boost::program_options::option_description;
  using boost::program_options::options_description;

  options_description global_options;
  options_description specific_options;

  no_option none;

//    {"no-show-c-function", 0, POPT_ARG_NONE, NULL, OPT_NO_SHOW_ENCLOSER, gettext_noop("another name for --no-show-encloser (for compatibility with GNU diff)"), NULL},
//    {"stdio", 0, POPT_ARG_NONE, NULL, OPT_STDIO, gettext_noop("serve netsync on stdio"), NULL},
//    {"no-transport-auth", 0, POPT_ARG_NONE, NULL, OPT_NO_TRANSPORT_AUTH, gettext_noop("disable transport authentication"), NULL},
//    {"automate-stdio-size", 's', POPT_ARG_LONG, &arglong, OPT_AUTOMATE_STDIO_SIZE, gettext_noop("block size in bytes for \"automate stdio\" output"), NULL},

  po::value_semantic *
  null_value()
  {
    return new po::untyped_value(true);
  }

  // the options below are also declared in options.hh for other users.  the
  // GOPT and COPT defines are just to reduce duplication, maybe there is a
  // cleaner way to do the same thing?

  // global options
#define GOPT(NAME, OPT, TYPE, DESC) global NAME(new option_description(OPT, TYPE, DESC))
  GOPT(debug, "debug", null_value(), gettext_noop("print debug log to stderr while running"));
  GOPT(dump, "dump", value<string>(), gettext_noop("file to dump debugging log to, on failure"));
  GOPT(log, "log", value<string>(), gettext_noop("file to write the log to"));
  GOPT(quiet, "quiet", null_value(), gettext_noop("suppress verbose, informational and progress messages"));
  GOPT(reallyquiet, "reallyquiet", null_value(), gettext_noop("suppress warning, verbose, informational and progress messages"));
  GOPT(help, "help,h", null_value(), gettext_noop("display help message"));
  GOPT(version, "version", null_value(), gettext_noop("print version number, then exit"));
  GOPT(full_version, "full-version", null_value(), gettext_noop("print detailed version number, then exit"));
  GOPT(argfile, "xargs,@", value<string>(), gettext_noop("insert command line arguments taken from the given file"));
  GOPT(ticker, "ticker", value<string>(), gettext_noop("set ticker style (count|dot|none)"));
  GOPT(nostd, "nostd", null_value(), gettext_noop("do not load standard lua hooks"));
  GOPT(norc, "norc", null_value(), gettext_noop("do not load ~/.monotone/monotonerc or _MTN/monotonerc lua files"));
  GOPT(rcfile, "rcfile", value<string>(), gettext_noop("load extra rc file"));
  GOPT(key_name, "key,k", value<string>(), gettext_noop("set key for signatures"));
  GOPT(db_name, "db,d", value<string>(), gettext_noop("set name of database"));
  GOPT(root, "root", value<string>(), gettext_noop("limit search for workspace to specified root"));
  GOPT(verbose, "verbose", null_value(), gettext_noop("verbose completion output"));
  GOPT(key_dir, "keydir", value<string>(), gettext_noop("set location of key store"));
  GOPT(conf_dir, "confdir", value<string>(), gettext_noop("set location of configuration directory"));
#undef OPT

  // command-specific options
#define COPT(NAME, OPT, TYPE, DESC) specific NAME(new option_description(OPT, TYPE, DESC))
  COPT(author, "author", value<string>(), gettext_noop("override author for commit"));
  COPT(bind, "bind", value<string>(), gettext_noop("address:port to listen on (default :4691)"));
  COPT(branch_name, "branch,b", value<string>(), gettext_noop("select branch cert for operation"));
  COPT(brief, "brief", null_value(), gettext_noop("print a brief version of the normal output"));
  COPT(context_diff, "context", null_value(), gettext_noop("use context diff format"));
  COPT(date, "date", value<string>(), gettext_noop("override date/time for commit"));
  COPT(depth, "depth", value<long>(), gettext_noop("limit the number of levels of directories to descend"));
  COPT(diffs, "diffs", null_value(), gettext_noop("print diffs along with logs"));
  COPT(drop_attr, "drop-attr", value<string>(), gettext_noop("when rosterifying, drop attrs entries with the given key"));
  COPT(exclude, "exclude", value<string>(), gettext_noop("leave out anything described by its argument"));
  COPT(execute, "execute,e", null_value(), gettext_noop("perform the associated file operation"));
  COPT(external_diff, "external", null_value(), gettext_noop("use external diff hook for generating diffs"));
  COPT(external_diff_args, "diff-args", value<string>(), gettext_noop("argument to pass external diff hook"));
  COPT(key_to_push, "key-to-push", value<string>(), gettext_noop("push the specified key even if it hasn't signed anything"));
  COPT(last, "last", value<long>(), gettext_noop("limit log output to the last number of entries"));
  COPT(message, "message,m", value<string>(), gettext_noop("set commit changelog message"));
  COPT(missing, "missing", null_value(), gettext_noop("perform the operations for files missing from workspace"));
  COPT(msgfile, "message-file", value<string>(), gettext_noop("set filename containing commit changelog message"));
  COPT(next, "next", value<long>(), gettext_noop("limit log output to the next number of entries"));
  COPT(no_files, "no-files", null_value(), gettext_noop("exclude files when printing logs"));
  COPT(no_merges, "no-merges", null_value(), gettext_noop("exclude merges when printing logs"));
  COPT(no_show_encloser, "no-show-encloser", null_value(), gettext_noop("do not show the function containing each block of changes"));
  COPT(pidfile, "pid-file", value<string>(), gettext_noop("record process id of server"));
  COPT(recursive, "recursive,R", null_value(), gettext_noop("also operate on the contents of any listed directories"));
  COPT(revision, "revision,r", value<string>(), gettext_noop("select revision id for operation"));
  COPT(set_default, "set-default", null_value(), gettext_noop("use the current arguments as the future default"));
  COPT(unified_diff, "unified", null_value(), gettext_noop("use unified diff format"));
  COPT(unknown, "unknown", null_value(), gettext_noop("perform the operations for unknown files from workspace"));
#undef COPT
}
