#ifndef __PATHS_HH__
#define __PATHS_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// safe, portable, fast, simple path handling -- in that order.
// but they all count.
//
// this file defines the vocabulary we speak in when dealing with the
// filesystem.  this is an extremely complex problem by the time one worries
// about normalization, security issues, character sets, and so on;
// furthermore, path manipulation has historically been a performance
// bottleneck in monotone.  so the goal here is the efficient implementation
// of a design that makes it hard or impossible to introduce as many classes
// of bugs as possible.
//
// Our approach is to have three different types of paths:
//   -- system_path
//      this is a path to anywhere in the fs.  it is in native format.  it is
//      always absolute.  when constructed from a string, it interprets the
//      string as being relative to the directory that monotone was run in.
//      (note that this may be different from monotone's current directory, as
//      when run in workspace monotone chdir's to the project root.)
//
//      one can also construct a system_path from one of the below two types
//      of paths.  this is intelligent, in that it knows that these sorts of
//      paths are considered to be relative to the project root.  thus
//        system_path(file_path_internal("foo"))
//      is not, in general, the same as
//        system_path("foo")
//
//   -- file_path
//      this is a path representing a versioned file.  it is always
//      a fully normalized relative path, that does not escape the project
//      root.  it is always relative to the project root.
//      you cannot construct a file_path directly from a string; you must pick
//      a constructor:
//        file_path_internal: use this for strings that come from
//          "monotone-internal" places, e.g. parsing revisions.  this turns on
//          stricter checking -- the string must already be normalized -- and
//          is extremely fast.  such strings are interpreted as being relative
//          to the project root.
//        file_path_external: use this for strings that come from the user.
//          these strings are normalized before being checked, and if there
//          is a problem trigger N() invariants rather than I() invariants.
//          if in a workspace, such strings are interpreted as being
//          _relative to the user's original directory_. if not in a
//          workspace, strings are treated as relative to the tree root. The
//          null string is accepted as referring to the workspace root
//          directory, because that is how file_path.as_external() outputs
//          that directory.
//      file_path's also provide optimized splitting and joining
//      functionality.
//
//   -- bookkeeping_path
//      this is a path representing something in the _MTN/ directory of a
//      workspace.  it has the same format restrictions as a file_path,
//      except instead of being forbidden to point into the _MTN directory, it
//      is _required_ to point into the _MTN directory.  the one constructor is
//      strict, and analogous to file_path_internal.  however, the normal way
//      to construct bookkeeping_path's is to use the global constant
//      'bookkeeping_root', which points to the _MTN directory.  Thus to
//      construct a path pointing to _MTN/options, use:
//          bookkeeping_root / "options"
//
// All path types should always be constructed from utf8-encoded strings.
//
// All path types provide an "operator /" which allows one to construct new
// paths pointing to things underneath a given path.  E.g.,
//     file_path_internal("foo") / "bar" == file_path_internal("foo/bar")
//
// All path types subclass 'any_path', which provides:
//    -- emptyness checking with .empty()
//    -- a method .as_internal(), which returns the utf8-encoded string
//       representing this path for internal use.  for instance, this is the
//       string that should be embedded into the text of revisions.
//    -- a method .as_external(), which returns a std::string suitable for
//       passing to filesystem interface functions. in practice, this means
//       that it is recoded into an appropriate character set, etc. For
//       bookkeeping_path and file_path, .as_external() is relative to the
//       workspace root.
//    -- a operator<< for ostreams.  this should always be used when writing
//       out paths for display to the user.  at the moment it just calls one
//       of the above functions, but this is _not_ correct.  there are
//       actually 3 different logical character sets -- internal (utf8),
//       user (locale-specific), and filesystem (locale-specific, except
//       when it's not, i.e., on OS X).  so we need three distinct operations,
//       and you should use the correct one.
//
//       all this means that when you want to print out a path, you usually
//       want to just say:
//           F("my path is %s") % my_path
//       i.e., nothing fancy necessary, for purposes of F() just treat it like
//       it were a string

#include <boost/shared_ptr.hpp>

class any_path;
class file_path;
class roster_t;
class utf8;

// A path_component is one component of a path.  It is always utf8, may not
// contain either kind of slash, and may not be a magic directory entry ("."
// or "..") It _may_ be the empty string, but you only get that if you ask
// for the basename of the root directory.  It resembles, but is not, a
// vocab type.

