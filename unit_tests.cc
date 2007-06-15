// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <boost/function.hpp>

#include "botan/botan.h"
#include "option.hh"
#include "unit_tests.hh"
#include "sanity.hh"
#include "ui.hh"
#include "current_exception.hh"

using std::map;
using std::pair;
using std::make_pair;
using std::vector;
using std::string;
using std::cout;
using std::cerr;
using std::clog;
using std::exit;

typedef unit_test::unit_test_case test_t;
typedef map<string const, test_t> test_list_t;
typedef map<string const, test_list_t> group_list_t;

// This is used by other global constructors, so initialize on demand.
static group_list_t & unit_tests()
{
  static group_list_t tests;
  return tests;
}

unit_test::unit_test_case::unit_test_case(char const * group,
                                          char const * name,
                                          void (*func)())
  : group(group), name(name), func(func)
{
  unit_tests()[group][name] = *this;
}

unit_test::unit_test_case::unit_test_case()
{}

// Testsuite state.
static bool any_test_failed = false;
static int number_of_failed_tests = 0;
static int number_of_succeeded_tests = 0;
static bool log_to_stderr = false;

// Test state.
static bool this_test_failed;

namespace { struct require_failed {}; }

static void log_state(char const * file, int line,
                      char const * kind, char const * msg)
{
  L(FL("%s:%s: %s: %s") % file % line % kind % msg);
}

// Report what we can about a fatal exception (caught in the outermost catch
// handlers) which is from the std::exception hierarchy.  In this case we
// can access the exception object.
static void log_exception(std::exception const & ex)
{
  using std::strcmp;
  using std::strncmp;
  char const * ex_name = typeid(ex).name();
  char const * ex_dem  = demangle_typename(ex_name);
  char const * ex_what = ex.what();

  if (ex_dem == 0)
    ex_dem = ex_name;

  // some demanglers stick "class" at the beginning of their output,
  // which looks dumb in this context
  if (!strncmp(ex_dem, "class ", 6))
    ex_dem += 6;

  // only print what() if it's interesting, i.e. nonempty and different
  // from the name (mangled or otherwise) of the exception type.
  if (ex_what == 0 || ex_what[0] == 0
      || !strcmp(ex_what, ex_name)
      || !strcmp(ex_what, ex_dem))
    L(FL("UNCAUGHT EXCEPTION: %s") % ex_dem);
  else
    L(FL("UNCAUGHT EXCEPTION: %s: %s") % ex_dem % ex_what);
}

// Report what we can about a fatal exception (caught in the outermost catch
// handlers) which is of unknown type.  If we have the <cxxabi.h> interfaces,
// we can at least get the type_info object.
static void
log_exception()
{
  std::type_info *ex_type = get_current_exception_type();
  if (ex_type)
    {
      char const * ex_name = ex_type->name();
      char const * ex_dem  = demangle_typename(ex_name);
      if (ex_dem == 0)
        ex_dem = ex_name;
      L(FL("UNCAUGHT EXCEPTION: %s") % ex_dem);
    }
  else
    L(FL("UNCAUGHT EXCEPTION: unknown type"));
}

void unit_test::do_check(bool checkval, char const * file,
                         int line, char const * message)
{
  if (!checkval)
    {
      this_test_failed = true;
      log_state(file, line, "CHECK FAILED", message);
    }
  else
    log_state(file, line, "CHECK OK", message);
}

void unit_test::do_require(bool checkval, char const * file,
                           int line, char const * message)
{
  if (!checkval)
    {
      this_test_failed = true;
      log_state(file, line, "REQUIRE FAILED", message);
      throw require_failed();
    }
  else
    log_state(file, line, "REQUIRE OK", message);
}

void unit_test::do_checkpoint(char const * file, int line,
                              char const * message)
{
  log_state(file, line, "CHECKPOINT", message);
}

