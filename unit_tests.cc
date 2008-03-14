// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include "vector.hh"
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
#include "botan_pipe_cache.hh"

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
                                          void (*func)(),
                                          bool fis)
  : group(group), name(name), func(func), failure_is_success(fis)
{
  unit_tests()[group][name] = *this;
}

unit_test::unit_test_case::unit_test_case()
{}

// Test state.
static bool this_test_failed = false;

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

// define the global objects needed by botan_pipe_cache.hh
pipe_cache_cleanup * global_pipe_cleanup_object;
Botan::Pipe * unfiltered_pipe;
static unsigned char unfiltered_pipe_cleanup_mem[sizeof(cached_botan_pipe)];

int main(int argc, char * argv[])
{
  bool help(false);
  string test_to_run;

  ui.initialize();
  ui.prog_name = argv[0];
  global_sanity.initialize(argc, argv, "C");  // we didn't call setlocale

  try
    {
      option::concrete_option_set os;
      os("help,h", "display help message", option::setter(help))
        ("--", "", option::setter(test_to_run));

      os.from_command_line(argc, argv);

      if (help)
        {
          cout << (FL("Usage: %s [-h|--help] [test]\n"
                      "  With no arguments, lists all test cases.\n"
                      "  With the name of a test case, runs that test.\n"
                      "  -h or --help prints this message.\n") % argv[0]);
          return 0;
        }
    }
  catch (option::option_error const & e)
    {
      cerr << argv[0] << ": " << e.what() << '\n';
      return 2;
    }

  if (test_to_run == "")
    {
      for (group_list_t::const_iterator i = unit_tests().begin();
           i != unit_tests().end(); i++)
        for (test_list_t::const_iterator j = i->second.begin();
             j != i->second.end(); ++j)
          cout << i->first << ":" << j->first << "\n";

      return 0;
    }


  // set up some global state before running the tests
  // keep this in sync with monotone.cc, except for selftest=1 here, =0 there
  Botan::LibraryInitializer acquire_botan("thread_safe=0 selftest=1 "
                                          "seed_rng=1 use_engines=0 "
                                          "secure_memory=1 fips140=0");
  // and caching for botan pipes
  pipe_cache_cleanup acquire_botan_pipe_caching;
  unfiltered_pipe = new Botan::Pipe;
  new (unfiltered_pipe_cleanup_mem) cached_botan_pipe(unfiltered_pipe);

  // Make clog and cout use the same streambuf as cerr; this ensures
  // that all messages will appear in the order written, no matter what
  // stream each one is written to.
  clog.rdbuf(cerr.rdbuf());
  cout.rdbuf(cerr.rdbuf());

  global_sanity.set_debug();

  string::size_type sep = test_to_run.find(":");

  if (sep == string::npos) // it's a group name
    {
      cerr << argv[0] << ": must specify a test, not a group, to run\n";
      return 2;
    }

  string group, test;
  group = test_to_run.substr(0, sep);
  test = test_to_run.substr(sep+1, string::npos);
            
  group_list_t::const_iterator g = unit_tests().find(group);

  if (g == unit_tests().end())
    {
      cerr << argv[0] << ": unrecognized test group: "
           << group << '\n';
      return 2;
    }

  test_list_t::const_iterator t = g->second.find(test);
  if (t == g->second.end())
    {
      cerr << argv[0] << ": unrecognized test: "
           << group << ':' << test << '\n';
      return 2;
    }

  L(FL("Beginning test %s:%s") % group % test);

  try
    {
      t->second.func();
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

  if (this_test_failed && !t->second.failure_is_success)
    {
      L(FL("Test %s:%s failed.\n") % group % test);
      return 1;
    }
  else
    {
      L(FL("Test %s:%s succeeded.\n") % group % test);
      return 0;
    }
}

// Stub for options.cc's sake.
void
localize_monotone()
{
}

// These are tests of the unit testing mechanism itself.  They would all
// fail, but we make use of a special mechanism to convert that failure
// into a success.  Since we don't want that mechanism used elsewhere,
// the necessary definition macro is defined here and not in unit_test.hh.

#define NEGATIVE_UNIT_TEST(GROUP, TEST)           \
  namespace unit_test {                           \
      static void t_##GROUP##_##TEST();           \
      static unit_test_case r_##GROUP##_##TEST    \
      (#GROUP, #TEST, t_##GROUP##_##TEST, true);  \
  }                                               \
  static void unit_test::t_##GROUP##_##TEST()

#include <stdexcept>

NEGATIVE_UNIT_TEST(_unit_tester, fail_check)
{
  UNIT_TEST_CHECKPOINT("checkpoint");
  UNIT_TEST_CHECK(false);
  UNIT_TEST_CHECK(false);
}

NEGATIVE_UNIT_TEST(_unit_tester, fail_require)
{
  UNIT_TEST_CHECKPOINT("checkpoint");
  UNIT_TEST_REQUIRE(false);
  UNIT_TEST_CHECK(false);
}

NEGATIVE_UNIT_TEST(_unit_tester, fail_throw)
{
  UNIT_TEST_CHECK_THROW(string().size(), int);
}

NEGATIVE_UNIT_TEST(_unit_tester, fail_nothrow)
{
  UNIT_TEST_CHECK_NOT_THROW(throw int(), int);
}

NEGATIVE_UNIT_TEST(_unit_tester, uncaught)
{
  throw int();
}

NEGATIVE_UNIT_TEST(_unit_tester, uncaught_std)
{
  throw std::bad_exception();
}

NEGATIVE_UNIT_TEST(_unit_tester, uncaught_std_what)
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
