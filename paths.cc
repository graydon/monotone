// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <string>
#include <sstream>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "constants.hh"
#include "paths.hh"
#include "platform-wrapped.hh"
#include "sanity.hh"
#include "interner.hh"
#include "charset.hh"
#include "simplestring_xform.hh"

using std::exception;
using std::ostream;
using std::ostringstream;
using std::string;


// some structure to ensure we aren't doing anything broken when resolving
// filenames.  the idea is to make sure
//   -- we don't depend on the existence of something before it has been set
//   -- we don't re-set something that has already been used
//   -- sometimes, we use the _non_-existence of something, so we shouldn't
//      set anything whose un-setted-ness has already been used
template <typename T>
struct access_tracker
{
  void set(T const & val, bool may_be_initialized)
  {
    I(may_be_initialized || !initialized);
    I(!very_uninitialized);
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
  void may_not_initialize()
  {
    I(!initialized);
    very_uninitialized = true;
  }
  // for unit tests
  void unset()
  {
    used = initialized = very_uninitialized = false;
  }
  T value;
  bool initialized, used, very_uninitialized;
  access_tracker() : initialized(false), used(false), very_uninitialized(false) {};
};

// paths to use in interpreting paths from various sources,
// conceptually:
//    working_root / initial_rel_path == initial_abs_path

// initial_abs_path is for interpreting relative system_path's
static access_tracker<system_path> initial_abs_path;
// initial_rel_path is for interpreting external file_path's
// for now we just make it an fs::path for convenience; we used to make it a
// file_path, but then you can't run monotone from inside the _MTN/ dir (even
// when referring to files outside the _MTN/ dir).
static access_tracker<fs::path> initial_rel_path;
// working_root is for converting file_path's and bookkeeping_path's to
// system_path's.
static access_tracker<system_path> working_root;

bookkeeping_path const bookkeeping_root("_MTN");
path_component const bookkeeping_root_component("_MTN");

// this is a file_path because it does not conform to the invariant that
// bookkeeping paths always start with the _current_ bookkeeping root.
file_path const old_bookkeeping_root = file_path_internal("MT");

void
save_initial_path()
{
  // FIXME: BUG: this only works if the current working dir is in utf8
  initial_abs_path.set(system_path(get_current_working_dir()), false);
  // We still use boost::fs, so let's continue to initialize it properly.
  fs::initial_path();
  fs::path::default_name_check(fs::native);
  L(FL("initial abs path is: %s") % initial_abs_path.get_but_unused());
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
bad_component(string const & component)
{
  static const string dot(".");
  static const string dotdot("..");
  if (component.empty())
    return true;
  if (component == dot)
    return true;
  if (component == dotdot)
    return true;
  return false;
}

static inline bool
has_bad_chars(string const & path)
{
  for (string::const_iterator c = path.begin(); LIKELY(c != path.end()); c++)
    {
      // char is often a signed type; convert to unsigned to ensure that
      // bytes 0x80-0xff are considered > 0x1f.
      u8 x = (u8)*c;
      // 0x5c is '\\'; we use the hex constant to make the dependency on
      // ASCII encoding explicit.
      if (UNLIKELY(x <= 0x1f || x == 0x5c || x == 0x7f))
        return true;
    }
  return false;
}

// fully_normalized_path performs very similar function to file_path.split().
// if want_split is set, split_path will be filled with the '/' separated
// components of the path.
static inline bool
fully_normalized_path_split(string const & path, bool want_split,
                            split_path & sp)
{
  // empty path is fine
  if (path.empty())
    return true;
  // could use is_absolute_somewhere, but this is the only part of it that
  // wouldn't be redundant
  if (path.size() > 1 && path[1] == ':')
    return false;
  // first scan for completely illegal bytes
  if (has_bad_chars(path))
    return false;
  // now check each component
  string::size_type start, stop;
  start = 0;
  while (1)
    {
      stop = path.find('/', start);
      if (stop == string::npos)
        {
          string const & s(path.substr(start));
          if (bad_component(s))
            return false;
          if (want_split)
            sp.push_back(s);
          break;
        }
      string const & s(path.substr(start, stop - start));
      if (bad_component(s))
        return false;
      if (want_split)
        sp.push_back(s);
      start = stop + 1;
    }
  return true;
}

static inline bool
fully_normalized_path(string const & path)
{
  split_path sp;
  return fully_normalized_path_split(path, false, sp);
}

// This function considers _MTN, _MTn, _MtN, _mtn etc. to all be bookkeeping
// paths, because on case insensitive filesystems, files put in any of them
// may end up in _MTN instead.  This allows arbitrary code execution.  A
// better solution would be to fix this in the working directory writing code
// -- this prevents all-unix projects from naming things "mt", which is a bit
// rude -- but as a temporary security kluge it works.
static inline bool
in_bookkeeping_dir(string const & path)
{
  if (path.size() == 0 || (path[0] != '_'))
    return false;
  if (path.size() == 1 || (path[1] != 'M' && path[1] != 'm'))
    return false;
  if (path.size() == 2 || (path[2] != 'T' && path[2] != 't'))
    return false;
  if (path.size() == 3 || (path[3] != 'N' && path[3] != 'n'))
    return false;
  // if we've gotten here, the first three letters are _, M, T, and N, in
  // either upper or lower case.  So if that is the whole path, or else if it
  // continues but the next character is /, then this is a bookkeeping path.
  if (path.size() == 4 || (path[4] == '/'))
    return true;
  return false;
}

static inline bool
is_valid_internal(string const & path)
{
  return (fully_normalized_path(path)
          && !in_bookkeeping_dir(path));
}

// equivalent to file_path_internal(path).split(sp), but
// avoids splitting the string twice
void
internal_string_to_split_path(string const & path, split_path & sp)
{
  I(utf8_validate(path));
  I(!in_bookkeeping_dir(path));
  sp.clear();
  sp.reserve(8);
  sp.push_back(the_null_component);
  I(fully_normalized_path_split(path, true, sp));
}

file_path::file_path(file_path::source_type type, string const & path)
{
  MM(path);
  I(utf8_validate(path));
  switch (type)
    {
    case internal:
      data = path;
      break;
    case external:
      if (!initial_rel_path.initialized)
        {
          // we are not in a workspace; treat this as an internal
          // path, and set the access_tracker() into a very uninitialised
          // state so that we will hit an exception if we do eventually
          // enter a workspace
          initial_rel_path.may_not_initialize();
          data = path;
          N(is_valid_internal(path) && !in_bookkeeping_dir(path),
            F("path '%s' is invalid") % path);
          break;
        }
      N(!path.empty(), F("empty path '%s' is invalid") % path);
      fs::path out, base, relative;
      try
        {
          base = initial_rel_path.get();
          // the fs::native is needed to get it to accept paths like ".foo".
          relative = fs::path(path, fs::native);
          out = (base / relative).normalize();
        }
      catch (exception &)
        {
          N(false, F("path '%s' is invalid") % path);
        }
      data = utf8(out.string());
      if (data() == ".")
        data = string("");
      N(!relative.has_root_path(),
        F("absolute path '%s' is invalid") % relative.string());
      N(fully_normalized_path(data()), F("path '%s' is invalid") % data);
      N(!in_bookkeeping_dir(data()), F("path '%s' is in bookkeeping dir") % data);
      break;
    }
  MM(data);
  I(is_valid_internal(data()));
}

bookkeeping_path::bookkeeping_path(string const & path)
{
  I(fully_normalized_path(path));
  I(in_bookkeeping_dir(path));
  data = path;
}

bool
bookkeeping_path::is_bookkeeping_path(string const & path)
{
  return in_bookkeeping_dir(path);
}

///////////////////////////////////////////////////////////////////////////
// splitting/joining
// this code must be superfast
// it depends very much on knowing that it can only be applied to fully
// normalized, relative, paths.
///////////////////////////////////////////////////////////////////////////

// This function takes a vector of path components and joins them into a
// single file_path.  This is the inverse to file_path::split.  It takes a
// vector of the form:
//
//   ["", p[0], p[1], ..., p[n]]
//
// and constructs the path:
//
//   p[0]/p[1]/.../p[n]
//
file_path::file_path(split_path const & sp)
{
  split_path::const_iterator i = sp.begin();
  I(i != sp.end());
  I(null_name(*i));
  string tmp;
  bool start = true;
  for (++i; i != sp.end(); ++i)
    {
      I(!null_name(*i));
      if (!start)
        tmp += "/";
      tmp += (*i)();
      if (start)
        start = false;
    }
  I(!in_bookkeeping_dir(tmp));
  data = tmp;
}

//
// this takes a path of the form
//
//  "p[0]/p[1]/.../p[n-1]/p[n]"
//
// and fills in a vector of paths corresponding to p[0] ... p[n].  This is the
// inverse to the file_path::file_path(split_path) constructor.
//
// The first entry in this vector is always the null component, "".  This path
// is the root of the tree.  So we actually output a vector like:
//   ["", p[0], p[1], ..., p[n]]
// with n+1 members.
void
file_path::split(split_path & sp) const
{
  sp.clear();
  sp.push_back(the_null_component);
  if (empty())
    return;
  string::size_type start, stop;
  start = 0;
  string const & s = data();
  while (1)
    {
      stop = s.find('/', start);
      if (stop == string::npos)
        {
          sp.push_back(s.substr(start));
          break;
        }
      sp.push_back(s.substr(start, stop - start));
      start = stop + 1;
    }
}

template <>
void dump(split_path const & sp, string & out)
{
  ostringstream oss;

  for (split_path::const_iterator i = sp.begin(); i != sp.end(); ++i)
    {
      if (null_name(*i))
        oss << ".";
      else
        oss << "/" << *i;
    }

  oss << "\n";

  out = oss.str();
}


///////////////////////////////////////////////////////////////////////////
// localizing file names (externalizing them)
// this code must be superfast when there is no conversion needed
///////////////////////////////////////////////////////////////////////////

string
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

ostream &
operator <<(ostream & o, any_path const & a)
{
  o << a.as_internal();
  return o;
}

ostream &
operator <<(ostream & o, split_path const & sp)
{
  file_path tmp(sp);
  return o << tmp;
}

///////////////////////////////////////////////////////////////////////////
// path manipulation
// this code's speed does not matter much
///////////////////////////////////////////////////////////////////////////

static bool
is_absolute_here(string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
#ifdef WIN32
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
#endif
  return false;
}

static inline bool
is_absolute_somewhere(string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
  return false;
}

file_path
file_path::operator /(string const & to_append) const
{
  I(!is_absolute_somewhere(to_append));
  if (empty())
    return file_path_internal(to_append);
  else
    return file_path_internal(data() + "/" + to_append);
}

bookkeeping_path
bookkeeping_path::operator /(string const & to_append) const
{
  I(!is_absolute_somewhere(to_append));
  I(!empty());
  return bookkeeping_path(data() + "/" + to_append);
}

system_path
system_path::operator /(string const & to_append) const
{
  I(!empty());
  I(!is_absolute_here(to_append));
  return system_path(data() + "/" + to_append);
}

///////////////////////////////////////////////////////////////////////////
// system_path
///////////////////////////////////////////////////////////////////////////

static string
normalize_out_dots(string const & path)
{
#ifdef WIN32
  return fs::path(path, fs::native).normalize().string();
#else
  return fs::path(path, fs::native).normalize().native_file_string();
#endif
}

system_path::system_path(any_path const & other, bool in_true_workspace)
{
  if (is_absolute_here(other.as_internal()))
    // another system_path.  the normalizing isn't really necessary, but it
    // makes me feel warm and fuzzy.
    data = normalize_out_dots(other.as_internal());
  else
    {
      system_path wr;
      if (in_true_workspace)
        wr = working_root.get();
      else
        wr = working_root.get_but_unused();
      data = normalize_out_dots((wr / other.as_internal()).as_internal());
    }
}

static inline string const_system_path(utf8 const & path)
{
  N(!path().empty(), F("invalid path ''"));
  string expanded = tilde_expand(path)();
  if (is_absolute_here(expanded))
    return normalize_out_dots(expanded);
  else
    return normalize_out_dots((initial_abs_path.get() / expanded).as_internal());
}

system_path::system_path(string const & path)
{
  data = const_system_path(path);
}

system_path::system_path(utf8 const & path)
{
  data = const_system_path(path);
}

///////////////////////////////////////////////////////////////////////////
// utility
///////////////////////////////////////////////////////////////////////////

bool
workspace_root(split_path const & sp)
{
  I(null_name(idx(sp,0)));
  return sp.size() == 1;
}

void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename)
{
  I(!sp.empty());
  // L(FL("dirname_basename('%s' [%d components],...)") % file_path(sp) % sp.size());
  dirname = sp;
  dirname.pop_back();
  basename = sp.back();
  if (dirname.empty())
    {
      // L(FL("basename %d vs. null component %d") % basename % the_null_component);
      I(null_name(basename));
    }
}

///////////////////////////////////////////////////////////////////////////
// workspace (and path root) handling
///////////////////////////////////////////////////////////////////////////

system_path
current_root_path()
{
  return system_path(fs::initial_path().root_path().string());
}

static bool
find_bookdir(fs::path const & root, fs::path const & bookdir, 
             fs::path & current, fs::path & removed)
{
  current = fs::initial_path();
  fs::path check = current / bookdir;

  // check that the current directory is below the specified search root

  fs::path::iterator ri = root.begin();
  fs::path::iterator ci = current.begin();

  while (ri != root.end() && ci != current.end() && *ri == *ci)
    {
      ++ri;
      ++ci;
    }

  // if it's not then issue a warning and abort the search

  if (ri != root.end())
    {
      W(F("current directory '%s' is not below root '%s'")
        % current.string()
        % root.string());
      return false;
    }

  L(FL("searching for '%s' directory with root '%s'")
    % bookdir.string()
    % root.string());

  while (current != root
         && current.has_branch_path()
         && current.has_leaf()
         && !fs::exists(check))
    {
      L(FL("'%s' not found in '%s' with '%s' removed")
        % bookdir.string() % current.string() % removed.string());
      removed = fs::path(current.leaf(), fs::native) / removed;
      current = current.branch_path();
      check = current / bookdir;
    }

  L(FL("search for '%s' ended at '%s' with '%s' removed")
    % bookdir.string() % current.string() % removed.string());

  if (!fs::exists(check))
    {
      L(FL("'%s' does not exist") % check.string());
      return false;
    }

  if (!fs::is_directory(check))
    {
      L(FL("'%s' is not a directory") % check.string());
      return false;
    }

  // check for _MTN/. and _MTN/.. to see if mt dir is readable
  if (!fs::exists(check / ".") || !fs::exists(check / ".."))
    {
      L(FL("problems with '%s' (missing '.' or '..')") % check.string());
      return false;
    }
  return true;
}


bool
find_and_go_to_workspace(system_path const & search_root)
{
  fs::path root(search_root.as_external(), fs::native);
  fs::path bookdir(bookkeeping_root.as_external(), fs::native);
  fs::path oldbookdir(old_bookkeeping_root.as_external(), fs::native);
  fs::path current, removed;

  // first look for the current name of the bookkeeping directory.
  // if we don't find it, look for it under the old name, so that
  // migration has a chance to work.
  if (!find_bookdir(root, bookdir, current, removed))
    if (!find_bookdir(root, oldbookdir, current, removed))
      return false;

  working_root.set(current.native_file_string(), true);
  initial_rel_path.set(removed, true);

  L(FL("working root is '%s'") % working_root.get_but_unused());
  L(FL("initial relative path is '%s'") % initial_rel_path.get_but_unused().string());

  change_current_working_dir(working_root.get_but_unused());

  return true;
}

void
go_to_workspace(system_path const & new_workspace)
{
  working_root.set(new_workspace, true);
  initial_rel_path.set(fs::path(), true);
  change_current_working_dir(new_workspace);
}

///////////////////////////////////////////////////////////////////////////
// tests
///////////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

using std::logic_error;

UNIT_TEST(paths, null_name)
{
  BOOST_CHECK(null_name(the_null_component));
}

UNIT_TEST(paths, file_path_internal)
{
  char const * baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "_MTN",
                            "_MTN/blah",
                            "foo/bar/",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_mTN",
                            "_mtn/foo",
                            "_mtN/foo",
                            "_mTn/foo",
                            "_Mtn/foo",
                            "_MTn/foo",
                            "_MtN/foo",
                            "_mTN/foo",
                            0 };
  initial_rel_path.unset();
  initial_rel_path.set(fs::path(), true);
  for (char const ** c = baddies; *c; ++c)
    {
      BOOST_CHECK_THROW(file_path_internal(*c), logic_error);
    }
  initial_rel_path.unset();
  initial_rel_path.set(fs::path("blah/blah/blah", fs::native), true);
  for (char const ** c = baddies; *c; ++c)
    {
      BOOST_CHECK_THROW(file_path_internal(*c), logic_error);
    }

