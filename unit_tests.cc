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
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <boost/function.hpp>

#include "botan/botan.h"
#include "option.hh"
#include "unit_tests.hh"
#include "sanity.hh"
#include "ui.hh"

using std::map;
using std::pair;
using std::make_pair;
using std::vector;
using std::string;
using std::cout;
using std::cerr;
using std::clog;
using std::exit;
using std::atexit;

typedef unit_test::unit_test_case test_t;
typedef map<string const, test_t> test_list_t;
typedef map<string const, test_list_t> group_list_t;

// This is used by other global constructors, so initialize on demand.
group_list_t & unit_tests()
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
bool any_test_failed = false;
int number_of_failed_tests = 0;
int number_of_succeeded_tests = 0;

// Test state.
bool this_test_failed;
string last_checkpoint;

struct require_failed {};

void note_checkpoint(char const * file, int line,
                     char const * kind, string const & msg)
{
  last_checkpoint = (FL("%s:%s> %s: %s")
                     % file % line % kind % msg).str();
}

void log_checkpoint()
{
  if (last_checkpoint == "")
    return;
  cerr<<"Last checkpoint: "<<last_checkpoint<<"\n";
  last_checkpoint = "";
}

void log_fail(char const * file, int line,
              char const * kind, string const & msg)
{
  cerr<<FL("%s:%s> %s: %s\n") % file % line % kind % msg;
  log_checkpoint();
}

void unit_test::do_check(bool checkval, char const * file,
                         int line, char const * message)
{
  if (!checkval)
    {
      this_test_failed = true;
      log_fail(file, line, "CHECK FAILED", message);
    }
  else
    note_checkpoint(file, line, "CHECK OK", message);
}

void unit_test::do_require(bool checkval, char const * file,
                           int line, char const * message)
{
  if (!checkval)
    {
      this_test_failed = true;
      log_fail(file, line, "REQUIRE FAILED", message);
    }
  else
    note_checkpoint(file, line, "REQUIRE OK", message);
}

void unit_test::do_checkpoint(char const * file, int line,
                              string const & message)
{
  note_checkpoint(file, line, "CHECKPOINT", message);
}

void run_test(test_t test)
{
  this_test_failed = false;
  last_checkpoint = "";

  cerr<<"----------------------------------------\n";
  cerr<<(FL("Beginning test %s:%s\n") % test.group % test.name);

  try
    {
      test.func();
    }
  catch(require_failed &)
    {
      this_test_failed = true;
    }
  catch(...)
    {
      cerr<<test.group<<":"<<test.name<<"> UNCAUGHT EXCEPTION\n";
      this_test_failed = true;
    }

  if (this_test_failed)
    {
      ++number_of_failed_tests;
      cerr<<(FL("Test %s:%s failed.\n") % test.group % test.name);
      log_checkpoint();
    }
  else
    {
      ++number_of_succeeded_tests;
      cerr<<(FL("Test %s:%s succeeded.\n") % test.group % test.name);
    }

  if (this_test_failed)
    any_test_failed = true;
}


// A teebuf implements the basic_streambuf interface and forwards all
// operations to two 'targets'.  This is used to get progress messages sent
// to both the log file and the terminal.  Note that it cannot be used for
// reading (and does not implement the read-side interfaces) nor is it
// seekable.
namespace {
  template <class C, class T = std::char_traits<C> >
  class basic_teebuf : public std::basic_streambuf<C, T>
  {
  public:
    // grmbl grmbl typedefs not inherited grmbl.
    typedef C char_type;
    typedef T traits_type;
    typedef typename T::int_type int_type;
    typedef typename T::pos_type pos_type;
    typedef typename T::off_type off_type;

    virtual ~basic_teebuf() {}
    basic_teebuf(std::basic_streambuf<C,T> * t1,
                 std::basic_streambuf<C,T> * t2)
      : std::basic_streambuf<C,T>(), target1(t1), target2(t2) {}

  protected:
    std::basic_streambuf<C, T> * target1;
    std::basic_streambuf<C, T> * target2;

    virtual void imbue(std::locale const & loc)
    {
      target1->pubimbue(loc);
      target2->pubimbue(loc);
    }
    virtual basic_teebuf * setbuf(C * p, std::streamsize n)
    {
      target1->pubsetbuf(p,n);
      target2->pubsetbuf(p,n);
      return this;
    }
    virtual int sync()
    {
      int r1 = target1->pubsync();
      int r2 = target2->pubsync();
      return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

    // Not overriding the seek, get, or putback functions produces a
    // streambuf which always fails those operations, thanks to the defaults
    // in basic_streambuf.

    // As we do no buffering in this object, it would be correct to forward
    // xsputn to the targets.  However, this could cause a headache in the
    // case that one target consumed fewer characters than the other.  As
    // this streambuf is not used for much data (most of the chatter goes to
    // clog) it is okay to fall back on the dumb-but-reliable default xsputn
    // in basic_streambuf.

    // You might think that overflow in this object should forward to
    // overflow in the targets, but that would defeat buffering in those
    // objects.
    virtual int_type overflow(int_type c = traits_type::eof())
    {
      if (!traits_type::eq_int_type(c,traits_type::eof()))
        {
          int_type r1 = target1->sputc(c);
          int_type r2 = target2->sputc(c);
          if (r1 == traits_type::eof() || r2 == traits_type::eof())
            return traits_type::eof();
          return traits_type::not_eof(c);
        }
      else
        {
          return sync() ? traits_type::eof() : traits_type::not_eof(c);
        }
    }
  };
  typedef basic_teebuf<char> teebuf;
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

      // Redirect both cerr and cout to a teebuf which will send their data
      // to both the logfile and wherever cerr currently goes.
      teebuf * progress_output = new teebuf(logbuf, cerr.rdbuf());
      cerr.rdbuf(progress_output);
      cout.rdbuf(progress_output);
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
    }

  global_sanity.set_debug();


  if (tests.size() == 0) // run all tests
    {
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
                       << group << '\n';
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
          for (vector<test_t>::const_iterator i = to_run.begin();
               i != to_run.end(); ++i)
            {
              run_test(*i);
            }
        }
    }

  cerr<<FL("Number of tests that failed: %d\n") % number_of_failed_tests;
  cerr<<FL("Number of tests that succeeded: %d\n") % number_of_succeeded_tests;

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


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
