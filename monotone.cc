// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "config.h"

#include <cstdio>
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
using std::endl;
using std::string;
using std::ios_base;
using std::ostringstream;
using std::set;
using std::string;
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
//
// "ui" is a global object, through which all messages to the user go.
// see ui.hh for it
//
// "cmds" is a static table in commands.cc which associates top-level
// commands, given on the command-line, to various version control tasks.
//
// "app_state" is a non-static object type which contains all the
// application state (filesystem, database, network, lua interpreter,
// etc). you can make more than one of these, and feed them to a command in
// the command table.

// this file defines cpp_main, which does option processing and sub-command
// dispatching, and provides the outermost exception catch clauses.  it is
// called by main, in unix/main.cc or win32/main.cc; that function is
// responsible for trapping fatal conditions reported by the operating
// system (signals, win32 structured exceptions, etc).

// this program should *never* unexpectedly terminate without dumping some
// diagnostics.  if the fatal condition is an invariant check or anything
// else that produces a C++ exception caught in this file, the debug logs
// will be dumped out.  if the fatal condition is only caught in the lower-
// level handlers in main.cc, at least we'll get a friendly error message.


// Wrapper class to ensure Botan is properly initialized and deinitialized.
struct botan_library
{
  botan_library() { 
    Botan::Init::initialize();
    Botan::set_default_allocator("malloc");
  }
  ~botan_library() {
    Botan::Init::deinitialize();
  }
};

// Similarly, for the global ui object.  (We do not want to use global
// con/destructors for this, as they execute outside the protection of
// main.cc's signal handlers.)
struct ui_library
{
  ui_library() {
    ui.initialize();
  }
  ~ui_library() {
    ui.deinitialize();
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

void
tokenize_for_command_line(string const & from, vector<string> & to)
{
  // Unfortunately, the tokenizer in basic_io is too format-specific
  to.clear();
  enum quote_type {none, one, two};
  string cur;
  quote_type type = none;
  bool have_tok(false);
  
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      if (*i == '\'')
        {
          if (type == none)
            type = one;
          else if (type == one)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '"')
        {
          if (type == none)
            type = two;
          else if (type == two)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '\\')
        {
          if (type != one)
            ++i;
          N(i != from.end(), F("Invalid escape in --xargs file"));
          cur += *i;
          have_tok = true;
        }
      else if (string(" \n\t").find(*i) != string::npos)
        {
          if (type == none)
            {
              if (have_tok)
                to.push_back(cur);
              cur.clear();
              have_tok = false;
            }
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else
        {
          cur += *i;
          have_tok = true;
        }
    }
  if (have_tok)
    to.push_back(cur);
}

int
cpp_main(int argc, char ** argv)
{
  int ret = 0;

  // go-go gadget i18n
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  // set up global ui object - must occur before anything that might try to
  // issue a diagnostic
  ui_library acquire_ui;

  // we want to catch any early informative_failures due to charset
  // conversion etc
  try
  {
  // Set up the global sanity object.  No destructor is needed and
  // therefore no wrapper object is needed either.
  global_sanity.initialize(argc, argv, setlocale(LC_ALL, 0));

  // Set up secure memory allocation etc
  botan_library acquire_botan;

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
  vector<string> args;
  utf8 progname;
  for (int i = 0; i < argc; ++i)
    {
      external ex(argv[i]);
      utf8 ut;
      system_to_utf8(ex, ut);
      if (i)
        args.push_back(ut());
      else
        progname = ut;
    }

  // find base name of executable
  string prog_path = fs::path(progname()).leaf();
  if (prog_path.rfind(".exe") == prog_path.size() - 4)
    prog_path = prog_path.substr(0, prog_path.size() - 4);
  utf8 prog_name(prog_path);

  app_state app;
  try
    {

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

      // Check the command line for -@/--xargs
      {
        po::parsed_options parsed = po::command_line_parser(args)
          .options(all_options)
          .run();
        po::variables_map vm;
        po::store(parsed, vm);
        po::notify(vm);
        if (vm.count(option::argfile()))
          {
            vector<string> files = vm[option::argfile()].as<vector<string> >();
            for (vector<string>::iterator f = files.begin();
                 f != files.end(); ++f)
              {
                data dat;
                read_data_for_command_line(*f, dat);
                vector<string> fargs;
                tokenize_for_command_line(dat(), fargs);
                for (vector<string>::const_iterator i = fargs.begin();
                     i != fargs.end(); ++i)
                  {
                    args.push_back(*i);
                  }
              }
          }
      }

      po::parsed_options parsed = po::command_line_parser(args)
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
      parsed = po::command_line_parser(args)
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
          vector<string> files = vm[option::rcfile()].as<vector<string> >();
          for (vector<string>::const_iterator i = files.begin();
               i != files.end(); ++i)
            app.add_rcfile(*i);
        }

      if (vm.count(option::dump()))
        {
          global_sanity.filename = system_path(vm[option::dump()].as<string>()).as_external();
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
            app.requested_help = true;
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
          return 0;
        }

      if (vm.count(option::full_version()))
        {
          print_full_version();
          return 0;
        }

      if (vm.count(option::revision()))
        {
          vector<string> revs = vm[option::revision()].as<vector<string> >();
          for (vector<string>::const_iterator i = revs.begin();
               i != revs.end(); ++i)
            app.add_revision(*i);
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

      if (vm.count(option::stdio()))
        {
          app.bind_stdio = true;
        }

      if (vm.count(option::no_transport_auth()))
        {
          app.use_transport_auth = false;
        }

      if (vm.count(option::exclude()))
        {
          vector<string> excls = vm[option::exclude()].as<vector<string> >();
          for (vector<string>::const_iterator i = excls.begin();
               i != excls.end(); ++i)
            app.add_exclude(utf8(*i));
        }

      if (vm.count(option::pidfile()))
        {
          app.set_pidfile(system_path(vm[option::pidfile()].as<string>()));
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

      if (vm.count(option::no_show_encloser()))
        {
          app.diff_show_encloser = false;
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
            app.bind_stdio = false;
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
          vector<string> kp = vm[option::key_to_push()].as<vector<string> >();
          for (vector<string>::const_iterator i = kp.begin();
               i != kp.end(); ++i)
            app.add_key_to_push(*i);
        }

      if (vm.count(option::drop_attr()))
        {
          vector<string> da = vm[option::drop_attr()].as<vector<string> >();
          for (vector<string>::const_iterator i = da.begin();
               i != da.end(); ++i)
            app.attrs_to_drop.insert(*i);
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
          app.requested_help = true;
        }

      // stop here if they asked for help
      if (app.requested_help)
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

      if (!app.found_workspace && global_sanity.filename.empty())
        global_sanity.filename = (app.get_confdir() / "dump").as_external();

      // main options processed, now invoke the
      // sub-command w/ remaining args
      if (cmd.empty())
        {
          throw usage("");
        }
      else
        {
          vector<utf8> args(positional_args.begin(), positional_args.end());
          return commands::process(app, cmd, args);
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
      if (app.requested_help)
        return 0;
      else
        return 2;

    }
  }
  catch (informative_failure & inf)
    {
      ui.inform(inf.what());
      return 1;
    }
  catch (ios_base::failure const & ex)
    {
      // an error has already been printed
      return 1;
    }
  catch (std::exception const & ex)
    {
      ui.fatal_exception (ex);
      return 3;
    }
  catch (...)
    {
      ui.fatal_exception ();
      return 3;
    }

  // control cannot reach this point
  ui.fatal("impossible: reached end of cpp_main");
  return 3;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
