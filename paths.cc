// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "constants.hh"
#include "paths.hh"
#include "platform.hh"
#include "sanity.hh"
#include "interner.hh"
#include "transforms.hh"

// some structure to ensure we aren't doing anything broken when resolving
// filenames.  the idea is to make sure
//   -- we don't depend on the existence of something before it has been set
//   -- we don't re-set something that has already been used
template <typename T>
struct access_tracker
{
  void set(T const & val, bool may_be_initialized)
  {
    I(may_be_initialized || !initialized);
    I(!used);
    initialized = true;
    value = val;
  }
  T const & get()
  {
    I(initialized);
    used = true;
    return value;
  }
  T const & get_but_unused()
  {
    I(initialized);
    return value;
  }
  // for unit tests
  void unset()
  {
    used = initialized = false;
  }
  T value;
  bool initialized, used;
  access_tracker() : initialized(false), used(false) {};
};

// paths to use in interpreting paths from various sources,
// conceptually:
//    working_root / initial_rel_path == initial_abs_path

// initial_abs_path is for interpreting relative system_path's
static access_tracker<system_path> initial_abs_path;
// initial_rel_path is for interpreting external file_path's
static access_tracker<file_path> initial_rel_path;
// working_root is for converting file_path's and bookkeeping_path's to
// system_path's.
static access_tracker<system_path> working_root;

bookkeeping_path const bookkeeping_root("MT");

void
save_initial_path()
{
  // FIXME: BUG: this only works if the current working dir is in utf8
  initial_abs_path.set(system_path(get_current_working_dir()), false);
  // We still use boost::fs, so let's continue to initialize it properly.
  fs::initial_path();
  L(F("initial abs path is: %s") % initial_abs_path.get_but_unused());
}

///////////////////////////////////////////////////////////////////////////
// verifying that internal paths are indeed normalized.
// this code must be superfast
///////////////////////////////////////////////////////////////////////////

// normalized means:
//  -- / as path separator
//  -- not an absolute path (on either posix or win32)
//     operationally, this means: first character != '/', first character != '\',
//     second character != ':'
//  -- no illegal characters
//     -- 0x00 -- 0x1f, 0x7f, \ are the illegal characters.  \ is illegal
//        unconditionally to prevent people checking in files on posix that
//        have a different interpretation on win32
//     -- (may want to allow 0x0a and 0x0d (LF and CR) in the future, but this
//        is blocked on manifest format changing)
//        (also requires changes to 'automate inventory', possibly others, to
//        handle quoting)
//  -- no doubled /'s
//  -- no trailing /
//  -- no "." or ".." path components
static inline bool
bad_component(std::string const & component)
{
  if (component == "")
    return true;
  if (component == ".")
    return true;
  if (component == "..")
    return true;
  return false;
}

static inline bool
fully_normalized_path(std::string const & path)
{
  // FIXME: probably should make this a 256-byte static lookup table
  const static std::string bad_chars = std::string("\\") + constants::illegal_path_bytes + std::string(1, '\0');
  
  // empty path is fine
  if (path.empty())
    return true;
  // : in second position => absolute path on windows
  if (path.size() > 1 && path[1] == ':')
    return false;
  // first scan for completely illegal bytes
  if (path.find_first_of(bad_chars) != std::string::npos)
    return false;
  // now check each component
  std::string::size_type start, stop;
  start = 0;
  while (1)
    {
      stop = path.find('/', start);
      if (stop == std::string::npos)
        {
          if (bad_component(path.substr(start)))
            return false;
          break;
        }
      if (bad_component(path.substr(start, stop - start)))
        return false;
      start = stop + 1;
    }
  return true;
}

static inline bool
in_bookkeeping_dir(std::string const & path)
{
  return path == "MT" || (path.size() >= 3 && (path.substr(0, 3) == "MT/"));
}

file_path::file_path(file_path::source_type type, std::string const & path)
{
  switch (type)
    {
    case internal:
      data = path;
      break;
    case external:
      fs::path tmp(initial_rel_path.get().as_internal());
      tmp /= fs::path(path, fs::native);
      tmp = tmp.normalize();
      data = utf8(tmp.string());
    }
  I(fully_normalized_path(data()));
  I(!in_bookkeeping_dir(data()));
}

bookkeeping_path::bookkeeping_path(std::string const & path)
{
  I(fully_normalized_path(path));
  I(in_bookkeeping_dir(path));
  data = path;
}

bool
bookkeeping_path::is_bookkeeping_path(std::string const & path)
{
  return in_bookkeeping_dir(path);
}