  BOOST_CHECK(file_path().empty());
  BOOST_CHECK(file_path_internal("").empty());

  char const * goodies[] = {"",
                            "a",
                            "foo",
                            "foo/bar/baz",
                            "foo/bar.baz",
                            "foo/with-hyphen/bar",
                            "foo/with_underscore/bar",
                            "foo/with,other+@weird*%#$=stuff/bar",
                            ".foo/bar",
                            "..foo/bar",
                            "_MTNfoo/bar",
                            "foo:bar",
                            0 };

  for (int i = 0; i < 2; ++i)
    {
      initial_rel_path.unset();
      initial_rel_path.set(i ? fs::path()
                             : fs::path("blah/blah/blah", fs::native),
                           true);
      for (char const ** c = goodies; *c; ++c)
        {
          file_path fp = file_path_internal(*c);
          BOOST_CHECK(fp.as_internal() == *c);
          BOOST_CHECK(file_path_internal(fp.as_internal()) == fp);
          split_path split_test;
          fp.split(split_test);
          BOOST_CHECK(!split_test.empty());
          file_path fp2(split_test);
          BOOST_CHECK(fp == fp2);
          BOOST_CHECK(null_name(split_test[0]));
          for (split_path::const_iterator
                 i = split_test.begin() + 1; i != split_test.end(); ++i)
            BOOST_CHECK(!null_name(*i));
        }
    }

