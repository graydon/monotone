#ifndef __RESTRICTIONS_HH__
#define __RESTRICTIONS_HH__

// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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

#include "app_state.hh"
#include "cset.hh"
#include "roster.hh"
#include "vocab.hh"

using std::map;
using std::vector;

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

class restriction
{
 public:
  restriction() {}

  restriction(vector<utf8> const & args,
              roster_t const & roster);

  restriction(vector<utf8> const & args,
              roster_t const & roster1,
              roster_t const & roster2);

  bool includes(roster_t const & roster, node_id nid) const;
  bool includes(split_path const & sp) const;
  bool empty() const { return restricted_node_map.empty(); }

 private:
  // true in these two maps indicates an explicitly included node or path and
  // explicitly included directories are recursive. alternatively, false in
  // these maps indicated an implicitly included parent directory which is
  // specifically required for the restriction but does not include any of its
  // children
 
  map<node_id, bool> restricted_node_map;
  map<split_path, bool> restricted_path_map;

  path_set paths;

  path_set valid_paths;

  void set_paths(vector<utf8> const & args);
  void add_nodes(roster_t const & roster);
  void check_paths();
};

#endif  // header guard