///////////////////////////////////////////////////////////////////////////
// splitting/joining
// this code must be superfast
// it depends very much on knowing that it can only be applied to fully
// normalized, relative, paths.
///////////////////////////////////////////////////////////////////////////

static interner<path_component> pc_interner("", the_null_component);

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
file_path::file_path(std::vector<path_component> const & pieces)
{
  std::vector<path_component>::const_iterator i = pieces.begin();
  I(i != pieces.end());
  if (pieces.size() > 1)
    I(!null_name(*i));
  std::string tmp = pc_interner.lookup(*i);
  for (++i; i != pieces.end(); ++i)
    {
      I(!null_name(*i));
      tmp += "/";
      tmp += pc_interner.lookup(*i);
    }
  data = tmp;
}

//
// this takes a path of the form
//
//  "p[0]/p[1]/.../p[n-1]/p[n]"
//
// and fills in a vector of paths corresponding to p[0] ... p[n-1]
//
// _unlike_ the old path splitting functions, this is the inverse to the above
// joining function.  the difference is that this code always returns a vector
// with at least one element in it; if you split the null path (""), you will
// get a single-element vector containing the null component.  with the old
// code, in this one case, you would have gotten an empty vector.
void
file_path::split(std::vector<path_component> & pieces) const
{
  pieces.clear();
  std::string::size_type start, stop;
  start = 0;
  std::string const & s = data();
  while (1)
    {
      stop = s.find('/', start);
      if (stop < 0 || stop > s.length())
        {
          pieces.push_back(pc_interner.intern(s.substr(start)));
          break;
        }
      pieces.push_back(pc_interner.intern(s.substr(start, stop - start)));
      start = stop + 1;
    }
}

///////////////////////////////////////////////////////////////////////////
// localizing file names (externalizing them)
// this code must be superfast when there is no conversion needed
///////////////////////////////////////////////////////////////////////////

std::string
any_path::as_external() const
{
#ifdef __APPLE__
  // on OS X paths for the filesystem/kernel are UTF-8 encoded, regardless of
  // locale.
  return data();
#else
  // on normal systems we actually have some work to do, alas.
  // not much, though, because utf8_to_system does all the hard work.  it is
  // carefully optimized.  do not screw it up.
  external out;
  utf8_to_system(data, out);
  return out();
#endif
}

///////////////////////////////////////////////////////////////////////////
// writing out paths
///////////////////////////////////////////////////////////////////////////

std::ostream &
operator <<(std::ostream & o, any_path const & a)
{
  o << a.as_internal();
  return o;
}

///////////////////////////////////////////////////////////////////////////
// path manipulation
// this code's speed does not matter much
///////////////////////////////////////////////////////////////////////////

file_path
file_path::operator /(std::string const & to_append) const
{
  if (empty())
    return file_path_internal(to_append);
  else
    return file_path_internal(data() + "/" + to_append);
}

bookkeeping_path
bookkeeping_path::operator /(std::string const & to_append) const
{
  if (empty())
    return bookkeeping_path(to_append);
  else
    return bookkeeping_path(data() + "/" + to_append);
}

system_path
system_path::operator /(std::string const & to_append) const
{
  I(!empty());
  return system_path(data() + "/" + to_append);
}

///////////////////////////////////////////////////////////////////////////
// system_path
///////////////////////////////////////////////////////////////////////////

static bool
is_absolute(std::string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
#ifdef _WIN32
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
#endif
  return false;
}

system_path::system_path(any_path const & other)
{
  I(!is_absolute(other.as_internal()));
  data = (working_root.get() / other.as_internal()).as_internal();
}

static inline std::string const_system_path(utf8 const & path)
{
  std::string expanded = tilde_expand(path)();
  if (is_absolute(expanded))
    return expanded;
  else
    return (initial_abs_path.get() / expanded).as_internal();
}

system_path::system_path(std::string const & path)
{
  data = const_system_path(path);
}

system_path::system_path(utf8 const & path)
{
  data = const_system_path(path);
}

///////////////////////////////////////////////////////////////////////////
// working copy (and path roots) handling
///////////////////////////////////////////////////////////////////////////