class path_component
{
public:
  path_component() : data() {}
  explicit path_component(utf8 const &);
  explicit path_component(std::string const &);
  explicit path_component(char const *);

  std::string const & operator()() const { return data; }
  bool empty() const { return data.empty(); }
  bool operator<(path_component const & other) const
  { return data < other(); }
  bool operator==(path_component const & other) const
  { return data == other(); }
  bool operator!=(path_component const & other) const
  { return data != other(); }

  friend std::ostream & operator<<(std::ostream &, path_component const &);

private:
  std::string data;

  // constructor for use by trusted operations.  bypasses validation.
  path_component(std::string const & path,
                 std::string::size_type start,
                 std::string::size_type stop = std::string::npos)
    : data(path.substr(start, stop))
  {}

  friend class any_path;
  friend class file_path;
  friend class roster_t;
};
std::ostream & operator<<(std::ostream &, path_component const &);
template <> void dump(path_component const &, std::string &);

// It's possible this will become a proper virtual interface in the future,
// but since the implementation is exactly the same in all cases, there isn't
// much point ATM...
class any_path
{
public:
  // converts to native charset and path syntax
  // this is a path that you can pass to the operating system
  std::string as_external() const;
  // leaves as utf8
  std::string const & as_internal() const
  { return data; }
  bool empty() const
  { return data.empty(); }
  // returns the trailing component of the path
  path_component basename() const;

  // a few places need to manipulate any_paths (notably the low-level stuff
  // in file_io.cc).
  any_path operator /(path_component const &) const;
  any_path dirname() const;

  any_path(any_path const & other)
    : data(other.data) {}
  any_path & operator=(any_path const & other)
  { data = other.data; return *this; }

protected:
  std::string data;
  any_path() {}

private:
  any_path(std::string const & path,
           std::string::size_type start,
           std::string::size_type stop = std::string::npos)
  {
    data = path.substr(start, stop);
  }
};

std::ostream & operator<<(std::ostream & o, any_path const & a);

class file_path : public any_path
{
public:
  file_path() {}
  // join a file_path out of pieces
  file_path operator /(path_component const & to_append) const;
  file_path operator /(file_path const & to_append) const;

  // these functions could be defined on any_path but are only needed
  // for file_path, and not defining them for system_path gets us out
  // of nailing down the semantics near the absolute root.

  // returns a path with the last component removed.
  file_path dirname() const;

  // does dirname() and basename() at the same time, for efficiency
  void dirname_basename(file_path &, path_component &) const;

  // returns the number of /-separated components of the path.
  // The empty path has depth zero.
  unsigned int depth() const;

  // ordering...
  bool operator==(const file_path & other) const
  { return data == other.data; }

  bool operator!=(const file_path & other) const
  { return data != other.data; }

  // the ordering on file_path is not exactly that of strings.
  // see the "ordering" unit test in paths.cc.
  bool operator <(const file_path & other) const
  {
    std::string::const_iterator p = data.begin();
    std::string::const_iterator plim = data.end();
    std::string::const_iterator q = other.data.begin();
    std::string::const_iterator qlim = other.data.end();

    while (p != plim && q != qlim && *p == *q)
      p++, q++;

    if (p == plim && q == qlim) // equal -> not less
      return false;

    // must do end of string before everything else, or 'foo' will sort
    // after 'foo/bar' which is not what we want.
    if (p == plim)
      return true;
    if (q == qlim)
      return false;

    // the only special case needed is that / sorts before everything -
    // this gives the effect of component-by-component comparison.
    if (*p == '/')
      return true;
    if (*q == '/')
      return false;

    // ensure unsigned comparison
    return static_cast<unsigned char>(*p) < static_cast<unsigned char>(*q);
  }

  void clear() { data.clear(); }

private:
  typedef enum { internal, external } source_type;
  // input is always in utf8, because everything in our world is always in
  // utf8 (except interface code itself).
  // external paths:
  //   -- are converted to internal syntax (/ rather than \, etc.)
  //   -- normalized
  //   -- if not 'to_workspace_root', assumed to be relative to the user's
  //      cwd, and munged to become relative to root of the workspace
  //      instead
  // internal and external paths:
  //   -- are confirmed to be normalized and relative
  //   -- not to be in _MTN/
  file_path(source_type type, std::string const & path, bool to_workspace_root);
  file_path(source_type type, utf8 const & path, bool to_workspace_root);
  friend file_path file_path_internal(std::string const & path);
  friend file_path file_path_external(utf8 const & path);
  friend file_path file_path_external_ws(utf8 const & path);

