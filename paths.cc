// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>

#include "paths.hh"
#include "file_io.hh"
#include "charset.hh"
#include "lua.hh"

using std::exception;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

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
// we used to make it a file_path, but then you can't run monotone from
// inside the _MTN/ dir (even when referring to files outside the _MTN/
// dir).  use of a bare string requires some caution but does work.
static access_tracker<string> initial_rel_path;
// working_root is for converting file_path's and bookkeeping_path's to
// system_path's.
static access_tracker<system_path> working_root;

void
save_initial_path()
{
  // FIXME: BUG: this only works if the current working dir is in utf8
  initial_abs_path.set(system_path(get_current_working_dir()), false);
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
  if (component.empty())
    return true;
  if (component == ".")
    return true;
  if (component == "..")
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

// as above, but disallows / as well.
static inline bool
has_bad_component_chars(string const & pc)
{
  for (string::const_iterator c = pc.begin(); LIKELY(c != pc.end()); c++)
    {
      // char is often a signed type; convert to unsigned to ensure that
      // bytes 0x80-0xff are considered > 0x1f.
      u8 x = (u8)*c;
      // 0x2f is '/' and 0x5c is '\\'; we use hex constants to make the
      // dependency on ASCII encoding explicit.
      if (UNLIKELY(x <= 0x1f || x == 0x2f || x == 0x5c || x == 0x7f))
        return true;
    }
  return false;

}

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

// fully_normalized_path verifies a complete pathname for validity and
// having been properly normalized (as if by normalize_path, below).
static inline bool
fully_normalized_path(string const & path)
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
  string::size_type start = 0, stop;
  while (1)
    {
      stop = path.find('/', start);
      if (stop == string::npos)
        break;
      string const & s(path.substr(start, stop - start));
      if (bad_component(s))
        return false;
      start = stop + 1;
    }

  string const & s(path.substr(start));
  return !bad_component(s);
}

// This function considers _MTN, _MTn, _MtN, _mtn etc. to all be bookkeeping
// paths, because on case insensitive filesystems, files put in any of them
// may end up in _MTN instead.  This allows arbitrary code execution.  A
// better solution would be to fix this in the working directory writing
// code -- this prevents all-unix projects from naming things "_mtn", which
// is less rude than when the bookkeeping root was "MT", but still rude --
// but as a temporary security kluge it works.
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

static string
normalize_path(string const & in)
{
  string inT = in;
  string leader;
  MM(inT);

#ifdef WIN32
  // the first thing we do is kill all the backslashes
  for (string::iterator i = inT.begin(); i != inT.end(); i++)
    if (*i == '\\')
      *i = '/';
#endif

  if (is_absolute_here (inT))
    {
      if (inT[0] == '/')
        {
          leader = "/";
          inT = inT.substr(1);

          if (inT.size() > 0 && inT[0] == '/')
            {
              // if there are exactly two slashes at the beginning they
              // are both preserved.  three or more are the same as one.
              string::size_type f = inT.find_first_not_of("/");
              if (f == string::npos)
                f = inT.size();
              if (f == 1)
                leader = "//";
              inT = inT.substr(f);
            }
        }
#ifdef WIN32
      else
        {
          I(inT[1] == ':');
          if (inT.size() > 2 && inT[2] == '/')
            {
              leader = inT.substr(0, 3);
              inT = inT.substr(3);
            }
          else
            {
              leader = inT.substr(0, 2);
              inT = inT.substr(2);
            }
        }
#endif

      I(!is_absolute_here(inT));
      if (inT.size() == 0)
        return leader;
    }

  vector<string> stack;
  string::const_iterator head, tail;
  string::size_type size_estimate = leader.size();
  for (head = inT.begin(); head != inT.end(); head = tail)
    {
      tail = head;
      while (tail != inT.end() && *tail != '/')
        tail++;

      string elt(head, tail);
      while (tail != inT.end() && *tail == '/')
        tail++;

      if (elt == ".")
        continue;
      // remove foo/.. element pairs; leave leading .. components alone
      if (elt == ".." && !stack.empty() && stack.back() != "..")
        {
          stack.pop_back();
          continue;
        }

      size_estimate += elt.size() + 1;
      stack.push_back(elt);
    }

  leader.reserve(size_estimate);
  for (vector<string>::const_iterator i = stack.begin(); i != stack.end(); i++)
    {
      if (i != stack.begin())
        leader += "/";
      leader += *i;
    }
  return leader;
}

LUAEXT(normalize_path, )
{
  const char *pathstr = luaL_checkstring(L, -1);
  N(pathstr, F("%s called with an invalid parameter") % "normalize_path");

  lua_pushstring(L, normalize_path(string(pathstr)).c_str());
  return 1;
}