  initial_rel_path.unset();
}

static void check_fp_normalizes_to(char * before, char * after)
{
  L(FL("check_fp_normalizes_to: '%s' -> '%s'") % before % after);
  file_path fp = file_path_external(string(before));
  L(FL("  (got: %s)") % fp);
  BOOST_CHECK(fp.as_internal() == after);
  BOOST_CHECK(file_path_internal(fp.as_internal()) == fp);
  // we compare after to the external form too, since as far as we know
  // relative normalized posix paths are always good win32 paths too
  BOOST_CHECK(fp.as_external() == after);
  split_path split_test;
  fp.split(split_test);
  BOOST_CHECK(!split_test.empty());
  file_path fp2(split_test);
  BOOST_CHECK(fp == fp2);
  BOOST_CHECK(null_name(split_test[0]));
  for (split_path::const_iterator
         i = split_test.begin() + 1; i != split_test.end(); ++i)
    BOOST_CHECK(!null_name(*i));
}

UNIT_TEST(paths, file_path_external_null_prefix)
{
  initial_rel_path.unset();
  initial_rel_path.set(fs::path(), true);

  char const * baddies[] = {"/foo",
                            "../bar",
                            "_MTN/blah",
                            "_MTN",
                            "//blah",
                            "\\foo",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            "",
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_mTN",
                            "_mtn/foo",
                            "_mtN/foo",
                            "_mTn/foo",
                            "_Mtn/foo",
                            "_MTn/foo",
                            "_MtN/foo",
                            "_mTN/foo",
                            0 };
  for (char const ** c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_null_prefix: trying baddie: %s") % *c);
      BOOST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
    }

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
#ifndef WIN32
  check_fp_normalizes_to("foo:bar", "foo:bar");
#endif
  check_fp_normalizes_to("foo/with,other+@weird*%#$=stuff/bar",
                         "foo/with,other+@weird*%#$=stuff/bar");

  // Why are these tests with // in them commented out?  because boost::fs
  // sucks and can't normalize them.  FIXME.
  //check_fp_normalizes_to("foo//bar", "foo/bar");
  check_fp_normalizes_to("foo/../bar", "bar");
  check_fp_normalizes_to("foo/bar/", "foo/bar");
  check_fp_normalizes_to("foo/./bar/", "foo/bar");
  check_fp_normalizes_to("./foo", "foo");
  //check_fp_normalizes_to("foo///.//", "foo");

  initial_rel_path.unset();
}

