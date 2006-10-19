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
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include <boost/test/unit_test_suite.hpp>
#include <boost/test/parameterized_test.hpp>

#include "botan/botan.h"
#include "option.hh"
#include "unit_tests.hh"
#include "sanity.hh"
#include "ui.hh"

using std::multimap;
using std::pair;
using std::make_pair;
using std::vector;
using std::string;
using std::cout;
using std::cerr;
using std::clog;
using std::endl;
using std::exit;
using std::atexit;
using boost::unit_test::test_suite;
typedef boost::unit_test::test_case boost_unit_test_case;

// This must be a pointer.  It is used by the constructor below, which is
// used to construct file-scope objects in different files; if it were the
// actual object there would be no way to guarantee that this gets
// constructed first.  Instead, we initialize it ourselves the first time we
// use it.
typedef multimap<string const,
                 boost_unit_test_case *> unit_test_list_t;

static unit_test_list_t * unit_tests;

unit_test::unit_test_case::unit_test_case(char const * group,
                                          char const * test,
                                          void (*func)())
{
  if (unit_tests == NULL)
    unit_tests = new unit_test_list_t;

  boost_unit_test_case * tcase = BOOST_TEST_CASE(func);
  tcase->p_name.set(string(test));
  unit_tests->insert(make_pair(string(group), tcase));
}

// This appears to be the sanest way to get progress notifications on a
// per-test-group level.
static void notifier(string const & group)
{
  cerr << group << "..." << endl;
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

test_suite * init_unit_test_suite(int argc, char * argv[])
{
  bool help(false);
  bool list_groups(false);
  bool list_tests(false);
  bool debug(false);
  string log;
  vector<string> tests;
  using boost::lambda::var;
  using boost::lambda::bind;
  using boost::lambda::_1;
  using boost::function;
  try
    {
      option::concrete_option_set os;
      os("help,h", "display help message",
         function<void()>(var(help) = true))
        ("list-groups,l", "list all test groups",
         function<void()>(var(list_groups) = true))
        ("list-tests,L", "list all test cases",
         function<void()>(var(list_tests) = true))
        ("debug", "write verbose debug log to stderr",
         function<void()>(var(debug) = true))
        ("log", "write verbose debug log to this file"
         " (default is unit_tests.log)",
         function<void(string)>(var(log) = _1))
        ("", "", function<void(string)>(bind(&vector<string>::push_back,
                                             &tests, _1)));

      os.from_command_line(argc, argv);

      if (help)
        {
          cout << (FL("Usage: %s [options] [tests]\nOptions") % argv[0])
               << os.get_usage_str() << endl;
          exit(0);
        }
    }
  catch (option::option_error const & e)
    {
      cerr << argv[0] << ": " << e.what() << endl;
      exit(2);
    }

  if (list_groups && list_tests)
    {
      cerr << argv[0]
           << ": only one of --list-groups and --list-tests at a time"
           << endl;
      exit(2);
    }

  if (list_groups)
    {
      string last;
      for (unit_test_list_t::const_iterator i = unit_tests->begin();
           i != unit_tests->end();
           i++)
        if (last != (*i).first)
          {
            cout << (*i).first << "\n";
            last = (*i).first;
          }
      exit(0);
    }

  if (list_tests)
    {
      for (unit_test_list_t::const_iterator i = unit_tests->begin();
           i != unit_tests->end();
           i++)
        cout << (*i).first << ':' << (*i).second->p_name.get() << "\n";
      exit(0);
    }

  // If we get here, we are really running the test suite.
  test_suite * suite = BOOST_TEST_SUITE("monotone unit tests");

  if (tests.size() == 0) // run all tests
    {
      string last;
      for (unit_test_list_t::const_iterator i = unit_tests->begin();
           i != unit_tests->end();
           i++)
        {
          if (last != (*i).first)
            {
              suite->add(BOOST_PARAM_TEST_CASE(notifier,
                                               &((*i).first),
                                               &((*i).first)+1));
              last = (*i).first;
            }
          suite->add((*i).second);
        }
    }
  else
    {
      bool unrecognized = false;

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
            
          pair<unit_test_list_t::const_iterator,
            unit_test_list_t::const_iterator>
            range = unit_tests->equal_range(group);

          if (range.first == range.second)
            {
              unrecognized = true;
              cerr << argv[0] << ": unrecognized test group: " << group << endl;
              continue;
            }

          suite->add(BOOST_PARAM_TEST_CASE(notifier,
                                           &(*i), &(*i)+1));

          bool found = false;
          for (unit_test_list_t::const_iterator j = range.first;
               j != range.second;
               j++)
            if (test == "" || test == (*j).second->p_name.get())
              {
                suite->add((*j).second);
                found = true;
              }
          if (!found)
            {
              unrecognized = true;
              cerr << argv[0] << ": unrecognized test: "
                   << group << ':' << test << endl;
            }
        }

      if (unrecognized)
        exit(1);
    }

  // set up some global state before running the tests
  ui.initialize();
  ui.prog_name = argv[0];
  global_sanity.initialize(argc, argv, "C");  // we didn't call setlocale
  Botan::Init::initialize();

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
               << ": " << syserr << endl;
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
               << ": only one of --debug and --log at a time"
               << endl;
          exit(2);
        }

      // Make clog and cout use the same streambuf as cerr; this ensures
      // that all messages will appear in the order written, no matter what
      // stream each one is written to.
      clog.rdbuf(cerr.rdbuf());
      cout.rdbuf(cerr.rdbuf());
    }

  global_sanity.set_debug();
  return suite;
}

// Stub for options.cc's sake.
void
localize_monotone()
{
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