static void
normalize_external_path(string const & path, string & normalized)
{
  if (!initial_rel_path.initialized)
    {
      // we are not in a workspace; treat this as an internal
      // path, and set the access_tracker() into a very uninitialised
      // state so that we will hit an exception if we do eventually
      // enter a workspace
      initial_rel_path.may_not_initialize();
      normalized = path;
      N(is_valid_internal(path),
        F("path '%s' is invalid") % path);
    }
  else
    {
      N(!is_absolute_here(path), F("absolute path '%s' is invalid") % path);
      string base;
      try
        {
          base = initial_rel_path.get();
          if (base == "")
            normalized = normalize_path(path);
          else
            normalized = normalize_path(base + "/" + path);
        }
      catch (exception &)
        {
          N(false, F("path '%s' is invalid") % path);
        }
      if (normalized == ".")
        normalized = string("");
      N(fully_normalized_path(normalized),
        F("path '%s' is invalid") % normalized);
    }
}

///////////////////////////////////////////////////////////////////////////
// single path component handling.
///////////////////////////////////////////////////////////////////////////

// these constructors confirm that what they are passed is a legitimate
// component.  note that the empty string is a legitimate component,
// but is not acceptable to bad_component (above) and therefore we have
// to open-code most of those checks.
path_component::path_component(utf8 const & d)
  : data(d())
{
  MM(data);
  I(!has_bad_component_chars(data) && data != "." && data != "..");
}

path_component::path_component(string const & d)
  : data(d)
{
  MM(data);
  I(utf8_validate(utf8(data))
    && !has_bad_component_chars(data)
    && data != "." && data != "..");
}

path_component::path_component(char const * d)
  : data(d)
{
  MM(data);
  I(utf8_validate(utf8(data))
    && !has_bad_component_chars(data)
    && data != "." && data != "..");
}

std::ostream & operator<<(std::ostream & s, path_component const & pc)
{
  return s << pc();
}

template <> void dump(path_component const & pc, std::string & to)
{
  to = pc();
}

///////////////////////////////////////////////////////////////////////////
// complete paths to files within a working directory
///////////////////////////////////////////////////////////////////////////

file_path::file_path(file_path::source_type type, string const & path)
{
  MM(path);
  I(utf8_validate(utf8(path)));
  if (type == external)
    {
      string normalized;
      normalize_external_path(path, normalized);
      N(!in_bookkeeping_dir(normalized),
        F("path '%s' is in bookkeeping dir") % normalized);
      data = normalized;
    }
  else
    data = path;
  MM(data);
  I(is_valid_internal(data));
}

file_path::file_path(file_path::source_type type, utf8 const & path)
{
  MM(path);
  I(utf8_validate(path));
  if (type == external)
    {
      string normalized;
      normalize_external_path(path(), normalized);
      N(!in_bookkeeping_dir(normalized),
        F("path '%s' is in bookkeeping dir") % normalized);
      data = normalized;
    }
  else
    data = path();
  MM(data);
  I(is_valid_internal(data));
}

bookkeeping_path::bookkeeping_path(string const & path)
{
  I(fully_normalized_path(path));
  I(in_bookkeeping_dir(path));
  data = path;
}

bool
bookkeeping_path::external_string_is_bookkeeping_path(utf8 const & path)
{
  // FIXME: this charset casting everywhere is ridiculous
  string normalized;
  try
    {
      normalize_external_path(path(), normalized);
    }
  catch (informative_failure &)
    {
      return false;
    }
  return internal_string_is_bookkeeping_path(utf8(normalized));
}
bool bookkeeping_path::internal_string_is_bookkeeping_path(utf8 const & path)
{
  return in_bookkeeping_dir(path());
}

///////////////////////////////////////////////////////////////////////////
// splitting/joining
// this code must be superfast
// it depends very much on knowing that it can only be applied to fully
// normalized, relative, paths.
///////////////////////////////////////////////////////////////////////////

// this peels off the last component of any path and returns it.
// the last component of a path with no slashes in it is the complete path.
// the last component of a path referring to the root directory is an
// empty string.
path_component
any_path::basename() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  if (sep == string::npos)
    return path_component(s, 0);  // force use of short circuit
  if (sep == s.size())
    return path_component();
  return path_component(s, sep + 1);
}

// this returns all but the last component of any path.  It has to take
// care at the root.
any_path
any_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  if (sep == string::npos)
    return any_path();

  // dirname() of the root directory is itself
  if (sep == s.size() - 1)
    return *this;

  // dirname() of a direct child of the root is the root
  if (sep == 0 || (sep == 1 && s[1] == '/')
#ifdef WIN32
      || (sep == 1 || sep == 2 && s[1] == ':')
#endif
      )
    return any_path(s, 0, sep+1);

  return any_path(s, 0, sep);
}

// these variations exist to get the return type right.  also,
// file_path dirname() can be a little simpler.
file_path
file_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    return file_path();
  return file_path(s, 0, sep);
}

system_path
system_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  I(sep != string::npos);

  // dirname() of the root directory is itself
  if (sep == s.size() - 1)
    return *this;

  // dirname() of a direct child of the root is the root
  if (sep == 0 || (sep == 1 && s[1] == '/')
#ifdef WIN32
      || (sep == 1 || sep == 2 && s[1] == ':')
#endif
      )
    return system_path(s, 0, sep+1);

  return system_path(s, 0, sep);
}


