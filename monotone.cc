// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

#include <cstdio>
#include <strings.h>
#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>
#include <locale.h>

#include <stdlib.h>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

#include "botan/botan.h"

#include "i18n.h"

#include "app_state.hh"
#include "commands.hh"
#include "sanity.hh"
#include "cleanup.hh"
#include "file_io.hh"
#include "charset.hh"
#include "ui.hh"
#include "mt_version.hh"
#include "options.hh"
#include "paths.hh"

using std::cout;
using std::string;
using std::ostringstream;
using std::set;
using std::vector;
using std::ios_base;
using boost::shared_ptr;
namespace po = boost::program_options;

// main option processing and exception handling code

// options are split into two categories.  the first covers global options,
// which globally affect program behaviour.  the second covers options
// specific to one or more commands.  these command-specific options are
// defined in a single group, with the intent that any command-specific
// option means the same thing for any command that uses it.

namespace option
{
  using boost::program_options::value;
  using boost::program_options::option_description;
  using boost::program_options::options_description;

  options_description global_options;
  options_description specific_options;

  no_option none;

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
  COPT(pidfile, "pid-file", value<string>(), gettext_noop("record process id of server"));
  COPT(recursive, "recursive,R", null_value(), gettext_noop("also operate on the contents of any listed directories"));
  COPT(revision, "revision,r", value<string>(), gettext_noop("select revision id for operation"));
  COPT(set_default, "set-default", null_value(), gettext_noop("use the current arguments as the future default"));
  COPT(unified_diff, "unified", null_value(), gettext_noop("use unified diff format"));
  COPT(unknown, "unknown", null_value(), gettext_noop("perform the operations for unknown files from workspace"));
#undef COPT
}

// there are 3 variables which serve as roots for our system.
//
// "global_sanity" is a global object, which contains the error logging
// system, which is constructed once and used by any nana logging actions.
// see cleanup.hh for it
//
// "cmds" is a static table in commands.cc which associates top-level
// commands, given on the command-line, to various version control tasks.
//
// "app_state" is a non-static object type which contains all the
// application state (filesystem, database, network, lua interpreter,
// etc). you can make more than one of these, and feed them to a command in
// the command table.

// our main function is run inside a boost execution monitor. this monitor
// portably sets up handlers for various fatal conditions (signals, win32
// structured exceptions, etc) and provides a uniform reporting interface
// to any exceptions it catches. we augment this with a helper atexit()
// which will also dump our internal logs when an explicit clean shutdown
// flag is not set.
//
// in other words, this program should *never* unexpectedly terminate
// without dumping some diagnostics.

void
dumper()
{
  if (!global_sanity.clean_shutdown)
    global_sanity.dump_buffer();

  Botan::Init::deinitialize();
}


struct
utf8_argv
{
  int argc;
  char **argv;

  explicit utf8_argv(int ac, char **av)
    : argc(ac),
      argv(static_cast<char **>(malloc(ac * sizeof(char *))))
  {
    I(argv != NULL);
    for (int i = 0; i < argc; ++i)
      {
        external ext(av[i]);
        utf8 utf;
        system_to_utf8(ext, utf);
        argv[i] = static_cast<char *>(malloc(utf().size() + 1));
        I(argv[i] != NULL);
        memcpy(argv[i], utf().data(), utf().size());
        argv[i][utf().size()] = static_cast<char>(0);
    }
  }

  ~utf8_argv()
  {
    if (argv != NULL)
      {
        for (int i = 0; i < argc; ++i)
          if (argv[i] != NULL)
            free(argv[i]);
        free(argv);
      }
  }
};

#if 0 // FIXME! need b::po equiv.
// Read arguments from a file.  The special file '-' means stdin.
// Returned value must be free()'d, after arg parsing has completed.
static void
my_poptStuffArgFile(poptContext con, utf8 const & filename)
{
  utf8 argstr;
  {
    data dat;
    read_data_for_command_line(filename, dat);
    external ext(dat());
    system_to_utf8(ext, argstr);
  }

  const char **argv = 0;
  int argc = 0;
  int rc;

  // Parse the string.  It's OK if there are no arguments.
  rc = poptParseArgvString(argstr().c_str(), &argc, &argv);
  N(rc >= 0 || rc == POPT_ERROR_NOARG,
    F("problem parsing arguments from file %s: %s")
    % filename % poptStrerror(rc));

  if (rc != POPT_ERROR_NOARG)
    {
      // poptStuffArgs does not take an argc argument, but rather requires that
      // the argv array be null-terminated.
      I(argv[argc] == NULL);
      N((rc = poptStuffArgs(con, argv)) >= 0,
        F("weird error when stuffing arguments read from %s: %s\n")
        % filename % poptStrerror(rc));
    }

  free(argv);
}
#endif

