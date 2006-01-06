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
#include "change_set.hh"
#include "vocab.hh"

void extract_rearranged_paths(change_set::path_rearrangement 
                              const & rearrangement, path_set & paths);

void add_intermediate_paths(path_set & paths);

void restrict_path_rearrangement(change_set::path_rearrangement const & work, 
                                 change_set::path_rearrangement & included,
                                 change_set::path_rearrangement & excluded,
                                 app_state & app);

void calculate_restricted_rearrangement(app_state & app, 
                                        std::vector<utf8> const & args,
                                        manifest_id & old_manifest_id,
                                        revision_id & old_revision_id,
                                        manifest_map & m_old,
                                        path_set & old_paths, 
                                        path_set & new_paths,
                                        change_set::path_rearrangement & included,
                                        change_set::path_rearrangement & excluded);

void calculate_restricted_revision(app_state & app, 
                                   std::vector<utf8> const & args,
                                   revision_set & rev,
                                   manifest_map & m_old,
                                   manifest_map & m_new,
                                   change_set::path_rearrangement & excluded);

void calculate_restricted_revision(app_state & app, 
                                   std::vector<utf8> const & args,
                                   revision_set & rev,
                                   manifest_map & m_old,
                                   manifest_map & m_new);

void calculate_unrestricted_revision(app_state & app, 
                                     revision_set & rev,
                                     manifest_map & m_old,
                                     manifest_map & m_new);

void calculate_restricted_change_set(app_state & app, 
                                     std::vector<utf8> const & args,
                                     change_set const & cs,
                                     change_set & included,
                                     change_set & excluded);

#endif  // header guard