// produce dirname and basename at the same time
void
file_path::dirname_basename(file_path & dir, path_component & base) const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    {
      dir = file_path();
      base = path_component(s, 0);
    }
  else
    {
      I(sep < s.size() - 1); // last component must have at least one char
      dir = file_path(s, 0, sep);
      base = path_component(s, sep + 1);
    }
}

// count the number of /-separated components of the path.
unsigned int
file_path::depth() const
{
  if (data.empty())
    return 0;

  unsigned int components = 1;
  for (string::const_iterator p = data.begin(); p != data.end(); p++)
    if (*p == '/')
      components++;

  return components;
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
  return data;
#else
  // on normal systems we actually have some work to do, alas.
  // not much, though, because utf8_to_system_string does all the hard work.
  // it is carefully optimized.  do not screw it up.
  external out;
  utf8_to_system_strict(utf8(data), out);
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

template <>
void dump(file_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

template <>
void dump(system_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

template <>
void dump(bookkeeping_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

///////////////////////////////////////////////////////////////////////////
// path manipulation
// this code's speed does not matter much
///////////////////////////////////////////////////////////////////////////

// relies on its arguments already being validated, except that you may not
// append the empty path component, and if you are appending to the empty
// path, you may not create an absolute path or a path into the bookkeeping
// directory.
file_path
file_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  if (empty())
    {
      string const & s = to_append();
      I(!is_absolute_somewhere(s) && !in_bookkeeping_dir(s));
      return file_path(s, 0, string::npos);
    }
  else
    return file_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append(), 0, string::npos);
}

// similarly, but even less checking is needed.
file_path
file_path::operator /(file_path const & to_append) const
{
  I(!to_append.empty());
  if (empty())
    return to_append;
  return file_path(((*(data.end() - 1) == '/') ? data : data + "/")
                   + to_append.as_internal(), 0, string::npos);
}

bookkeeping_path
bookkeeping_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return bookkeeping_path(((*(data.end() - 1) == '/') ? data : data + "/")
                          + to_append(), 0, string::npos);
}

system_path
system_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return system_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append(), 0, string::npos);
}

any_path
any_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return any_path(((*(data.end() - 1) == '/') ? data : data + "/")
                  + to_append(), 0, string::npos);
}

// these take strings and validate
bookkeeping_path
bookkeeping_path::operator /(char const * to_append) const
{
  I(!is_absolute_somewhere(to_append));
  I(!empty());
  return bookkeeping_path(((*(data.end() - 1) == '/') ? data : data + "/")
                          + to_append);
}

system_path
system_path::operator /(char const * to_append) const
{
  I(!empty());
  I(!is_absolute_here(to_append));
  return system_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append);
}

///////////////////////////////////////////////////////////////////////////
// system_path
///////////////////////////////////////////////////////////////////////////

system_path::system_path(any_path const & other, bool in_true_workspace)
{
  if (is_absolute_here(other.as_internal()))
    // another system_path.  the normalizing isn't really necessary, but it
    // makes me feel warm and fuzzy.
    data = normalize_path(other.as_internal());
  else
    {
      system_path wr;
      if (in_true_workspace)
        wr = working_root.get();
      else
        wr = working_root.get_but_unused();
      data = normalize_path(wr.as_internal() + "/" + other.as_internal());
    }
}

static inline string const_system_path(utf8 const & path)
{
  N(!path().empty(), F("invalid path ''"));
  string expanded = tilde_expand(path());
  if (is_absolute_here(expanded))
    return normalize_path(expanded);
  else
    return normalize_path(initial_abs_path.get().as_internal()
                          + "/" + path());
}

system_path::system_path(string const & path)
{
  data = const_system_path(utf8(path));
}

system_path::system_path(utf8 const & path)
{
  data = const_system_path(utf8(path));
}

///////////////////////////////////////////////////////////////////////////
// workspace (and path root) handling
///////////////////////////////////////////////////////////////////////////

static bool
find_bookdir(system_path const & root, path_component const & bookdir,
             system_path & current, string & removed)
{
  current = initial_abs_path.get();
  removed.clear();

  // check that the current directory is below the specified search root
  if (current.as_internal().find(root.as_internal()) != 0)
    {
      W(F("current directory '%s' is not below root '%s'") % current % root);
      return false;
    }

  L(FL("searching for '%s' directory with root '%s'") % bookdir % root);

  system_path check;
  while (!(current == root))
    {
      check = current / bookdir;
      switch (get_path_status(check))
        {
        case path::nonexistent:
          L(FL("'%s' not found in '%s' with '%s' removed")
            % bookdir % current % removed);
          if (removed.empty())
            removed = current.basename()();
          else
            removed = current.basename()() + "/" + removed;
          current = current.dirname();
          continue;

        case path::file:
          L(FL("'%s' is not a directory") % check);
          return false;

        case path::directory:
          goto found;
        }
    }

  // if we get here, we have hit the root; try once more
  check = current / bookdir;
  switch (get_path_status(check))
    {
    case path::nonexistent:
      L(FL("'%s' not found in '%s' with '%s' removed")
        % bookdir % current % removed);
      return false;

    case path::file:
      L(FL("'%s' is not a directory") % check);
      return false;

    case path::directory:
      goto found;
    }
  return false;

 found:
  // check for _MTN/. and _MTN/.. to see if mt dir is readable
  try
    {
      if (!path_exists(check / ".") || !path_exists(check / ".."))
        {
          L(FL("problems with '%s' (missing '.' or '..')") % check);
          return false;
        }
    }
  catch(exception &)
    {
      L(FL("problems with '%s' (cannot check for '.' or '..')") % check);
      return false;
    }
  return true;
}


