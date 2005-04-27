// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// the idea of this file is that if we're very careful about the functions
// that are allowed to intern path components, and manipulate them when
// pulling them out again, then we don't have to do nearly so much sanity
// checking on them themselves.

// valid path components are:
//   ""
//   anything that's a valid file_path, but is only one element long
//   "MT", which is not a valid file_path, but is a valid path component
//     anyway

// this means that if we _start_ with a valid file_path, we can get valid
// path_component's by just doing a string split/join on "/".  except for
// noticing MT/, but we'll notice that anyway when reconstructing to a
// file_path.

#include <string>

#include <boost/filesystem/path.hpp>

#include "path_component.hh"
#include "interner.hh"

static interner<path_component> pc_interner;

// This function takes a vector of path components and joins them into a
// single file_path.  Valid input may be a single-element vector whose sole
// element is the empty path component (""); this represents the null path,
// which we use to represent non-existent files.  Alternatively, input may be
// a multi-element vector, in which case all elements of the vector are
// required to be non-null.  The following are valid inputs (with strings
// replaced by their interned version, of course):
//    - [""]
//    - ["foo"]
//    - ["foo", "bar"]
// The following are not:
//    - []
//    - ["foo", ""]
//    - ["", "bar"]
void
compose_path(std::vector<path_component> const & names,
             file_path & path)
{
  std::vector<path_component>::const_iterator i = names.begin();
  I(i != names.end());
  if (names.size() > 1)
    I(!null_name(*i));
  std::string path_str;
  path_str = pc_interner.lookup(*i);
  for (++i; i != names.end(); ++i)
    {
      I(!null_name(*i));
      path_str += "/";
      path_str += pc_interner.lookup(*i);
    }
  path = file_path(path_str);
}

//
// this takes a path of the form
//
//  "p[0]/p[1]/.../p[n-1]/p[n]"
//
// and fills in a vector of paths corresponding to p[0] ... p[n-1],
// along with, perhaps, a separate "leaf path" for element p[n]. 
//
// confusingly, perhaps, passing a null path ("") returns a zero-length
// components vector, rather than a length one vector with a single null
// component.
void
split_path(file_path const & p,
           std::vector<path_component> & components)
{
  components.clear();
  std::string const & p_str = p();
  if (p_str.empty())
    return;
  std::string::size_type start, stop;
  start = 0;
  while (1)
    {
      stop = p_str.find('/', start);
      if (stop < 0 || stop > p_str.length())
        {
          components.push_back(pc_interner.intern(p_str.substr(start)));
          break;
        }
      components.push_back(pc_interner.intern(p_str.substr(start, stop - start)));
      start = stop + 1;
    }
}

void
split_path(file_path const & p,
           std::vector<path_component> & prefix,
           path_component & leaf_path)
{
  split_path(p, prefix);
  I(prefix.size() > 0);
  leaf_path = prefix.back();
  prefix.pop_back();
}

path_component
make_null_component()
{
  static path_component null_pc = pc_interner.intern("");
  return null_pc;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"

static void
test_a_roundtrip(std::string const in)
{
  file_path old_fp = file_path(in);
  std::vector<path_component> vec;
  split_path(old_fp, vec);
  file_path new_fp;
  compose_path(vec, new_fp);
  BOOST_CHECK(old_fp == new_fp);
}

static void
roundtrip_tests()
{
  test_a_roundtrip("foo");
  test_a_roundtrip("foo/bar");
  test_a_roundtrip("foo/MT/bar");
}

static void
null_test()
{
  std::vector<path_component> vec;
  split_path(file_path(""), vec);
  BOOST_CHECK(vec.empty());
}

void
add_path_component_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(roundtrip_tests));
  suite->add(BOOST_TEST_CASE(null_test));
}

#endif  // BUILD_UNIT_TESTS