bool
find_and_go_to_working_copy(system_path const & search_root)
{
  // unimplemented
  fs::path root = search_root.as_external();
  fs::path bookdir = bookkeeping_root.as_external();
  fs::path current = fs::initial_path();
  fs::path removed;
  fs::path check = current / bookdir;
  
  L(F("searching for '%s' directory with root '%s'\n") 
    % bookdir.string()
    % root.string());

  while (current != root
         && current.has_branch_path()
         && current.has_leaf()
         && !fs::exists(check))
    {
      L(F("'%s' not found in '%s' with '%s' removed\n")
        % bookdir.string() % current.string() % removed.string());
      removed = fs::path(current.leaf(), fs::native) / removed;
      current = current.branch_path();
      check = current / bookdir;
    }

  L(F("search for '%s' ended at '%s' with '%s' removed\n") 
    % bookdir.string() % current.string() % removed.string());

  if (!fs::exists(check))
    {
      L(F("'%s' does not exist\n") % check.string());
      return false;
    }

  if (!fs::is_directory(check))
    {
      L(F("'%s' is not a directory\n") % check.string());
      return false;
    }

  // check for MT/. and MT/.. to see if mt dir is readable
  if (!fs::exists(check / ".") || !fs::exists(check / ".."))
    {
      L(F("problems with '%s' (missing '.' or '..')\n") % check.string());
      return false;
    }

  working_root.set(current.native_file_string(), true);
  initial_rel_path.set(file_path_internal(removed.string()), true);

  L(F("working root is '%s'") % working_root.get_but_unused());
  L(F("initial relative path is '%s'") % initial_rel_path.get_but_unused());

  change_current_working_dir(working_root.get_but_unused());

  return true;
}

void
go_to_working_copy(system_path const & new_working_copy)
{
  working_root.set(new_working_copy, true);
  initial_rel_path.set(file_path(), true);
  change_current_working_dir(new_working_copy);
}

///////////////////////////////////////////////////////////////////////////
// tests
///////////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void test_null_name()
{
  BOOST_CHECK(null_name(the_null_component));
}

static void test_file_path_internal()
{
  char const * baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "MT/blah",
                            "foo/bar/",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            0 };
  initial_rel_path.set(file_path(), true);
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path_internal(*c), std::logic_error);
  initial_rel_path.set(file_path_internal("blah/blah/blah"), true);
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path_internal(*c), std::logic_error);

  char const * goodies[] = {"",
                            "a",
                            "foo",
                            "foo/bar/baz",
                            "foo/bar.baz",
                            "foo/with-hyphen/bar",
                            "foo/with_underscore/bar",
                            ".foo/bar",
                            "..foo/bar",
                            "MTfoo/bar",
                            0 };
  
  for (int i = 0; i < 2; ++i)
    {
      initial_rel_path.set(i ? file_path()
                             : file_path_internal("blah/blah/blah"),
                           true);
      for (char const ** c = goodies; *c; ++c)
        {
          file_path fp = file_path_internal(*c);
          BOOST_CHECK(fp.as_internal() == *c);
          BOOST_CHECK(file_path_internal(fp.as_internal()) == fp);
          std::vector<path_component> split_test;
          fp.split(split_test);
          file_path fp2(split_test);
          BOOST_CHECK(fp == fp2);
          if (split_test.size() > 1)
            for (std::vector<path_component>::const_iterator i = split_test.begin();
                 i != split_test.end(); ++i)
              BOOST_CHECK(!null_name(*i));
        }
    }

  initial_rel_path.unset();
}

static void check_fp_normalizes_to(char * before, char * after)
{
  file_path fp = file_path_external(std::string(before));
  BOOST_CHECK(fp.as_internal() == after);
  BOOST_CHECK(file_path_internal(fp.as_internal()) == fp);
  // we compare after to the external form too, since as far as we know
  // relative normalized posix paths are always good win32 paths too
  BOOST_CHECK(fp.as_external() == after);
  std::vector<path_component> split_test;
  fp.split(split_test);
  file_path fp2(split_test);
  BOOST_CHECK(fp == fp2);
  for (std::vector<path_component>::const_iterator i = split_test.begin();
       i != split_test.end(); ++i)
    BOOST_CHECK(!null_name(*i));
}
  
static void test_file_path_external_no_prefix()
{
  initial_rel_path.set(file_path(), true);

  char const * baddies[] = {"/foo",
                            "../bar",
                            "MT/blah",
                            "MT",
                            "//blah",
                            "\\foo",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            0 };
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
  
  check_fp_normalizes_to("", "");
  check_fp_normalizes_to("a", "a");
  check_fp_normalizes_to("foo", "foo");
  check_fp_normalizes_to("foo/bar", "foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", ".foo/bar");
  check_fp_normalizes_to("..foo/bar", "..foo/bar");
  check_fp_normalizes_to(".", "");

  check_fp_normalizes_to("foo//bar", "foo/bar");
  check_fp_normalizes_to("foo/../bar", "bar");
  check_fp_normalizes_to("foo/bar/", "foo/bar");
  check_fp_normalizes_to("foo/./bar/", "foo/bar");
  check_fp_normalizes_to("./foo", "foo");
  check_fp_normalizes_to("foo///.//", "foo");

  initial_rel_path.unset();
}