bool
find_and_go_to_workspace(string const & search_root)
{
  system_path root, current;
  string removed;

  if (search_root.empty())
    {
#ifdef WIN32
      std::string cur_str = get_current_working_dir();
      current = cur_str;
      if (cur_str[0] == '/' || cur_str[0] == '\\')
        {
          if (cur_str.size() > 1 && (cur_str[1] == '/' || cur_str[1] == '\\'))
            {
              // UNC name
              string::size_type uncend = cur_str.find_first_of("\\/", 2);
              if (uncend == string::npos)
                root = system_path(cur_str + "/");
              else
                root = system_path(cur_str.substr(0, uncend));
            }
          else
            root = system_path("/");
        }
      else if (cur_str.size() > 1 && cur_str[1] == ':')
        {
          root = system_path(cur_str.substr(0,2) + "/");
        }
      else I(false);
#else
      root = system_path("/");
#endif
    }
  else
    {
      root = system_path(search_root);
      L(FL("limiting search for workspace to %s") % root);

      require_path_is_directory(root,
                               F("search root '%s' does not exist") % root,
                               F("search root '%s' is not a directory") % root);
    }

  // first look for the current name of the bookkeeping directory.
  // if we don't find it, look for it under the old name, so that
  // migration has a chance to work.
  if (!find_bookdir(root, bookkeeping_root_component, current, removed))
    if (!find_bookdir(root, old_bookkeeping_root_component, current, removed))
      return false;

  working_root.set(current, true);
  initial_rel_path.set(removed, true);

  L(FL("working root is '%s'") % working_root.get_but_unused());
  L(FL("initial relative path is '%s'") % initial_rel_path.get_but_unused());

  change_current_working_dir(working_root.get_but_unused());

  return true;
}

void
go_to_workspace(system_path const & new_workspace)
{
  working_root.set(new_workspace, true);
  initial_rel_path.set(string(), true);
  change_current_working_dir(new_workspace);
}

void
mark_std_paths_used(void)
{
  working_root.get();
  initial_rel_path.get();
}

///////////////////////////////////////////////////////////////////////////
// tests
///////////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "randomizer.hh"

using std::logic_error;

UNIT_TEST(paths, path_component)
{
  char const * const baddies[] = {".",
                            "..",
                            "/foo",
                            "\\foo",
                            "foo/bar",
                            "foo\\bar",
                            0 };

  // these would not be okay in a full file_path, but are okay here.
  char const * const goodies[] = {"c:foo",
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_MTN",
                            0 };


  for (char const * const * c = baddies; *c; ++c)
    {
      // the comparison prevents the compiler from eliminating the
      // expression.
      UNIT_TEST_CHECK_THROW((path_component(*c)()) == *c, logic_error);
    }
  for (char const * const *c = goodies; *c; ++c)
    {
      path_component p(*c);
      UNIT_TEST_CHECK_THROW(file_path() / p, logic_error);
    }

  UNIT_TEST_CHECK_THROW(file_path_internal("foo") / path_component(),
                        logic_error);
}


UNIT_TEST(paths, file_path_internal)
{
  char const * const baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "_MTN",
                            "_MTN/blah",
                            "foo/bar/",
                            "foo/bar/.",
                            "foo/bar/./",
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
  initial_rel_path.set(string(), true);
  for (char const * const * c = baddies; *c; ++c)
    {
      UNIT_TEST_CHECK_THROW(file_path_internal(*c), logic_error);
    }
  initial_rel_path.unset();
  initial_rel_path.set("blah/blah/blah", true);
  for (char const * const * c = baddies; *c; ++c)
    {
      UNIT_TEST_CHECK_THROW(file_path_internal(*c), logic_error);
    }

  UNIT_TEST_CHECK(file_path().empty());
  UNIT_TEST_CHECK(file_path_internal("").empty());

  char const * const goodies[] = {"",
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
      initial_rel_path.set(i ? string()
                             : string("blah/blah/blah"),
                           true);
      for (char const * const * c = goodies; *c; ++c)
        {
          file_path fp = file_path_internal(*c);
          UNIT_TEST_CHECK(fp.as_internal() == *c);
          UNIT_TEST_CHECK(file_path_internal(fp.as_internal()) == fp);
        }
    }

  initial_rel_path.unset();
}