UNIT_TEST(paths, file_path_external_prefix__MTN)
{
  initial_rel_path.unset();
  initial_rel_path.set(fs::path("_MTN"), true);

  BOOST_CHECK_THROW(file_path_external(utf8("foo")), informative_failure);
  BOOST_CHECK_THROW(file_path_external(utf8(".")), informative_failure);
  BOOST_CHECK_THROW(file_path_external(utf8("./blah")), informative_failure);
  check_fp_normalizes_to("..", "");
  check_fp_normalizes_to("../foo", "foo");
}

UNIT_TEST(paths, file_path_external_prefix_a_b)
{
  initial_rel_path.unset();
  initial_rel_path.set(fs::path("a/b"), true);

  char const * baddies[] = {"/foo",
                            "../../../bar",
                            "../../..",
                            "../../_MTN",
                            "../../_MTN/foo",
                            "//blah",
                            "\\foo",
                            "c:\\foo",
#ifdef WIN32
                            "c:foo",
                            "c:/foo",
#endif
                            "",
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "../../_mtn",
                            "../../_mtN",
                            "../../_mTn",
                            "../../_Mtn",
                            "../../_MTn",
                            "../../_MtN",
                            "../../_mTN",
                            "../../_mtn/foo",
                            "../../_mtN/foo",
                            "../../_mTn/foo",
                            "../../_Mtn/foo",
                            "../../_MTn/foo",
                            "../../_MtN/foo",
                            "../../_mTN/foo",
                            0 };
  for (char const ** c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_prefix_a_b: trying baddie: %s") % *c);
      BOOST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
    }

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
#ifndef WIN32
  check_fp_normalizes_to("foo:bar", "a/b/foo:bar");
#endif
  check_fp_normalizes_to("foo/with,other+@weird*%#$=stuff/bar",
                         "a/b/foo/with,other+@weird*%#$=stuff/bar");
  // why are the tests with // in them commented out?  because boost::fs sucks
  // and can't normalize them.  FIXME.
  //check_fp_normalizes_to("foo//bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/../bar", "a/b/bar");
  check_fp_normalizes_to("foo/bar/", "a/b/foo/bar");
  check_fp_normalizes_to("foo/./bar/", "a/b/foo/bar");
  check_fp_normalizes_to("./foo", "a/b/foo");
  //check_fp_normalizes_to("foo///.//", "a/b/foo");
  // things that would have been bad without the initial_rel_path:
  check_fp_normalizes_to("../foo", "a/foo");
  check_fp_normalizes_to("..", "a");
  check_fp_normalizes_to("../..", "");
  check_fp_normalizes_to("_MTN/foo", "a/b/_MTN/foo");
  check_fp_normalizes_to("_MTN", "a/b/_MTN");
#ifndef WIN32
  check_fp_normalizes_to("c:foo", "a/b/c:foo");
  check_fp_normalizes_to("c:/foo", "a/b/c:/foo");
#endif

  initial_rel_path.unset();
}