int
cpp_main(int argc, char ** argv)
{
  int ret = 0;

  atexit(&dumper);

  // go-go gadget i18n
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);


  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {

  // set up some marked strings, so even if our logbuf overflows, we'll get
  // this data in a crash.
  string cmdline_string;
  {
    ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << "'" << argv[i] << "'";
      }
    cmdline_string = cmdline_ss.str();
  }
  MM(cmdline_string);
  L(FL("command line: %s\n") % cmdline_string);

  string locale_string = (setlocale(LC_ALL, NULL) == NULL ? "n/a" : setlocale(LC_ALL, NULL));
  MM(locale_string);
  L(FL("set locale: LC_ALL=%s\n") % locale_string);

  string full_version_string;
  get_full_version(full_version_string);
  MM(full_version_string);

  // Set up secure memory allocation etc
  Botan::Init::initialize();
  Botan::set_default_allocator("malloc");

  // decode all argv values into a UTF-8 array
  save_initial_path();
  utf8_argv uv(argc, argv);

  // find base name of executable
  string prog_path = fs::path(uv.argv[0]).leaf();
  if (prog_path.rfind(".exe") == prog_path.size() - 4)
    prog_path = prog_path.substr(0, prog_path.size() - 4);
  utf8 prog_name(prog_path);

  // process main program options
  bool requested_help = false;

  try
    {
      app_state app;

      app.set_prog_name(prog_name);

      // set up for parsing.  we add a hidden argument that collections all
      // positional arguments, which we process ourselves in a moment.
      po::options_description all_options;
      all_options.add(option::global_options);
      all_options.add(option::specific_options);
      all_options.add_options()
        ("all_positional_args", po::value< vector<string> >());
      po::positional_options_description all_positional_args;
      all_positional_args.add("all_positional_args", -1);

      po::parsed_options parsed = po::command_line_parser(argc, uv.argv)
        .options(all_options)
        .positional(all_positional_args)
        .run();
      po::variables_map vm;
      po::store(parsed, vm);
      po::notify(vm);

      // consume the command, and perform completion if necessary
      string cmd;
      vector<string> positional_args;
      if (vm.count("all_positional_args"))
        {
          positional_args = vm["all_positional_args"].as< vector<string> >();
          cmd = commands::complete_command(idx(positional_args, 0));
          positional_args.erase(positional_args.begin());
        }

      // build an options_description specific to this cmd.
      set< shared_ptr<po::option_description> > cmd_options;
      cmd_options = commands::command_options(cmd);
      po::options_description cmd_options_desc;
      set< shared_ptr<po::option_description> >::const_iterator it;
      for (it = cmd_options.begin(); it != cmd_options.end(); ++it)
        cmd_options_desc.add(*it);

      po::options_description all_for_this_cmd;
      all_for_this_cmd.add(option::global_options);
      all_for_this_cmd.add(cmd_options_desc);

      // reparse arguments using specific options.
      parsed = po::command_line_parser(argc, uv.argv)
        .options(all_for_this_cmd)
        .run();
      po::store(parsed, vm);
      po::notify(vm);

      if (vm.count(option::debug()))
        {
          global_sanity.set_debug();
        }

      if (vm.count(option::quiet()))
        {
          global_sanity.set_quiet();
          ui.set_tick_writer(new tick_write_nothing);
        }

      if (vm.count(option::reallyquiet()))
        {
          global_sanity.set_reallyquiet();
          ui.set_tick_writer(new tick_write_nothing);
        }

      if (vm.count(option::nostd()))
        {
          app.set_stdhooks(false);
        }

      if (vm.count(option::norc()))
        {
          app.set_rcfiles(false);
        }

      if (vm.count(option::verbose()))
        {
          app.set_verbose(true);
        }

      if (vm.count(option::rcfile()))
        {
          app.add_rcfile(vm[option::rcfile()].as<string>());
        }

      if (vm.count(option::dump()))
        {
          global_sanity.filename = system_path(vm[option::dump()].as<string>());
        }

      if (vm.count(option::log()))
        {
          ui.redirect_log_to(system_path(vm[option::log()].as<string>()));
        }

      if (vm.count(option::db_name()))
        {
          app.set_database(system_path(vm[option::db_name()].as<string>()));
        }

      if (vm.count(option::key_dir()))
        {
          app.set_key_dir(system_path(vm[option::key_dir()].as<string>()));
        }

      if (vm.count(option::conf_dir()))
        {
          app.set_confdir(system_path(vm[option::conf_dir()].as<string>()));
        }

      if (vm.count(option::ticker()))
        {
          string ticker = vm[option::ticker()].as<string>();
          if (ticker == "none" || global_sanity.quiet)
            ui.set_tick_writer(new tick_write_nothing);
          else if (ticker == "dot")
            ui.set_tick_writer(new tick_write_dot);
          else if (ticker == "count")
            ui.set_tick_writer(new tick_write_count);
          else
            requested_help = true;
        }

      if (vm.count(option::key_name()))
        {
          app.set_signing_key(vm[option::key_name()].as<string>());
        }

      if (vm.count(option::branch_name()))
        {
          app.set_branch(vm[option::branch_name()].as<string>());
          app.set_is_explicit_option(option::branch_name());
        }

      if (vm.count(option::version()))
        {
          print_version();
          global_sanity.clean_shutdown = true;
          return 0;
        }

      if (vm.count(option::full_version()))
        {
          print_full_version();
          global_sanity.clean_shutdown = true;
          return 0;
        }

      if (vm.count(option::revision()))
        {
          app.add_revision(vm[option::revision()].as<string>());
        }

      if (vm.count(option::message()))
        {
          app.set_message(vm[option::message()].as<string>());
          app.set_is_explicit_option(option::message());
        }

      if (vm.count(option::msgfile()))
        {
          app.set_message_file(vm[option::msgfile()].as<string>());
          app.set_is_explicit_option(option::msgfile());
        }

      if (vm.count(option::date()))
        {
          app.set_date(vm[option::date()].as<string>());
        }

      if (vm.count(option::author()))
        {
          app.set_author(vm[option::author()].as<string>());
        }

      if (vm.count(option::root()))
        {
          app.set_root(system_path(vm[option::root()].as<string>()));
        }

      if (vm.count(option::last()))
        {
          app.set_last(vm[option::last()].as<long>());
        }

      if (vm.count(option::next()))
        {
          app.set_next(vm[option::next()].as<long>());
        }

      if (vm.count(option::depth()))
        {
          app.set_depth(vm[option::depth()].as<long>());
        }

      if (vm.count(option::brief()))
        {
          global_sanity.set_brief();
        }

      if (vm.count(option::diffs()))
        {
          app.diffs = true;
        }

      if (vm.count(option::no_merges()))
        {
          app.no_merges = true;
        }

      if (vm.count(option::set_default()))
        {
          app.set_default = true;
        }

      if (vm.count(option::exclude()))
        {
          app.add_exclude(utf8(vm[option::exclude()].as<string>()));
        }

      if (vm.count(option::pidfile()))
        {
          app.set_pidfile(system_path(vm[option::pidfile()].as<string>()));
        }

      if (vm.count(option::argfile()))
        {
#if 0
          // FIXME!
          my_poptStuffArgFile(ctx(), utf8(string(argstr)));
#endif
        }

      if (vm.count(option::unified_diff()))
        {
          app.set_diff_format(unified_diff);
        }

      if (vm.count(option::context_diff()))
        {
          app.set_diff_format(context_diff);
        }

      if (vm.count(option::external_diff()))
        {
          app.set_diff_format(external_diff);
        }

      if (vm.count(option::external_diff_args()))
        {
          app.set_diff_args(utf8(vm[option::external_diff_args()].as<string>()));
        }

      if (vm.count(option::execute()))
        {
          app.execute = true;
        }

      if (vm.count(option::bind()))
        {
          {
            string arg = vm[option::bind()].as<string>();
            string addr_part, port_part;
            size_t l_colon = arg.find(':');
            size_t r_colon = arg.rfind(':');

            // not an ipv6 address, as that would have at least two colons
            if (l_colon == r_colon)
              {
                addr_part = (r_colon == string::npos ? arg : arg.substr(0, r_colon));
                port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
              }
            else
              {
                // IPv6 addresses have a port specified in the style: [2001:388:0:13::]:80
                size_t squareb = arg.rfind(']');
                if ((arg.find('[') == 0) && (squareb != string::npos))
                  {
                    if (squareb < r_colon)
                      port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
                    else
                      port_part = "";
                    addr_part = (squareb == string::npos ? arg.substr(1, arg.size()) : arg.substr(1, squareb-1));
                  }
                else
                  {
                    addr_part = arg;
                    port_part = "";
                  }
              }
            app.bind_address = utf8(addr_part);
            app.bind_port = utf8(port_part);
          }
          app.set_is_explicit_option(option::bind());
        }

      if (vm.count(option::missing()))
        {
          app.missing = true;
        }

      if (vm.count(option::unknown()))
        {
          app.unknown = true;
        }

      if (vm.count(option::key_to_push()))
        {
          app.add_key_to_push(vm[option::key_to_push()].as<string>());
        }

      if (vm.count(option::drop_attr()))
        {
          app.attrs_to_drop.insert(vm[option::drop_attr()].as<string>());
        }

      if (vm.count(option::no_files()))
        {
          app.no_files = true;
        }

      if (vm.count(option::recursive()))
        {
          app.set_recursive();
        }

      if (vm.count(option::help()))
        {
          requested_help = true;
        }

      // stop here if they asked for help
      if (requested_help)
        {
          throw usage(cmd);     // cmd may be empty, and that's fine.
        }

      // at this point we allow a workspace (meaning search for it
      // and if found read _MTN/options, but don't use the data quite
      // yet, and read all the monotonercs).  Processing the data
      // from _MTN/options happens later.
      // Certain commands may subsequently require a workspace or fail
      // if we didn't find one at this point.
      app.allow_workspace();

      // main options processed, now invoke the
      // sub-command w/ remaining args
      if (cmd.empty())
        {
          throw usage("");
        }
      else
        {
          vector<utf8> args(positional_args.begin(), positional_args.end());
          ret = commands::process(app, cmd, args);
        }
    }
  catch (po::ambiguous_option const & e)
    {
      string msg = (F("%s:\n") % e.what()).str();
      vector<string>::const_iterator it = e.alternatives.begin();
      for (; it != e.alternatives.end(); ++it)
        msg += *it + "\n";
      N(false, i18n_format(msg));
    }
  catch (po::error const & e)
    {
      N(false, F("%s") % e.what());
    }
  catch (usage & u)
    {
      // Make sure to hide documentation that's not part of
      // the current command.
      set< shared_ptr<po::option_description> > cmd_options;
      cmd_options = commands::command_options(u.which);

      unsigned count = 0;
      po::options_description cmd_options_desc;
      set< shared_ptr<po::option_description> >::const_iterator it;
      for (it = cmd_options.begin(); it != cmd_options.end(); ++it, ++count)
        cmd_options_desc.add(*it);

      cout << F("Usage: %s [OPTION...] command [ARG...]") % prog_name << "\n\n";
      cout << option::global_options << "\n";

      if (count > 0)
        {
          cout << F("Options specific to '%s %s':") % prog_name % u.which << "\n\n";
          cout << cmd_options_desc << "\n";
        }

      commands::explain_usage(u.which, cout);
      global_sanity.clean_shutdown = true;
      return 2;
    }
  }
  catch (informative_failure & inf)
  {
    ui.inform(inf.what);
    global_sanity.clean_shutdown = true;
    return 1;
  }
  catch (ios_base::failure const & ex)
  {
    global_sanity.clean_shutdown = true;
    return 1;
  }

  global_sanity.clean_shutdown = true;
  return ret;
}