static void run_test(test_t test)
{
  this_test_failed = false;

  L(FL("----------------------------------------\n"
       "Beginning test %s:%s") % test.group % test.name);

  if (!log_to_stderr)
    {
      string groupname = string(test.group) + ':' + test.name;
      cerr << "    " << std::left << std::setw(46) << groupname;
      if (groupname.size() >= 46)
        cerr << " ";
      // lack of carriage return is intentional
    }
    
  try
    {
      test.func();
    }
  catch(require_failed &)
    {
      // no action required
    }
  catch(std::exception & e)
    {
      log_exception(e);
      this_test_failed = true;
    }
  catch(...)
    {
      log_exception();
      this_test_failed = true;
    }

  if (this_test_failed)
    {
      ++number_of_failed_tests;
      L(FL("Test %s:%s failed.\n") % test.group % test.name);
      if (!log_to_stderr)
        cerr << "FAIL\n";
    }
  else
    {
      ++number_of_succeeded_tests;
      L(FL("Test %s:%s succeeded.\n") % test.group % test.name);
      if (!log_to_stderr)
        cerr << "ok\n";
    }

  if (this_test_failed)
    any_test_failed = true;
}

int main(int argc, char * argv[])
{
  bool help(false);
  bool list_groups(false);
  bool list_tests(false);
  bool debug(false);
  string log;
  vector<string> tests;

  try
    {
      option::concrete_option_set os;
      os("help,h", "display help message", option::setter(help))
        ("list-groups,l", "list all test groups", option::setter(list_groups))
        ("list-tests,L", "list all test cases", option::setter(list_tests))
        ("debug", "write verbose debug log to stderr", option::setter(debug))
        ("log", "write verbose debug log to this file"
         " (default is unit_tests.log)", option::setter(log))
        ("--", "", option::setter(tests));

      os.from_command_line(argc, argv);

      if (help)
        {
          cout << (FL("Usage: %s [options] [tests]\nOptions") % argv[0])
               << os.get_usage_str() << '\n';
          exit(0);
        }
    }
  catch (option::option_error const & e)
    {
      cerr << argv[0] << ": " << e.what() << '\n';
      exit(2);
    }

  if (list_groups && list_tests)
    {
      cerr << argv[0]
           << ": only one of --list-groups and --list-tests at a time\n";
      exit(2);
    }

  if (list_groups)
    {
      for (group_list_t::const_iterator i = unit_tests().begin();
           i != unit_tests().end(); i++)
        {
          if (i->first != "")
            cout << i->first << '\n';
        }
      return 0;
    }

  if (list_tests)
    {
      for (group_list_t::const_iterator i = unit_tests().begin();
           i != unit_tests().end(); i++)
        {
          if (i->first == "")
            continue;
          for (test_list_t::const_iterator j = i->second.begin();
               j != i->second.end(); ++j)
            {
              cout << i->first << ":" << j->first << "\n";
            }
        }
      return 0;
    }


  // set up some global state before running the tests
  ui.initialize();
  ui.prog_name = argv[0];
  global_sanity.initialize(argc, argv, "C");  // we didn't call setlocale
  Botan::LibraryInitializer::initialize();

  if (!debug)
    {
      // We would _like_ to use ui.redirect_log_to() but it takes a
      // system_path and we're not set up to use that here.
      char const * logname;
      if (log.empty())
        logname = "unit_tests.log";
      else
        logname = log.c_str();

      std::filebuf * logbuf = new std::filebuf;
      if (!logbuf->open(logname, std::ios_base::out|std::ios_base::trunc))
        {
          char const * syserr = std::strerror(errno);
          cerr << argv[0] << ": failed to open " << logname
               << ": " << syserr << '\n';
          exit(1);
        }
      clog.rdbuf(logbuf);
      // Nobody should be writing to cout, but just in case, send it to
      // the log.
      cout.rdbuf(logbuf);
    }
  else
    {
      if (!log.empty())
        {
          cerr << argv[0]
               << ": only one of --debug and --log at a time\n";
          exit(2);
        }

      // Make clog and cout use the same streambuf as cerr; this ensures
      // that all messages will appear in the order written, no matter what
      // stream each one is written to.
      clog.rdbuf(cerr.rdbuf());
      cout.rdbuf(cerr.rdbuf());

      // Suppress double progress messages.
      log_to_stderr = true;
    }

  global_sanity.set_debug();

  if (tests.size() == 0) // run all tests
    {
      if (!log_to_stderr)
        cerr << "Running unit tests...\n";
      
      for (group_list_t::const_iterator i = unit_tests().begin();
           i != unit_tests().end(); i++)
        {
          if (i->first == "")
            continue;
          for (test_list_t::const_iterator j = i->second.begin();
               j != i->second.end(); ++j)
            {
              run_test(j->second);
            }
        }
    }
  else
    {
      bool unrecognized = false;

      vector<test_t> to_run;

      for(vector<string>::const_iterator i = tests.begin();
          i != tests.end();
          i++)
        {
          string group, test;
          string::size_type sep = (*i).find(":");

          if (sep >= (*i).length()) // it's a group name
            group = *i;
          else
            {
              group = (*i).substr(0, sep);
              test = (*i).substr(sep+1, string::npos);
            }
            
          group_list_t::const_iterator g = unit_tests().find(group);

          if (g == unit_tests().end())
            {
              unrecognized = true;
              cerr << argv[0] << ": unrecognized test group: "
                   << group << '\n';
              continue;
            }

          if (test == "") // entire group
            {
              for (test_list_t::const_iterator t = g->second.begin();
                   t != g->second.end(); ++t)
                {
                  to_run.push_back(t->second);
                }
            }
          else
            {
              test_list_t::const_iterator t = g->second.find(test);
              if (t == g->second.end())
                {
                  unrecognized = true;
                  cerr << argv[0] << ": unrecognized test: "
                       << group << ':' << test << '\n';
                  continue;
                }
              to_run.push_back(t->second);
            }
        }

      if (unrecognized)
        {
          return 1;
        }
      else
        {
          if (!log_to_stderr)
            cerr << "Running unit tests...\n";
          for (vector<test_t>::const_iterator i = to_run.begin();
               i != to_run.end(); ++i)
            {
              run_test(*i);
            }
        }
    }

  if (!log_to_stderr)
    cerr << "\nOf " << (number_of_failed_tests + number_of_succeeded_tests)
         << " tests run:\n\t"
         << number_of_succeeded_tests << " succeeded\n\t"
         << number_of_failed_tests << " failed\n";

  return any_test_failed?1:0;
}

// Stub for options.cc's sake.
void
localize_monotone()
{
}


// Obviously, these tests are not run by default.
// They are also not listed by --list-groups or --list-tests .
// Use "unit_tests :" to run them; they should all fail.
#include <stdexcept>

UNIT_TEST(, fail_check)
{
  UNIT_TEST_CHECKPOINT("checkpoint");
  UNIT_TEST_CHECK(false);
  UNIT_TEST_CHECK(false);
}

UNIT_TEST(, fail_require)
{
  UNIT_TEST_CHECKPOINT("checkpoint");
  UNIT_TEST_REQUIRE(false);
  UNIT_TEST_CHECK(false);
}

UNIT_TEST(, fail_throw)
{
  UNIT_TEST_CHECK_THROW(string().size(), int);
}

UNIT_TEST(, fail_nothrow)
{
  UNIT_TEST_CHECK_NOT_THROW(throw int(), int);
}

UNIT_TEST(, uncaught)
{
  throw int();
}

UNIT_TEST(, uncaught_std)
{
  throw std::bad_exception();
}

UNIT_TEST(, uncaught_std_what)
{
  throw std::runtime_error("There is no spoon.");
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
