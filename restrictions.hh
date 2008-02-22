#ifndef __RESTRICTIONS_HH__
#define __RESTRICTIONS_HH__

// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// the following commands accept file arguments and --exclude and --depth
// options used to define a restriction on the files that will be processed:
//
// ls unknown
// ls ignored
// ls missing
// ls known
// status
// diff
// commit
// revert
//
// it is important that these commands operate on the same set of files given
// the same restriction specification.  this allows for destructive commands
// (commit and revert) to be "tested" first with non-destructive commands
// (ls unknown/ignored/missing/known, status, diff)

#include <set>
#include "vocab.hh"
#include "rev_types.hh"

class workspace;

// between any two related revisions, A and B, there is a set of changes (a
// cset) that describes the operations required to get from A to B. for example:
//
// revision A ... changes ... revision B
//
// a restriction is a means of masking off some of these changes to produce a
// third revision, X that lies somewhere between A and B.  changes included by
// the restriction when applied to revision A would produce revision X.  changes
// excluded by the restriction when applied to revision X would produce revision
// B.
//
// conceptually, a restriction allows for something like a sliding control for
// selecting the changes between revisions A and B. when the control is "all the
// way to the right" all changes are included and X == B. when then control is
// "all the way to the left" no changes are included and X == A. when the
// control is somewhere between these extremes X is a new revision.
//
// revision A ... included ... revision X ... excluded ... revision B

namespace restricted_path
{
  enum status { included, excluded };
}

class restriction
{
 public:
  bool empty() const { return included_paths.empty() && excluded_paths.empty(); }

 protected:
  restriction() : depth(-1) {}

  restriction(std::vector<file_path> const & includes,
              std::vector<file_path> const & excludes,
              long depth);

  std::set<file_path> included_paths, excluded_paths;
  long depth;
};

class node_restriction : public restriction
{
 public:
  node_restriction() : restriction() {}

  node_restriction(std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   roster_t const & roster);

  node_restriction(std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   roster_t const & roster1,
                   roster_t const & roster2);

  node_restriction(std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   parent_map const & rosters1,
                   roster_t const & roster2);

#ifndef BUILD_UNIT_TESTS
  node_restriction(workspace & work,
                   std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   roster_t const & roster);

  node_restriction(workspace & work,
                   std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   roster_t const & roster1,
                   roster_t const & roster2);

  node_restriction(workspace & work,
                   std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   parent_map const & rosters1,
                   roster_t const & roster2);
#endif
  
  bool includes(roster_t const & roster, node_id nid) const;

  node_restriction & operator=(node_restriction const & other)
  {
    included_paths = other.included_paths;
    excluded_paths = other.excluded_paths;
    depth = other.depth;
    known_paths = other.known_paths;
    node_map = other.node_map;
    return *this;
  }

 private:
  std::set<file_path> known_paths;
  std::map<node_id, restricted_path::status> node_map;
};

class path_restriction : public restriction
{
 public:
  enum validity_check { check_paths = 0, skip_check };

  path_restriction() : restriction() {}

  path_restriction(std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   validity_check vc = check_paths);

#ifndef BUILD_UNIT_TESTS
  path_restriction(workspace & work,
                   std::vector<file_path> const & includes,
                   std::vector<file_path> const & excludes,
                   long depth,
                   validity_check vc = check_paths);
#endif

  bool includes(file_path const & sp) const;

 private:
  std::map<file_path, restricted_path::status> path_map;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif  // header guard
