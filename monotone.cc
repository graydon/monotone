// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <config.h>

#include <popt.h>
#include <cstdio>
#include <iterator>
#include <iostream>
#include <sstream>

#include <stdlib.h>

#include "app_state.hh"
#include "commands.hh"
#include "sanity.hh"
#include "cleanup.hh"
#include "file_io.hh"
#include "transforms.hh"
#include "ui.hh"
#include "mt_version.hh"

#define OPT_DEBUG 1
#define OPT_HELP 2
#define OPT_NOSTD 3
#define OPT_NORC 4
#define OPT_RCFILE 5
#define OPT_DB_NAME 6
#define OPT_KEY_NAME 7
#define OPT_BRANCH_NAME 8
#define OPT_QUIET 9
#define OPT_VERSION 10
#define OPT_DUMP 11
#define OPT_TICKER 12
#define OPT_FULL_VERSION 13
#define OPT_REVISION 14
#define OPT_MESSAGE 15

// main option processing and exception handling code

using namespace std;

char * argstr = NULL;

struct poptOption options[] =
  {
    {"debug", 0, POPT_ARG_NONE, NULL, OPT_DEBUG, "print debug log to stderr while running", NULL},
    {"dump", 0, POPT_ARG_STRING, &argstr, OPT_DUMP, "file to dump debugging log to, on failure", NULL},
    {"quiet", 0, POPT_ARG_NONE, NULL, OPT_QUIET, "suppress log and progress messages", NULL},
    {"help", 0, POPT_ARG_NONE, NULL, OPT_HELP, "display help message", NULL},
    {"nostd", 0, POPT_ARG_NONE, NULL, OPT_NOSTD, "do not load standard lua hooks", NULL},
    {"norc", 0, POPT_ARG_NONE, NULL, OPT_NORC, "do not load ~/.monotonerc or MT/monotonerc lua files", NULL},
    {"rcfile", 0, POPT_ARG_STRING, &argstr, OPT_RCFILE, "load extra rc file", NULL},
    {"key", 0, POPT_ARG_STRING, &argstr, OPT_KEY_NAME, "set key for signatures", NULL},
    {"db", 0, POPT_ARG_STRING, &argstr, OPT_DB_NAME, "set name of database", NULL},
    {"branch", 0, POPT_ARG_STRING, &argstr, OPT_BRANCH_NAME, "select branch cert for operation", NULL},
    {"version", 0, POPT_ARG_NONE, NULL, OPT_VERSION, "print version number, then exit", NULL},
    {"full-version", 0, POPT_ARG_NONE, NULL, OPT_FULL_VERSION, "print detailed version number, then exit", NULL},
    {"ticker", 0, POPT_ARG_STRING, &argstr, OPT_TICKER, "set ticker style (count|dot) [count]", NULL},
    {"revision", 0, POPT_ARG_STRING, &argstr, OPT_REVISION, "select revision id for operation", NULL},
    {"message", 0, POPT_ARG_STRING, &argstr, OPT_MESSAGE, "set commit changelog message", NULL},
    { NULL, 0, 0, NULL, 0 }
  };

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

static bool clean_shutdown;

