#ifndef __UNIT_TESTS__
#define __UNIT_TESTS__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/test/test_tools.hpp>

// strangely this was left out. perhaps it'll arrive later?
#ifndef BOOST_CHECK_NOT_THROW
#define BOOST_CHECK_NOT_THROW(statement, exception)                     \
  do                                                                    \
    {                                                                   \
      try                                                               \
        {                                                               \
          statement;                                                    \
          BOOST_CHECK_MESSAGE(true, "exception "#exception" did not occur"); \
        }                                                               \
      catch(exception const &)                                          \
        {                                                               \
          BOOST_ERROR("exception "#exception" occurred");               \
        }                                                               \
    } while (0)
#endif

// Declarative mechanism for specifying unit tests, similar to
// auto_unit_test in boost, but more suited to our needs.
namespace unit_test {
  struct unit_test_case
  {
    unit_test_case(char const * group,
                   char const *test,
                   void (*func)());
                   
  };
}

// The names of the test functions must not collide with each other or with
// names of symbols in the code being tested, despite their being in a
// separate namespace, so that references _from_ the test functions _to_ the
// code under test resolve correctly.
#define UNIT_TEST(GROUP, TEST)                  \
  namespace unit_test {                         \
      static void t_##GROUP##_##TEST();         \
      static unit_test_case r_##GROUP##_##TEST  \
      (#GROUP, #TEST, t_##GROUP##_##TEST);      \
  }                                             \
  static void unit_test::t_##GROUP##_##TEST()

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
