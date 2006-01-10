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

struct restriction
{
  roster_t base_roster;
  roster_t current_roster;
  roster_t restricted_roster;
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