UNIT_TEST(paths, split_join)
{
  file_path fp1 = file_path_internal("foo/bar/baz");
  file_path fp2 = file_path_internal("bar/baz/foo");
  split_path split1, split2;
  fp1.split(split1);
  fp2.split(split2);
  BOOST_CHECK(fp1 == file_path(split1));
  BOOST_CHECK(fp2 == file_path(split2));
  BOOST_CHECK(!(fp1 == file_path(split2)));
  BOOST_CHECK(!(fp2 == file_path(split1)));
  BOOST_CHECK(split1.size() == 4);
  BOOST_CHECK(split2.size() == 4);
  BOOST_CHECK(split1[1] != split1[2]);
  BOOST_CHECK(split1[1] != split1[3]);
  BOOST_CHECK(split1[2] != split1[3]);
  BOOST_CHECK(null_name(split1[0])
              && !null_name(split1[1])
              && !null_name(split1[2])
              && !null_name(split1[3]));
  BOOST_CHECK(split1[1] == split2[3]);
  BOOST_CHECK(split1[2] == split2[1]);
  BOOST_CHECK(split1[3] == split2[2]);

  file_path fp3 = file_path_internal("");
  split_path split3;
  fp3.split(split3);
  BOOST_CHECK(split3.size() == 1 && null_name(split3[0]));

  // empty split_path is invalid
  split_path split4;
  // this comparison tricks the compiler into not completely eliminating this
  // code as dead...
  BOOST_CHECK_THROW(file_path(split4) == file_path(), logic_error);
  split4.push_back(the_null_component);
  BOOST_CHECK(file_path(split4) == file_path());

  // split_path without null first item is invalid
  split4.clear();
  split4.push_back(split1[1]);
  // this comparison tricks the compiler into not completely eliminating this
  // code as dead...
  BOOST_CHECK_THROW(file_path(split4) == file_path(), logic_error);

  // split_path with non-first item item null is invalid
  split4.clear();
  split4.push_back(the_null_component);
  split4.push_back(split1[0]);
  split4.push_back(the_null_component);
  // this comparison tricks the compiler into not completely eliminating this
  // code as dead...
  BOOST_CHECK_THROW(file_path(split4) == file_path(), logic_error);

  // Make sure that we can't use joining to create a path into the bookkeeping
  // dir
  {
    split_path split_mt1, split_mt2;
    file_path_internal("foo/_MTN").split(split_mt1);
    BOOST_CHECK(split_mt1.size() == 3);
    I(split_mt1[2] == bookkeeping_root_component);
    split_mt2.push_back(the_null_component);
    split_mt2.push_back(split_mt1[2]);
    // split_mt2 now contains the component "_MTN"
    BOOST_CHECK_THROW(file_path(split_mt2) == file_path(), logic_error);
    split_mt2.push_back(split_mt1[1]);
    // split_mt2 now contains the components "_MTN", "foo" in that order
    // this comparison tricks the compiler into not completely eliminating this
    // code as dead...
    BOOST_CHECK_THROW(file_path(split_mt2) == file_path(), logic_error);
  }
  // and make sure it fails for the klugy security cases -- see comments on
  // in_bookkeeping_dir
  {
    split_path split_mt1, split_mt2;
    file_path_internal("foo/_mTn").split(split_mt1);
    BOOST_CHECK(split_mt1.size() == 3);
    split_mt2.push_back(the_null_component);
    split_mt2.push_back(split_mt1[2]);
    // split_mt2 now contains the component "_mTn"
    BOOST_CHECK_THROW(file_path(split_mt2) == file_path(), logic_error);
    split_mt2.push_back(split_mt1[1]);
    // split_mt2 now contains the components "_mTn", "foo" in that order
    // this comparison tricks the compiler into not completely eliminating this
    // code as dead...
    BOOST_CHECK_THROW(file_path(split_mt2) == file_path(), logic_error);
  }
}

