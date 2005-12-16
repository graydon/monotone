#ifndef __RESTRICTIONS_HH__
#define __RESTRICTIONS_HH__

// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "cset.hh"
#include "roster.hh"
#include "vocab.hh"

using std::map;

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
  void add_nodes(roster_t const & roster, path_set const & paths);
  bool includes(roster_t const & roster, node_id nid) const;
  
 private:
  typedef map<node_id, bool> restriction_map;
  restriction_map restricted_node_map;
  
  void insert(node_id nid, bool recursive);
};

void 
extract_rearranged_paths(cset const & rearrangement, 
                         path_set & paths);

void 
add_intermediate_paths(path_set & paths);

void 
restrict_cset(cset const & work, 
              cset & included,
              cset & excluded,
              app_state & app);

void
remap_paths(path_set const & old_paths,
            roster_t const & r_old,
            cset const & work,
            path_set & new_paths);

void 
get_base_roster_and_working_cset(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_id & old_revision_id,
                                 roster_t & old_roster,
                                 path_set & old_paths, 
                                 path_set & new_paths,
                                 cset & included,
                                 cset & excluded);

void 
get_working_revision_and_rosters(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_set & rev,
                                 roster_t & old_roster,
                                 roster_t & new_roster,
                                 cset & excluded);

// Same as above, only without the "excluded" out-parameter.
void
get_working_revision_and_rosters(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_set & rev,
                                 roster_t & old_roster,
                                 roster_t & new_roster);

void
get_unrestricted_working_revision_and_rosters(app_state & app, 
                                              revision_set & rev,
                                              roster_t & old_roster,
                                              roster_t & new_roster);

void
calculate_restricted_cset(app_state & app, 
                          std::vector<utf8> const & args,
                          cset const & cs,
                          cset & included,
                          cset & excluded);

#endif  // header guard
