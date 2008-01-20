#ifndef __MERGE_HH__
#define __MERGE_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"

class database;
class lua_hooks;
class roster_t;

// Destructively alter a roster_merge_result to attempt to remove any
// conflicts in it. Takes a content_merge_adaptor to pass on to the content
// merger; used from both the merge-to-database code (below) and the
// merge-to-workspace "update" code in commands.cc.

struct roster_merge_result;
struct content_merge_adaptor;

void
resolve_merge_conflicts(roster_t const & left_roster,
                        roster_t const & right_roster,
                        roster_merge_result & result,
                        content_merge_adaptor & adaptor,
                        lua_hooks & lua);

// traditional resolve-all-conflicts-as-you-go style merging with 3-way merge
//   for file texts
// throws if merge fails
// writes out resulting revision to the db, along with author and date certs
//   (but _not_ branch or changelog certs)
// this version can only be used to merge revisions that are in the db, and
//   that are written straight back out to the db; some refactoring would
//   probably be good
// 'update' requires some slightly different interface, to deal with the gunk
//   around the revision and its files not being in the db, and the resulting
//   revision and its merged files not being written back to the db
void
interactive_merge_and_store(revision_id const & left, revision_id const & right,
                            revision_id & merged, database & db, lua_hooks & lua);

void
store_roster_merge_result(roster_t const & left_roster,
                          roster_t const & right_roster,
                          roster_merge_result & result,
                          revision_id const & left_rid,
                          revision_id const & right_rid,
                          revision_id & merged_rid,
                          database & db);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
