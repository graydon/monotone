#ifndef __PATHS_H__
#define __PATHS_H__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// safe, portable, fast, simple file handling -- in that order

#include <ios_fwd>
#include <string>
#include <vector>

#include "numeric_vocab.hh"
#include "vocab.hh"

typedef u32 path_component;

const path_component the_null_component = 0;

inline bool
null_name(path_component pc)
{
  return pc == the_null_component;
}

// It's possible this will become a proper virtual interface in the future,
// but since the implementation is exactly the same in all cases, there isn't
// much point ATM...
class any_path
{
public:
  // converts to native charset and path syntax
  std::string as_external() const;
  // leaves as utf8
  utf8 const & as_internal() const
  { return data; }
protected:
  utf8 data;
private:
  any_path();
  any_path(any_path const & other);
  any_path & operator=(any_path const & other);
}

std::ostream & operator<<(std::ostream & o, any_path const & a);

class file_path : public any_path
{
public:
  file_path();
  // join a file_path out of pieces
  file_path(std::vector<path_component> const & pieces);
  
  file_path operator /(std::string const & to_append);

  void split(std::vector<path_component> & pieces) const;

  bool operator ==(const file_path & other) const
  { return data == other.data; }

  bool operator !=(const file_path & other) const
  { return data != other.data; }

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
  //      to become relative to root of the working copy instead
  // both types of paths:
  //   -- are confirmed to be normalized and relative
  //   -- not to be in MT/
  file_path(source_type type, std::string const & path);
  file_path(source_type type, utf8 const & path);
  friend file_path file_path_internal(std::string const & path);
  friend file_path file_path_external(std::string const & path);
  
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
  bookkeeping_path();
  // path _should_ contain the leading MT/
  // and _should_ look like an internal path
  // usually you should just use the / operator as a constructor!
  bookkeeping_path(std::string const & path);
  bookkeeping_path(utf8 const & path);
  std::string as_external() const;
  bookkeeping_path operator /(std::string const & to_append);
};

extern bookkeeping_path const bookkeeping_root;

  // this will always be an absolute path
class system_path : public any_path
{
public:
  system_path();
  // this path can contain anything, and it will be absolutified and
  // tilde-expanded.  it will considered to be relative to the directory
  // monotone started in.  it should be in utf8.
  system_path(std::string const & path);
  system_path(utf8 const & path);
  bool empty() const;
  system_path operator /(std::string const & to_append);
};


void
save_initial_path();

// returns true if working copy found, in which case cwd has been changed
// returns false if working copy not found
bool
find_and_go_to_working_copy(system_path const & search_root);

#endif