static void check_bk_normalizes_to(char * before, char * after)
{
  bookkeeping_path bp(bookkeeping_root / before);
  L(FL("normalizing %s to %s (got %s)") % before % after % bp);
  BOOST_CHECK(bp.as_external() == after);
  BOOST_CHECK(bookkeeping_path(bp.as_internal()).as_internal() == bp.as_internal());
}

UNIT_TEST(paths, bookkeeping)
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
                            "a:b",
                            0 };
  string tmp_path_string;

  for (char const ** c = baddies; *c; ++c)
    {
      L(FL("test_bookkeeping_path baddie: trying '%s'") % *c);
            BOOST_CHECK_THROW(bookkeeping_path(tmp_path_string.assign(*c)), logic_error);
            BOOST_CHECK_THROW(bookkeeping_root / tmp_path_string.assign(*c), logic_error);
    }
  BOOST_CHECK_THROW(bookkeeping_path(tmp_path_string.assign("foo/bar")), logic_error);
  BOOST_CHECK_THROW(bookkeeping_path(tmp_path_string.assign("a")), logic_error);

  check_bk_normalizes_to("a", "_MTN/a");
  check_bk_normalizes_to("foo", "_MTN/foo");
  check_bk_normalizes_to("foo/bar", "_MTN/foo/bar");
  check_bk_normalizes_to("foo/bar/baz", "_MTN/foo/bar/baz");
}