static void test_file_path_external_prefix_a_b()
{
  initial_rel_path.set(file_path_internal("a/b"), true);

  char const * baddies[] = {"/foo",
                            "../../../bar",
                            "../../..",
                            "//blah",
                            "\\foo",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            "",
                            0 };
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
  
  check_fp_normalizes_to("foo", "a/b/foo");
  check_fp_normalizes_to("a", "a/b/a");
  check_fp_normalizes_to("foo/bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "a/b/foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "a/b/foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "a/b/foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "a/b/foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", "a/b/.foo/bar");
  check_fp_normalizes_to("..foo/bar", "a/b/..foo/bar");
  check_fp_normalizes_to(".", "a/b");
  // things that would have been bad without the initial_rel_path:
  check_fp_normalizes_to("foo//bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/../bar", "a/b/bar");
  check_fp_normalizes_to("foo/bar/", "a/b/foo/bar");
  check_fp_normalizes_to("foo/./bar/", "a/b/foo/bar");
  check_fp_normalizes_to("./foo", "a/b/foo");
  check_fp_normalizes_to("foo///.//", "a/b/foo");
  check_fp_normalizes_to("../foo", "a/foo");
  check_fp_normalizes_to("..", "a");
  check_fp_normalizes_to("../..", "");
  check_fp_normalizes_to("MT/foo", "a/b/MT/foo");
  check_fp_normalizes_to("MT", "a/b/MT");

  initial_rel_path.unset();
}

static void test_split_join()
{
  file_path fp1 = file_path_internal("foo/bar/baz");
  file_path fp2 = file_path_internal("bar/baz/foo");
  typedef std::vector<path_component> pcv;
  pcv split1, split2;
  fp1.split(split1);
  fp2.split(split2);
  BOOST_CHECK(fp1 == file_path(split1));
  BOOST_CHECK(fp2 == file_path(split2));
  BOOST_CHECK(!(fp1 == file_path(split2)));
  BOOST_CHECK(!(fp2 == file_path(split1)));
  BOOST_CHECK(split1.size() == 3);
  BOOST_CHECK(split2.size() == 3);
  BOOST_CHECK(split1[0] != split1[1]);
  BOOST_CHECK(split1[0] != split1[2]);
  BOOST_CHECK(split1[1] != split1[2]);
  BOOST_CHECK(!null_name(split1[0])
              && !null_name(split1[1])
              && !null_name(split1[2]));
  BOOST_CHECK(split1[0] == split2[2]);
  BOOST_CHECK(split1[1] == split2[0]);
  BOOST_CHECK(split1[2] == split2[1]);

  file_path fp3 = file_path_internal("");
  pcv split3;
  fp3.split(split3);
  BOOST_CHECK(split3.size() == 1);
  BOOST_CHECK(null_name(split3[0]));
  BOOST_CHECK(fp3 == file_path(split3));

  pcv split4;
  BOOST_CHECK_THROW(file_path(split4), std::logic_error);
  split4.push_back(the_null_component);
  BOOST_CHECK_NOT_THROW(file_path(split4), std::logic_error);
  split4.clear();
  split4.push_back(split1[0]);
  split4.push_back(the_null_component);
  split4.push_back(split1[0]);
  BOOST_CHECK_THROW(file_path(split4), std::logic_error);
}

static void check_bk_normalizes_to(char * before, char * after)
{
  bookkeeping_path bp(bookkeeping_root / before);
  BOOST_CHECK(bp.as_external() == after);
  BOOST_CHECK(bookkeeping_path(bp.as_internal()).as_internal() == bp.as_internal());
}