void 
dumper() 
{
  if (!clean_shutdown)
    global_sanity.dump_buffer();
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

int 
cpp_main(int argc, char ** argv)
{
  
  clean_shutdown = false;

  atexit(&dumper);

  // go-go gadget i18n

  setlocale(LC_CTYPE, "");
  setlocale(LC_MESSAGES, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
  
  {
    std::ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << "'" << argv[i] << "'";
      }
    L(F("command line: %s\n") % cmdline_ss.str());
  }       

  L(F("set locale: LC_CTYPE=%s, LC_MESSAGES=%s\n")
    % (setlocale(LC_CTYPE, NULL) == NULL ? "n/a" : setlocale(LC_CTYPE, NULL))
    % (setlocale(LC_MESSAGES, NULL) == NULL ? "n/a" : setlocale(LC_CTYPE, NULL)));
  
  // decode all argv values into a UTF-8 array

  save_initial_path();
  utf8_argv uv(argc, argv);

  // prepare for arg parsing
      
  cleanup_ptr<poptContext, poptContext> 
    ctx(poptGetContext(NULL, argc, (char const **) uv.argv, options, 0),
        &poptFreeContext);

  // process main program options

  int ret = 0;
  int opt;
  bool requested_help = false;

  poptSetOtherOptionHelp(ctx(), "[OPTION...] command [ARGS...]\n");

  try 
    {      

      app_state app;

      while ((opt = poptGetNextOpt(ctx())) > 0)
        {
          switch(opt)
            {
            case OPT_DEBUG:
              global_sanity.set_debug();
              break;

            case OPT_QUIET:
              global_sanity.set_quiet();
              break;

            case OPT_NOSTD:
              app.set_stdhooks(false);
              break;

            case OPT_NORC:
              app.set_rcfiles(false);
              break;

            case OPT_RCFILE:
              app.add_rcfile(absolutify(tilde_expand(string(argstr))));
              break;

            case OPT_DUMP:
              global_sanity.filename = absolutify(tilde_expand(string(argstr)));
              break;

            case OPT_DB_NAME:
              app.set_database(absolutify(tilde_expand(string(argstr))));
              break;

            case OPT_TICKER:
              if (string(argstr) == "dot")
                ui.set_tick_writer(new tick_write_dot);
              else if (string(argstr) == "count")
                ui.set_tick_writer(new tick_write_count);
              else
                requested_help = true;
              break;

            case OPT_KEY_NAME:
              app.set_signing_key(string(argstr));
              break;

            case OPT_BRANCH_NAME:
              app.set_branch(string(argstr));
              break;

            case OPT_VERSION:
              print_version();
              clean_shutdown = true;
              return 0;

            case OPT_FULL_VERSION:
              print_full_version();
              clean_shutdown = true;
              return 0;

            case OPT_REVISION:
               app.add_revision(string(argstr));
              break;

            case OPT_MESSAGE:
               app.set_message(string(argstr));
              break;

            case OPT_HELP:
            default:
              requested_help = true;
              break;
            }
        }

      // verify that there are no errors in the command line

      N(opt == -1,
        F("syntax error near the \"%s\" option: %s") %
          poptBadOption(ctx(), POPT_BADOPTION_NOALIAS) % poptStrerror(opt));

      // stop here if they asked for help

      if (requested_help)
        {
          if (poptPeekArg(ctx()))
            {
              string cmd(poptGetArg(ctx()));
              throw usage(cmd);
            }
          else
            throw usage("");
        }

      // main options processed, now invoke the 
      // sub-command w/ remaining args

      if (!poptPeekArg(ctx()))
        {
          throw usage("");
        }
      else
        {
          string cmd(poptGetArg(ctx()));
          vector<utf8> args;
          while(poptPeekArg(ctx())) 
            {
              args.push_back(utf8(string(poptGetArg(ctx()))));
            }
          ret = commands::process(app, cmd, args);
        } 
    }
  catch (usage & u)
    {
      poptPrintHelp(ctx(), stdout, 0);
      cout << endl;
      commands::explain_usage(u.which, cout);
      clean_shutdown = true;
      return 0;
    }
  catch (informative_failure & inf)
    {
      ui.inform(inf.what + string("\n"));
      clean_shutdown = true;
      return 1;
    }
  catch (...)
    {
      // nb: we dump here because it's nicer to get the log dump followed
      // by the exception printout, when possible. this does *not* mean you
      // can remove the atexit() hook above, since it dumps when the
      // execution monitor traps sigsegv / sigabrt etc.
      global_sanity.dump_buffer();
      clean_shutdown = true;
      throw;
    }

  clean_shutdown = true;
  return ret;
}
