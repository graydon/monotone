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
protected:
  utf8 data;
private:
  any_path();
  any_path(any_path const & other);
  any_path & operator=(any_path const & other);
}

std::ostream & operator<<(ostream & o, any_path const & a);

class file_path : public any_path
{
public:
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
  // join a file_path out of pieces
  file_path(std::vector<path_component> const & pieces);
  
  // returns raw normalized path string
  // the string is always stored in normalized etc. form; so the generated
  // copy constructor and assignment operator are fine.
  std::string const & as_internal() const
  { return data(); }

  void split(std::vector<path_component> & pieces) const;

  bool operator ==(const file_path & other) const
  { return data == other.data; }

  bool operator !=(const file_path & other) const
  { return data != other.data; }

  bool operator <(const file_path & other) const
  { return data < other.data; }
};

class bookkeeping_path : public any_path
{
public:
  // path should _not_ contain the leading MT/
  // and _should_ look like an internal path
  bookkeeping_path(std::string const & path);
  std::string as_external() const;
};

  // this will always be an absolute path
class system_path : public any_path
{
public:
  // this path can contain anything, and it will be absolutified and
  // tilde-expanded.  it should be in utf8.
  system_path(std::string const & path);
  bool empty() const;
};


void
save_initial_path();

// returns true if working copy found, in which case cwd has been changed
// returns false if working copy not found
bool
find_and_go_to_working_copy(system_path const & search_root);

#endif