static void check_system_normalizes_to(char * before, char * after)
{
  system_path sp(before);
  L(FL("normalizing '%s' to '%s' (got '%s')") % before % after % sp);
  BOOST_CHECK(sp.as_external() == after);
  BOOST_CHECK(system_path(sp.as_internal()).as_internal() == sp.as_internal());
}

UNIT_TEST(paths, system)
{
  initial_abs_path.unset();
  initial_abs_path.set(system_path("/a/b"), true);

  BOOST_CHECK_THROW(system_path(""), informative_failure);

  check_system_normalizes_to("foo", "/a/b/foo");
  check_system_normalizes_to("foo/bar", "/a/b/foo/bar");
  check_system_normalizes_to("/foo/bar", "/foo/bar");
  check_system_normalizes_to("//foo/bar", "//foo/bar");
#ifdef WIN32
  check_system_normalizes_to("c:foo", "c:foo");
  check_system_normalizes_to("c:/foo", "c:/foo");
  check_system_normalizes_to("c:\\foo", "c:/foo");
#else
  check_system_normalizes_to("c:foo", "/a/b/c:foo");
  check_system_normalizes_to("c:/foo", "/a/b/c:/foo");
  check_system_normalizes_to("c:\\foo", "/a/b/c:\\foo");
  check_system_normalizes_to("foo:bar", "/a/b/foo:bar");
#endif
  // we require that system_path normalize out ..'s, because of the following
  // case:
  //   /work mkdir newdir
  //   /work$ cd newdir
  //   /work/newdir$ monotone setup --db=../foo.db
  // Now they have either "/work/foo.db" or "/work/newdir/../foo.db" in
  // _MTN/options
  //   /work/newdir$ cd ..
  //   /work$ mv newdir newerdir  # better name
  // Oops, now, if we stored the version with ..'s in, this workspace
  // is broken.
  check_system_normalizes_to("../foo", "/a/foo");
  check_system_normalizes_to("foo/..", "/a/b");
  check_system_normalizes_to("/foo/bar/..", "/foo");
  check_system_normalizes_to("/foo/..", "/");
  // can't do particularly interesting checking of tilde expansion, but at
  // least we can check that it's doing _something_...
  string tilde_expanded = system_path("~/foo").as_external();
#ifdef WIN32
  BOOST_CHECK(tilde_expanded[1] == ':');
#else
  BOOST_CHECK(tilde_expanded[0] == '/');
#endif
  BOOST_CHECK(tilde_expanded.find('~') == string::npos);
  // and check for the weird WIN32 version
#ifdef WIN32
  string tilde_expanded2 = system_path("~this_user_does_not_exist_anywhere").as_external();
  BOOST_CHECK(tilde_expanded2[1] == ':');
  BOOST_CHECK(tilde_expanded2.find('~') == string::npos);
#else
  BOOST_CHECK_THROW(system_path("~this_user_does_not_exist_anywhere"), informative_failure);
#endif

  // finally, make sure that the copy-from-any_path constructor works right
  // in particular, it should interpret the paths it gets as being relative to
  // the project root, not the initial path
  working_root.unset();
  working_root.set(system_path("/working/root"), true);
  initial_rel_path.unset();
  initial_rel_path.set(fs::path("rel/initial"), true);

  BOOST_CHECK(system_path(system_path("foo/bar")).as_internal() == "/a/b/foo/bar");
  BOOST_CHECK(!working_root.used);
  BOOST_CHECK(system_path(system_path("/foo/bar")).as_internal() == "/foo/bar");
  BOOST_CHECK(!working_root.used);
  BOOST_CHECK(system_path(file_path_internal("foo/bar"), false).as_internal()
              == "/working/root/foo/bar");
  BOOST_CHECK(!working_root.used);
  BOOST_CHECK(system_path(file_path_internal("foo/bar")).as_internal()
              == "/working/root/foo/bar");
  BOOST_CHECK(working_root.used);
  BOOST_CHECK(system_path(file_path_external(string("foo/bar"))).as_external()
              == "/working/root/rel/initial/foo/bar");
  file_path a_file_path;
  BOOST_CHECK(system_path(a_file_path).as_external()
              == "/working/root");
  BOOST_CHECK(system_path(bookkeeping_path("_MTN/foo/bar")).as_internal()
              == "/working/root/_MTN/foo/bar");
  BOOST_CHECK(system_path(bookkeeping_root).as_internal()
              == "/working/root/_MTN");
  initial_abs_path.unset();
  working_root.unset();
  initial_rel_path.unset();
}