  // private substring constructor, does no validation.  used by dirname()
  // and operator/ with a path_component.
  file_path(std::string const & path,
            std::string::size_type start,
            std::string::size_type stop = std::string::npos)
  {
    data = path.substr(start, stop);
  }

  // roster_t::get_name is allowed to use the private substring constructor.
  friend class roster_t;
};

// these are the public file_path constructors. path is relative to the
// current working directory.
inline file_path file_path_internal(std::string const & path)
{
  return file_path(file_path::internal, path, false);
}
inline file_path file_path_external(utf8 const & path)
{
  return file_path(file_path::external, path, false);
}

// path is relative to the workspace root
inline file_path file_path_external_ws(utf8 const & path)
{
  return file_path(file_path::external, path, true);
}

class bookkeeping_path : public any_path
{
public:
  bookkeeping_path() {}
  // path _should_ contain the leading _MTN/
  // and _should_ look like an internal path
  // usually you should just use the / operator as a constructor!
  bookkeeping_path(std::string const &);
  bookkeeping_path operator /(char const *) const;
  bookkeeping_path operator /(path_component const &) const;

  // exposed for the use of walk_tree and friends
  static bool internal_string_is_bookkeeping_path(utf8 const & path);
  static bool external_string_is_bookkeeping_path(utf8 const & path);
  bool operator==(const bookkeeping_path & other) const
  { return data == other.data; }

  bool operator <(const bookkeeping_path & other) const
  { return data < other.data; }

private:
  bookkeeping_path(std::string const & path,
                   std::string::size_type start,
                   std::string::size_type stop = std::string::npos)
  {
    data = path.substr(start, stop);
  }
};

// these are #defines so that they will be constructed lazily, when
// used.  this is necessary for correct behavior; the path constructors
// use sanity.hh assertions and therefore must not run before
// sanity::initialize is called.

#define bookkeeping_root (bookkeeping_path("_MTN"))
#define bookkeeping_root_component (path_component("_MTN"))
// for migration
#define old_bookkeeping_root_component (path_component("MT"))

// this will always be an absolute path
class system_path : public any_path
{
public:
  system_path() {};
  system_path(system_path const & other) : any_path(other) {};

  // the optional argument takes some explanation.  this constructor takes a
  // path relative to the workspace root.  the question is how to interpret
  // that path -- since it's possible to have multiple workspaces over the
  // course of a the program's execution (e.g., if someone runs 'checkout'
  // while already in a workspace).  if 'true' is passed (the default),
  // then monotone will trigger an invariant if the workspace changes after
  // we have already interpreted the path relative to some other working
  // copy.  if 'false' is passed, then the path is taken to be relative to
  // whatever the current workspace is, and will continue to reference it
  // even if the workspace later changes.
  explicit system_path(any_path const & other,
                       bool in_true_workspace = true);
  // this path can contain anything, and it will be absolutified and
  // tilde-expanded.  it will considered to be relative to the directory
  // monotone started in.  it should be in utf8.
  system_path(std::string const & path);
  system_path(utf8 const & path);

  bool operator==(const system_path & other) const
  { return data== other.data; }

  bool operator <(const system_path & other) const
  { return data < other.data; }

  system_path operator /(path_component const & to_append) const;
  system_path operator /(char const * to_append) const;
  system_path dirname() const;

private:
  system_path(std::string const & path,
              std::string::size_type start,
              std::string::size_type stop = std::string::npos)
  {
    data = path.substr(start, stop);
  }
};

template <> void dump(file_path const & sp, std::string & out);
template <> void dump(bookkeeping_path const & sp, std::string & out);
template <> void dump(system_path const & sp, std::string & out);

// Return a file_path, bookkeeping_path, or system_path, as appropriate.
// 'path' is an external path. If to_workspace_root, path is relative to
// workspace root, or absolute. Otherwise, it is relative to the current
// working directory, or absolute.
boost::shared_ptr<any_path> new_optimal_path(std::string path, bool to_workspace_root);

// record the initial path.  must be called before any use of system_path.
void
save_initial_path();

// returns true if workspace found, in which case cwd has been changed
// returns false if workspace not found
bool
find_and_go_to_workspace(std::string const & search_root);

// this is like change_current_working_dir, but also initializes the various
// root paths that are needed to interpret paths
void
go_to_workspace(system_path const & new_workspace);

void mark_std_paths_used(void);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
