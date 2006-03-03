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

// explicit in the sense that the path was explicitly given on the command line
// implicit in the sense that parent directories are included for explicit paths
// FIXME: should we really be doing implicit includes of parents?

// TODO: move these into the class below?!?
enum path_state { explicit_include, explicit_exclude, implicit_include };
  
class restriction
{
 public:
  restriction(app_state & a) : app(a) {}

  restriction(vector<utf8> const & includes,
              vector<utf8> const & excludes,
              roster_t const & roster, 
              app_state & a) :
    app(a)
  {
    map_paths(includes, excludes);
    map_nodes(roster);
    validate();
  }
  
  restriction(vector<utf8> const & includes,
              vector<utf8> const & excludes,
              roster_t const & roster1,
              roster_t const & roster2,
              app_state & a) :
    app(a)
  {
    map_paths(includes, excludes);
    map_nodes(roster1);
    map_nodes(roster2);
    validate();
  }

  bool includes(roster_t const & roster, node_id nid) const;

  bool includes(split_path const & sp) const;

  bool empty() const { return included_paths.empty() && excluded_paths.empty(); }

  restriction & operator=(restriction const & other)
  {
    included_paths = other.included_paths;
    excluded_paths = other.excluded_paths;
    known_paths = other.known_paths;
    node_map = other.node_map;
    path_map = other.path_map;
    return *this;
  }

 private:

  app_state & app;
  path_set included_paths, excluded_paths, known_paths;

  // we maintain maps by node_id and also by split_path, which is not
  // particularly nice, but paths are required for checking unknown and ignored
  // files
  map<node_id, path_state> node_map;
  map<split_path, path_state> path_map;

  void map_paths(vector<utf8> const & includes,
                 vector<utf8> const & excludes);

  void map_nodes(roster_t const & roster);

  void validate();
};

#endif  // header guard