UNIT_TEST(paths, access_tracker)
{
  access_tracker<int> a;
  BOOST_CHECK_THROW(a.get(), logic_error);
  a.set(1, false);
  BOOST_CHECK_THROW(a.set(2, false), logic_error);
  a.set(2, true);
  BOOST_CHECK_THROW(a.set(3, false), logic_error);
  BOOST_CHECK(a.get() == 2);
  BOOST_CHECK_THROW(a.set(3, true), logic_error);
  a.unset();
  a.may_not_initialize();
  BOOST_CHECK_THROW(a.set(1, false), logic_error);
  BOOST_CHECK_THROW(a.set(2, true), logic_error);
  a.unset();
  a.set(1, false);
  BOOST_CHECK_THROW(a.may_not_initialize(), logic_error);
}

static void test_a_path_ordering(string const & left, string const & right)
{
  MM(left);
  MM(right);
  split_path left_sp, right_sp;
  file_path_internal(left).split(left_sp);
  file_path_internal(right).split(right_sp);
  I(left_sp < right_sp);
}

UNIT_TEST(paths, ordering)
{
  // this ordering is very important:
  //   -- it is used to determine the textual form of csets and manifests
  //      (in particular, it cannot be changed)
  //   -- it is used to determine in what order cset operations can be applied
  //      (in particular, foo must sort before foo/bar, so that we can use it
  //      to do top-down and bottom-up traversals of a set of paths).
  test_a_path_ordering("a", "b");
  test_a_path_ordering("a", "c");
  test_a_path_ordering("ab", "ac");
  test_a_path_ordering("a", "ab");
  test_a_path_ordering("", "a");
  test_a_path_ordering("", ".foo");
  test_a_path_ordering("foo", "foo/bar");
  // . is before / asciibetically, so sorting by strings will give the wrong
  // answer on this:
  test_a_path_ordering("foo/bar", "foo.bar");

  // path_components used to be interned strings, and we used the default sort
  // order, which meant that in practice path components would sort in the
  // _order they were first used in the program_.  So let's put in a test that
  // would catch this sort of brokenness.
  test_a_path_ordering("fallanopic_not_otherwise_mentioned", "xyzzy");
  test_a_path_ordering("fallanoooo_not_otherwise_mentioned_and_smaller", "fallanopic_not_otherwise_mentioned");
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
