#ifndef __PATHS_H__
#define __PATHS_H__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// safe, portable, fast, simple file handling -- in that order

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

void
save_initial_path();

// returns true if working copy found, in which case cwd has been changed
// returns false if working copy not found
bool
find_and_go_to_working_copy(external_path const & search_root);


class file_path
{
public:
  enum { internal, external } source_type;
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
  std::string const & as_internal();
  // converts to native charset and path syntax
  std::string const & as_external();

  void split(std::vector<path_component> & pieces);

  bool operator ==(const file_path & other)
  { return data == other.data; }

  bool operator !=(const file_path & other)
  { return data != other.data; }

  bool operator <(const file_path & other)
  { return data < other.data; }

private:
  // this string is always stored in normalized etc. form; so the generated
  // copy constructor and assignment operator are fine.
  std::string data;
};

class bookkeeping_path
{
public:
  // path should _not_ contain the leading MT/
  bookkeeping_path(std::string const & path);
  std::string const & as_external();
private:
  std::string data;
}

class external_path
{
public:
  // this path will 
  external_path(std::string const & path);
  // this will always be an absolute path
  std::string const & as_external();
private:
  std::string data;
}