static void check_fp_normalizes_to(char const * before, char const * after)
{
  L(FL("check_fp_normalizes_to: '%s' -> '%s'") % before % after);
  file_path fp = file_path_external(utf8(before));
  L(FL("  (got: %s)") % fp);
  UNIT_TEST_CHECK(fp.as_internal() == after);
  UNIT_TEST_CHECK(file_path_internal(fp.as_internal()) == fp);
  // we compare after to the external form too, since as far as we know
  // relative normalized posix paths are always good win32 paths too
  UNIT_TEST_CHECK(fp.as_external() == after);
}

UNIT_TEST(paths, file_path_external_null_prefix)
{
  initial_rel_path.unset();
  initial_rel_path.set(string(), true);

  char const * const baddies[] = {"/foo",
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
  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_null_prefix: trying baddie: %s") % *c);
      UNIT_TEST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
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
  check_fp_normalizes_to("foo/bar/.", "foo/bar");
  check_fp_normalizes_to("foo/bar/./", "foo/bar");
  check_fp_normalizes_to("foo/./bar/", "foo/bar");
  check_fp_normalizes_to("./foo", "foo");
  //check_fp_normalizes_to("foo///.//", "foo");

  initial_rel_path.unset();
}

UNIT_TEST(paths, file_path_external_prefix__MTN)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("_MTN"), true);

  UNIT_TEST_CHECK_THROW(file_path_external(utf8("foo")), informative_failure);
  UNIT_TEST_CHECK_THROW(file_path_external(utf8(".")), informative_failure);
  UNIT_TEST_CHECK_THROW(file_path_external(utf8("./blah")), informative_failure);
  check_fp_normalizes_to("..", "");
  check_fp_normalizes_to("../foo", "foo");
}

