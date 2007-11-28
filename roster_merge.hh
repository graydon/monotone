#ifndef __ROSTER_MERGE_HH__
#define __ROSTER_MERGE_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "vocab.hh"
#include "roster.hh"

// our general strategy is to return a (possibly insane) roster, and a list of
// conflicts encountered in that roster.  Each conflict encountered in merging
// the roster creates an entry in this list.

// nodes with divergent name conflicts are left detached in the resulting
// roster, with null parent and name fields.
// note that it is possible that the parent node on the left, the right, or
// both, no longer exist in the merged roster.  also note that it is possible
// that on one or both sides, they do exist, but already have an entry with
// the given name.
struct divergent_name_conflict
{
  node_id nid;
  divergent_name_conflict(node_id nid) : nid(nid) {}
  std::pair<node_id, path_component> left, right;
};

// files with content conflicts are left attached in resulting tree (unless
// detached for some other reason), but with a null content hash.
struct file_content_conflict
{
  node_id nid;
  file_content_conflict(node_id nid) : nid(nid) {}
  file_id left, right;
};

// nodes with attrs conflicts are left attached in the resulting tree (unless
// detached for some other reason), but with the given attribute left out of
// their full_attr_map_t.  Note that this doesn't actually leave the resulting
// roster insane (FIXME: we could put an invalid attr value in instead, like a
// pair (false, "foo") (since the second value can only be non-null if the
// first is 'true').  Should we do this?)
struct node_attr_conflict
{
  node_id nid;
  node_attr_conflict(node_id nid) : nid(nid) {}
  attr_key key;
  std::pair<bool, attr_value> left, right;
};

// interactions between conflict types:
//   node rename conflicts never participate in structural conflicts
//     (e.g., merge <rename a foo; rename b bar>, <rename a bar> could be
//     considered to have two conflicts -- 'a' being renamed to both 'foo' and
//     'bar', and 'a' and 'b' both being renamed to 'bar'.  Only the former
//     occurs; 'b' merges cleanly and will be named 'bar' in the resulting
//     manifest.)
//

// orphaned nodes always merged their name cleanly, so we simply put that name
// here.  the node in the resulting roster is detached.
struct orphaned_node_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// this is when two distinct nodes want to have the same name.  these nodes
// always each merged their names cleanly.  the nodes in the resulting roster
// are both detached.
// only two nodes are possible, because we
//   -- only merge two rosters at a time
//   -- merge (parent, basename) as a single scalar.  If we merged them
//      separately, then it would be possible to have one side of a merge
//      rename a bunch of files in different directories to all have the same
//      basename, and the other side of the merge to move them all into the
//      same directory.
// a clean *-merge of a scalar always takes on the value of one parent or
// another, and the requirement here is that each node have a unique (parent,
// basename) tuple, and since our requirement matches our *-merge scalar,
// we're okay.
struct convergent_name_conflict
{
  node_id left_nid, right_nid;
  std::pair<node_id, path_component> parent_name;
};

struct directory_loop_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

// renaming the root dir allows these:
//   -- _MTN in root
//   -- missing root directory

// this is a node that cleanly merged to some name, but that name was somehow
// forbidden.  (Currently, the only forbidden name is "_MTN" in the root
// directory.)
struct illegal_name_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name;
};

struct roster_merge_result
{
  // three main types of conflicts
  // - content conflicts
  // - attribute conflicts
  // - tree layout conflicts  (which have the following subtypes)
  //   - convergent name conflicts
  //   - divergent name conflicts
  //   - orphaned node conflicts
  //   - directory loop conflicts
  //   - illegal name conflicts
  //   - missing root conflicts

  std::vector<divergent_name_conflict> divergent_name_conflicts;
  std::vector<file_content_conflict> file_content_conflicts;
  std::vector<node_attr_conflict> node_attr_conflicts;
  std::vector<orphaned_node_conflict> orphaned_node_conflicts;
  std::vector<convergent_name_conflict> convergent_name_conflicts;
  std::vector<directory_loop_conflict> directory_loop_conflicts;
  std::vector<illegal_name_conflict> illegal_name_conflicts;
  bool missing_root_dir;
  // this roster is sane if is_clean() returns true
  roster_t roster;
  bool is_clean() const;
  bool has_content_conflicts() const;
  bool has_non_content_conflicts() const;
  void log_conflicts() const;
  void warn_non_content_conflicts(roster_t const & left,
                                  roster_t const & right) const;
  void clear();
};

template <> void dump(roster_merge_result const & result, std::string & out);

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             std::set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             std::set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