static void test_bookkeeping_path()
{
  char const * baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "foo/bar/",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            "",
                            0 };
  
  for (char const ** c = baddies; *c; ++c)
    {
      BOOST_CHECK_THROW(bookkeeping_path(std::string(*c)), std::logic_error);
      BOOST_CHECK_THROW(bookkeeping_root / std::string(*c), std::logic_error);
    }
  BOOST_CHECK_THROW(bookkeeping_path(std::string("foo/bar")), std::logic_error);
  BOOST_CHECK_THROW(bookkeeping_path(std::string("a")), std::logic_error);
  
  check_bk_normalizes_to("a", "MT/foo");
  check_bk_normalizes_to("foo", "MT/foo");
  check_bk_normalizes_to("foo/bar", "MT/foo/bar");
  check_bk_normalizes_to("foo/bar/baz", "MT/foo/bar/baz");
}

static void check_system_normalizes_to(char * before, char * after)
{
  system_path sp(before);
  BOOST_CHECK(sp.as_external() == after);
  BOOST_CHECK(system_path(sp.as_internal()).as_internal() == sp.as_internal());
}

static void test_system_path()
{
  initial_abs_path.set(system_path("/a/b"), true);

  BOOST_CHECK_THROW(system_path(""), informative_failure);

  check_system_normalizes_to("foo", "/a/b/foo");
  check_system_normalizes_to("foo/bar", "/a/b/foo/bar");
  check_system_normalizes_to("/foo/bar", "/foo/bar");
  check_system_normalizes_to("//foo/bar", "/foo/bar");
#ifdef _WIN32
  check_system_normalizes_to("c:foo", "c:foo");
  check_system_normalizes_to("c:/foo", "c:/foo");
  check_system_normalizes_to("c:\\foo", "c:\\foo");
#else
  check_system_normalizes_to("c:foo", "a/b/c:foo");
  check_system_normalizes_to("c:/foo", "a/b/c:/foo");
  check_system_normalizes_to("c:\\foo", "a/b/c:\\foo");
#endif
  // can't do particularly interesting checking of tilde expansion, but at
  // least we can check that it's doing _something_...
  std::string tilde_expanded = system_path("~/foo").as_external();
  BOOST_CHECK(tilde_expanded[0] == '/');
  BOOST_CHECK(tilde_expanded.find('~') == std::string::npos);
  // and check for the weird WIN32 version
#ifdef _WIN32
  std::string tilde_expanded2 = system_path("~this_user_does_not_exist_anywhere").as_external();
  BOOST_CHECK(tilde_expanded2[0] = '/');
  BOOST_CHECK(tilde_expanded.find('~') == std::string::npos);
#else
  BOOST_CHECK_THROW(system_path("~this_user_does_not_exist_anywhere"), informative_failure);
#endif

  // finally, make sure that the copy-from-any_path constructor works right
  // in particular, it should interpret the paths it gets as being relative to
  // the project root, not the initial path
  working_root.set(system_path("/working/root"), true);
  initial_rel_path.set(file_path_internal("rel/initial"), true);

  BOOST_CHECK(system_path(system_path("foo/bar")).as_internal() == "/a/b/foo/bar");
  BOOST_CHECK(system_path(system_path("/foo/bar")).as_internal() == "/foo/bar");
  BOOST_CHECK(system_path(file_path_internal("foo/bar")).as_internal()
              == "/working/root/foo/bar");
  BOOST_CHECK(system_path(file_path_external(std::string("foo/bar"))).as_external()
              == "/working/root/rel/initial/foo/bar");
  BOOST_CHECK(system_path(bookkeeping_path("MT/foo/bar")).as_internal()
              == "/working/root/MT/foo/bar");

  initial_abs_path.unset();
  working_root.unset();
  initial_rel_path.unset();
}

static void test_access_tracker()
{
  access_tracker<int> a;
  BOOST_CHECK_THROW(a.get(), std::logic_error);
  a.set(1, false);
  BOOST_CHECK_THROW(a.set(2, false), std::logic_error);
  a.set(2, true);
  BOOST_CHECK_THROW(a.set(3, false), std::logic_error);
  BOOST_CHECK(a.get() == 2);
  BOOST_CHECK_THROW(a.set(3, true), std::logic_error);
}

void add_paths_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_null_name));
  suite->add(BOOST_TEST_CASE(&test_file_path_internal));
  suite->add(BOOST_TEST_CASE(&test_file_path_external_no_prefix));
  suite->add(BOOST_TEST_CASE(&test_file_path_external_prefix_a_b));
  suite->add(BOOST_TEST_CASE(&test_split_join));
  suite->add(BOOST_TEST_CASE(&test_bookkeeping_path));
  suite->add(BOOST_TEST_CASE(&test_system_path));
  suite->add(BOOST_TEST_CASE(&test_access_tracker));
}

#endif // BUILD_UNIT_TESTS