UNIT_TEST(paths, file_path_external_prefix_a_b)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("a/b"), true);

  char const * const baddies[] = {"/foo",
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
  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_prefix_a_b: trying baddie: %s") % *c);
      UNIT_TEST_CHECK_THROW(file_path_external(utf8(*c)), informative_failure);
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
  check_fp_normalizes_to("foo/bar/.", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/./", "a/b/foo/bar");
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

UNIT_TEST(paths, basename)
{
  struct t
  {
    char const * in;
    char const * out;
  };
  // file_paths cannot be absolute, but may be the empty string.
  struct t const fp_cases[] = {
    { "",            ""    },
    { "foo",         "foo" },
    { "foo/bar",     "bar" },
    { "foo/bar/baz", "baz" },
    { 0, 0 }
  };
  // bookkeeping_paths cannot be absolute and must start with the
  // bookkeeping_root_component.
  struct t const bp_cases[] = {
    { "_MTN",         "_MTN" },
    { "_MTN/foo",     "foo"  },
    { "_MTN/foo/bar", "bar"  },
    { 0, 0 }
  };

  // system_paths must be absolute.  this relies on the setting of
  // initial_abs_path below.  note that most of the cases whose full paths
  // vary between Unix and Windows will still have the same basenames.
  struct t const sp_cases[] = {
    { "/",          ""      },
    { "//",         ""      },
    { "foo",        "foo"   },
    { "/foo",       "foo"   },
    { "//foo",      "foo"   },
    { "~/foo",      "foo"   },
    { "c:/foo",     "foo"   },
    { "foo/bar",    "bar"   },
    { "/foo/bar",   "bar"   },
    { "//foo/bar",  "bar"   },
    { "~/foo/bar",  "bar"   },
    { "c:/foo/bar", "bar"   },
#ifdef WIN32
    { "c:/",        ""      },
    { "c:foo",      "foo"   },
#else
    { "c:/",        "c:"    },
    { "c:foo",      "c:foo" },
#endif
    { 0, 0 }
  };

  UNIT_TEST_CHECKPOINT("file_path basenames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      file_path fp = file_path_internal(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  UNIT_TEST_CHECKPOINT("bookkeeping_path basenames");
  for (struct t const *p = bp_cases; p->in; p++)
    {
      bookkeeping_path fp(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }


  UNIT_TEST_CHECKPOINT("system_path basenames");

  initial_abs_path.unset();
  initial_abs_path.set(system_path("/a/b"), true);

  for (struct t const *p = sp_cases; p->in; p++)
    {
      system_path fp(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  // any_path::basename() should return exactly the same thing that
  // the corresponding specialized basename() does, but with type any_path.
  UNIT_TEST_CHECKPOINT("any_path basenames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      any_path ap(file_path_internal(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }
  for (struct t const *p = bp_cases; p->in; p++)
    {
      any_path ap(bookkeeping_path(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }
  for (struct t const *p = sp_cases; p->in; p++)
    {
      any_path ap(system_path(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  initial_abs_path.unset();
}

UNIT_TEST(paths, dirname)
{
  struct t
  {
    char const * in;
    char const * out;
  };
  // file_paths cannot be absolute, but may be the empty string.
  struct t const fp_cases[] = {
    { "",            ""        },
    { "foo",         ""        },
    { "foo/bar",     "foo"     },
    { "foo/bar/baz", "foo/bar" },
    { 0, 0 }
  };

  // system_paths must be absolute.  this relies on the setting of
  // initial_abs_path below.
  struct t const sp_cases[] = {
    { "/",          "/"           },
    { "//",         "//"          },
    { "foo",        "/a/b"        },
    { "/foo",       "/"           },
    { "//foo",      "//"          },
    { "~/foo",      "~"           },
    { "foo/bar",    "/a/b/foo"    },
    { "/foo/bar",   "/foo"        },
    { "//foo/bar",  "//foo"       },
    { "~/foo/bar",  "~/foo"       },
#ifdef WIN32
    { "c:",         "c:"          },
    { "c:foo",      "c:"          },
    { "c:/",        "c:/"         },
    { "c:/foo",     "c:/"         },
    { "c:/foo/bar", "c:/foo"      },
#else
    { "c:",         "/a/b"        },
    { "c:foo",      "/a/b"        },
    { "c:/",        "/a/b"        },
    { "c:/foo",     "/a/b/c:"     },
    { "c:/foo/bar", "/a/b/c:/foo" },
#endif
    { 0, 0 }
  };

  initial_abs_path.unset();

  UNIT_TEST_CHECKPOINT("file_path dirnames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      file_path fp = file_path_internal(p->in);
      file_path dn = fp.dirname();
      UNIT_TEST_CHECK_MSG(dn == file_path_internal(p->out),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % p->out);
    }


  initial_abs_path.set(system_path("/a/b"), true);
  UNIT_TEST_CHECKPOINT("system_path dirnames");
  for (struct t const *p = sp_cases; p->in; p++)
    {
      system_path fp(p->in);
      system_path dn(fp.dirname());

      UNIT_TEST_CHECK_MSG(dn == system_path(p->out),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % p->out);
    }

  // any_path::dirname() should return exactly the same thing that
  // the corresponding specialized dirname() does, but with type any_path.
  UNIT_TEST_CHECKPOINT("any_path dirnames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      any_path ap(file_path_internal(p->in));
      any_path dn(ap.dirname());
      any_path rf(file_path_internal(p->out));
      UNIT_TEST_CHECK_MSG(dn.as_internal() == rf.as_internal(),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % rf);
    }
  for (struct t const *p = sp_cases; p->in; p++)
    {
      any_path ap(system_path(p->in));
      any_path dn(ap.dirname());
      any_path rf(system_path(p->out));
      UNIT_TEST_CHECK_MSG(dn.as_internal() == rf.as_internal(),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % rf);
    }

  initial_abs_path.unset();
}

UNIT_TEST(paths, depth)
{
  char const * const cases[] = {"", "foo", "foo/bar", "foo/bar/baz", 0};
  for (unsigned int i = 0; cases[i]; i++)
    {
      file_path fp = file_path_internal(cases[i]);
      unsigned int d = fp.depth();
      UNIT_TEST_CHECK_MSG(d == i,
                          FL("depth('%s') = %d (expect %d)") % fp % d % i);
    }
}

static void check_bk_normalizes_to(char const * before, char const * after)
{
  bookkeeping_path bp(bookkeeping_root / before);
  L(FL("normalizing %s to %s (got %s)") % before % after % bp);
  UNIT_TEST_CHECK(bp.as_external() == after);
  UNIT_TEST_CHECK(bookkeeping_path(bp.as_internal()).as_internal() == bp.as_internal());
}

UNIT_TEST(paths, bookkeeping)
{
  char const * const baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "foo/bar/",
                            "foo/bar/.",
                            "foo/bar/./",
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

  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_bookkeeping_path baddie: trying '%s'") % *c);
            UNIT_TEST_CHECK_THROW(bookkeeping_path(tmp_path_string.assign(*c)),
                                  logic_error);
            UNIT_TEST_CHECK_THROW(bookkeeping_root / *c, logic_error);
    }

  // these are legitimate as things to append to bookkeeping_root, but
  // not as bookkeeping_paths in themselves.
  UNIT_TEST_CHECK_THROW(bookkeeping_path("a"), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(bookkeeping_root / "a", logic_error);
  UNIT_TEST_CHECK_THROW(bookkeeping_path("foo/bar"), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(bookkeeping_root / "foo/bar", logic_error);

  check_bk_normalizes_to("a", "_MTN/a");
  check_bk_normalizes_to("foo", "_MTN/foo");
  check_bk_normalizes_to("foo/bar", "_MTN/foo/bar");
  check_bk_normalizes_to("foo/bar/baz", "_MTN/foo/bar/baz");
}

static void check_system_normalizes_to(char const * before, char const * after)
{
  system_path sp(before);
  L(FL("normalizing '%s' to '%s' (got '%s')") % before % after % sp);
  UNIT_TEST_CHECK(sp.as_external() == after);
  UNIT_TEST_CHECK(system_path(sp.as_internal()).as_internal() == sp.as_internal());
}

UNIT_TEST(paths, system)
{
  initial_abs_path.unset();
  initial_abs_path.set(system_path("/a/b"), true);

  UNIT_TEST_CHECK_THROW(system_path(""), informative_failure);

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
  UNIT_TEST_CHECK(tilde_expanded[1] == ':');
#else
  UNIT_TEST_CHECK(tilde_expanded[0] == '/');
#endif
  UNIT_TEST_CHECK(tilde_expanded.find('~') == string::npos);
  // on Windows, ~name is not expanded
#ifdef WIN32
  UNIT_TEST_CHECK(system_path("~this_user_does_not_exist_anywhere")
                  .as_external()
                  == "/a/b/~this_user_does_not_exist_anywhere");
#else
  UNIT_TEST_CHECK_THROW(system_path("~this_user_does_not_exist_anywhere"),
                        informative_failure);
#endif

  // finally, make sure that the copy-from-any_path constructor works right
  // in particular, it should interpret the paths it gets as being relative to
  // the project root, not the initial path
  working_root.unset();
  working_root.set(system_path("/working/root"), true);
  initial_rel_path.unset();
  initial_rel_path.set(string("rel/initial"), true);

  UNIT_TEST_CHECK(system_path(system_path("foo/bar")).as_internal() == "/a/b/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(system_path("/foo/bar")).as_internal() == "/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_internal("foo/bar"), false).as_internal()
              == "/working/root/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_internal("foo/bar")).as_internal()
              == "/working/root/foo/bar");
  UNIT_TEST_CHECK(working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_external(utf8("foo/bar"))).as_external()
              == "/working/root/rel/initial/foo/bar");
  file_path a_file_path;
  UNIT_TEST_CHECK(system_path(a_file_path).as_external()
              == "/working/root");
  UNIT_TEST_CHECK(system_path(bookkeeping_path("_MTN/foo/bar")).as_internal()
              == "/working/root/_MTN/foo/bar");
  UNIT_TEST_CHECK(system_path(bookkeeping_root).as_internal()
              == "/working/root/_MTN");
  initial_abs_path.unset();
  working_root.unset();
  initial_rel_path.unset();
}

UNIT_TEST(paths, access_tracker)
{
  access_tracker<int> a;
  UNIT_TEST_CHECK_THROW(a.get(), logic_error);
  a.set(1, false);
  UNIT_TEST_CHECK_THROW(a.set(2, false), logic_error);
  a.set(2, true);
  UNIT_TEST_CHECK_THROW(a.set(3, false), logic_error);
  UNIT_TEST_CHECK(a.get() == 2);
  UNIT_TEST_CHECK_THROW(a.set(3, true), logic_error);
  a.unset();
  a.may_not_initialize();
  UNIT_TEST_CHECK_THROW(a.set(1, false), logic_error);
  UNIT_TEST_CHECK_THROW(a.set(2, true), logic_error);
  a.unset();
  a.set(1, false);
  UNIT_TEST_CHECK_THROW(a.may_not_initialize(), logic_error);
}

static void test_path_less_than(string const & left, string const & right)
{
  MM(left);
  MM(right);
  file_path left_fp = file_path_internal(left);
  file_path right_fp = file_path_internal(right);
  I(left_fp < right_fp);
}

static void test_path_equal(string const & left, string const & right)
{
  MM(left);
  MM(right);
  file_path left_fp = file_path_internal(left);
  file_path right_fp = file_path_internal(right);
  I(left_fp == right_fp);
}

UNIT_TEST(paths, ordering)
{
  // this ordering is very important:
  //   -- it is used to determine the textual form of csets and manifests
  //      (in particular, it cannot be changed)
  //   -- it is used to determine in what order cset operations can be applied
  //      (in particular, foo must sort before foo/bar, so that we can use it
  //      to do top-down and bottom-up traversals of a set of paths).
  test_path_less_than("a", "b");
  test_path_less_than("a", "c");
  test_path_less_than("ab", "ac");
  test_path_less_than("a", "ab");
  test_path_less_than("", "a");
  test_path_less_than("", ".foo");
  test_path_less_than("foo", "foo/bar");
  // . is before / asciibetically, so sorting by strings will give the wrong
  // answer on this:
  test_path_less_than("foo/bar", "foo.bar");

  // path_components used to be interned strings, and we used the default sort
  // order, which meant that in practice path components would sort in the
  // _order they were first used in the program_.  So let's put in a test that
  // would catch this sort of brokenness.
  test_path_less_than("fallanopic_not_otherwise_mentioned", "xyzzy");
  test_path_less_than("fallanoooo_not_otherwise_mentioned_and_smaller",
                       "fallanopic_not_otherwise_mentioned");
}

UNIT_TEST(paths, ordering_random)
{
  char x[4] = {0,0,0,0};
  char y[4] = {0,0,0,0};
  u8 a, b, c, d;
  const int ntrials = 1000;
  int i;
  randomizer rng;

  // use of numbers is intentional; these strings are defined to be UTF-8.

  UNIT_TEST_CHECKPOINT("a and b");
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      x[0] = a;
      y[0] = b;
      if (a < b)
        test_path_less_than(x, y);
      else if (a > b)
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }

  UNIT_TEST_CHECKPOINT("ab and cd");
  for (i = 0; i < ntrials; i++)
    {
      do
        {
          do a = rng.uniform(0x7f - 0x20) + 0x20;
          while (a == 0x5c || a == 0x2f); // '\\', '/'

          do b = rng.uniform(0x7f - 0x20) + 0x20;
          while (b == 0x5c || b == 0x2f || b == 0x3a); // '\\', '/', ':'
        }
      while (a == 0x2e && b == 0x2e);  // ".."

      do
        {
          do c = rng.uniform(0x7f - 0x20) + 0x20;
          while (c == 0x5c || c == 0x2f); // '\\', '/'

          do d = rng.uniform(0x7f - 0x20) + 0x20;
          while (d == 0x5c || d == 0x2f || d == 0x3a); // '\\', '/', ':'
        }
      while (c == 0x2e && d == 0x2e);  // ".."

      x[0] = a;
      x[1] = b;
      y[0] = c;
      y[1] = d;

      if (a < c || (a == c && b < d))
        test_path_less_than(x, y);
      else if (a > c || (a == c && b > d))
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }

  UNIT_TEST_CHECKPOINT("a and b/c");
  x[1] = 0;
  y[1] = '/';
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      x[0] = a;
      y[0] = b;
      y[2] = c;

      // only the order of a and b matters.  1 sorts before 1/2.
      if (a <= b)
        test_path_less_than(x, y);
      else
        test_path_less_than(y, x);
    }

  UNIT_TEST_CHECKPOINT("ab and c/d");
  for (i = 0; i < ntrials; i++)
    {
      do
        {
          do a = rng.uniform(0x7f - 0x20) + 0x20;
          while (a == 0x5c || a == 0x2f); // '\\', '/'

          do b = rng.uniform(0x7f - 0x20) + 0x20;
          while (b == 0x5c || b == 0x2f || b == 0x3a); // '\\', '/', ':'
        }
      while (a == 0x2e && b == 0x2e);  // ".."

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      do d = rng.uniform(0x7f - 0x20) + 0x20;
      while (d == 0x5c || d == 0x2f || d == 0x2e); // '\\', '/', '.'


      x[0] = a;
      x[1] = b;
      y[0] = c;
      y[2] = d;

      // only the order of a and c matters,
      // but this time, 12 sorts after 1/2.
      if (a < c)
        test_path_less_than(x, y);
      else
        test_path_less_than(y, x);
    }


  UNIT_TEST_CHECKPOINT("a/b and c/d");
  x[1] = '/';
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      do d = rng.uniform(0x7f - 0x20) + 0x20;
      while (d == 0x5c || d == 0x2f || d == 0x2e); // '\\', '/', '.'

      x[0] = a;
      x[2] = b;
      y[0] = c;
      y[2] = d;

      if (a < c || (a == c && b < d))
        test_path_less_than(x, y);
      else if (a > c || (a == c && b > d))
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }
}

UNIT_TEST(paths, test_internal_string_is_bookkeeping_path)
{
  char const * const yes[] = {"_MTN",
                        "_MTN/foo",
                        "_mtn/Foo",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                ::internal_string_is_bookkeeping_path(utf8(std::string(*c))));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                 ::internal_string_is_bookkeeping_path(utf8(std::string(*c))));
}

UNIT_TEST(paths, test_external_string_is_bookkeeping_path_prefix_none)
{
  initial_rel_path.unset();
  initial_rel_path.set(string(), true);

  char const * const yes[] = {"_MTN",
                        "_MTN/foo",
                        "_mtn/Foo",
                        "_MTN/foo/..",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       "_MTN/..",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                 ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
}

UNIT_TEST(paths, test_external_string_is_bookkeeping_path_prefix_a_b)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("a/b"), true);

  char const * const yes[] = {"../../_MTN",
                        "../../_MTN/foo",
                        "../../_mtn/Foo",
                        "../../_MTN/foo/..",
                        "../../foo/../_MTN/foo",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       "_MTN",
                       "../../foo/_MTN",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                 ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
}

UNIT_TEST(paths, test_external_string_is_bookkeeping_path_prefix__MTN)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("_MTN"), true);

  char const * const yes[] = {".",
                        "foo",
                        "../_MTN/foo/..",
                        "../_mtn/foo",
                        "../foo/../_MTN/foo",
                        0 };
  char const * const no[] = {"../foo",
                       "../foo/bar",
                       "../foo/_MTN",
#ifdef WIN32
                       "c:/foo/foo", // don't throw informative_failure exception
#else
                       "/foo/foo", // don't throw informative_failure exception
#endif
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                 ::external_string_is_bookkeeping_path(utf8(std::string(*c))));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
