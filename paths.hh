#ifndef __PATHS_H__
#define __PATHS_H__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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
//          these strings are normalized before being checked, and if there is
//          a problem trigger N() invariants rather than I() invariants.  if in 
//          a workspace, such strings are interpreted as being 
//          _relative to the user's original directory_.  
//          if not in a workspace, strings are treated as referring to some 
//          database object directly.
//      file_path's also provide optimized splitting and joining
//      functionality.
//
//   -- bookkeeping_path
//      this is a path representing something in the MT/ directory of a
//      workspace.  it has the same format restrictions as a file_path,
//      except instead of being forbidden to point into the MT directory, it
//      is _required_ to point into the MT directory.  the one constructor is
//      strict, and analogous to file_path_internal.  however, the normal way
//      to construct bookkeeping_path's is to use the global constant
//      'bookkeeping_root', which points to the MT directory.  Thus to
//      construct a path pointing to MT/options, use:
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
//       passing to filesystem interface functions.  in practice, this means
//       that it is recoded into an appropriate character set, etc.
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
//
//
// There is also one "not really a path" type, 'split_path'.  This is a vector
// of path_component's, and semantically equivalent to a file_path --
// file_path's can be split into split_path's, and split_path's can be joined
// into file_path's.


#include <iosfwd>
#include <string>
#include <vector>

#include "vocab.hh"

typedef std::vector<path_component> split_path;

const path_component the_null_component;

inline bool
null_name(path_component pc)
{
  return pc == the_null_component;
}

template <> void dump(split_path const & sp, std::string & out);

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
  { return data(); }
  bool empty() const
  { return data().empty(); }
protected:
  utf8 data;
  any_path() {}
  any_path(any_path const & other)
    : data(other.data) {}
  any_path & operator=(any_path const & other)
  { data = other.data; return *this; }
};

std::ostream & operator<<(std::ostream & o, any_path const & a);
std::ostream & operator<<(std::ostream & o, split_path const & s);

class file_path : public any_path
{
public:
  file_path() {}
  // join a file_path out of pieces
  file_path(split_path const & sp);
  
  // this currently doesn't do any normalization or anything.
  file_path operator /(std::string const & to_append) const;

  void split(split_path & sp) const;

  bool operator ==(const file_path & other) const
  { return data == other.data; }

  bool operator <(const file_path & other) const
  { return data < other.data; }

private:
  typedef enum { internal, external } source_type;
  // input is always in utf8, because everything in our world is always in
  // utf8 (except interface code itself).
  // external paths:
  //   -- are converted to internal syntax (/ rather than \, etc.)
  //   -- normalized
  //   -- assumed to be relative to the user's cwd, and munged
  //      to become relative to root of the workspace instead
  // both types of paths:
  //   -- are confirmed to be normalized and relative
  //   -- not to be in MT/
  file_path(source_type type, std::string const & path);
  friend file_path file_path_internal(std::string const & path);
  friend file_path file_path_external(utf8 const & path);
};

// these are the public file_path constructors
inline file_path file_path_internal(std::string const & path)
{
  return file_path(file_path::internal, path);
}
inline file_path file_path_external(utf8 const & path)
{
  return file_path(file_path::external, path());
}

class bookkeeping_path : public any_path
{
public:
  bookkeeping_path() {};
  // path _should_ contain the leading MT/
  // and _should_ look like an internal path
  // usually you should just use the / operator as a constructor!
  bookkeeping_path(std::string const & path);
  bookkeeping_path operator /(std::string const & to_append) const;
  // exposed for the use of walk_tree
  static bool is_bookkeeping_path(std::string const & path);
  bool operator ==(const bookkeeping_path & other) const
  { return data == other.data; }

  bool operator <(const bookkeeping_path & other) const
  { return data < other.data; }
};

extern bookkeeping_path const bookkeeping_root;

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
  system_path operator /(std::string const & to_append) const;
};


void
save_initial_path();

// returns true if workspace found, in which case cwd has been changed
// returns false if workspace not found
bool
find_and_go_to_workspace(system_path const & search_root);

// this is like change_current_working_dir, but also initializes the various
// root paths that are needed to interpret paths
void
go_to_workspace(system_path const & new_workspace);

typedef std::set<split_path> path_set;

#endif
