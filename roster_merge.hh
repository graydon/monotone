#ifndef __ROSTER_MERGE_HH__
#define __ROSTER_MERGE_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>

#include "rev_types.hh"
#include "database.hh"
#include "diff_patch.hh"
#include "roster.hh" // needs full definition of roster_t available

// interactions between conflict types:
//   node rename conflicts never participate in structural conflicts
//     (e.g., merge <rename a foo; rename b bar>, <rename a bar> could be
//     considered to have two conflicts -- 'a' being renamed to both 'foo' and
//     'bar', and 'a' and 'b' both being renamed to 'bar'.  Only the former
//     occurs; 'b' merges cleanly and will be named 'bar' in the resulting
//     manifest.)
//

namespace resolve_conflicts
{
  enum resolution_t {none, content_user, content_internal, rename, drop};

  typedef std::pair<resolve_conflicts::resolution_t, boost::shared_ptr<any_path> > file_resolution_t;

  boost::shared_ptr<any_path> new_file_path(std::string path);

}

// renaming the root dir allows these:
//   -- _MTN in root
//   -- missing root directory

// this is a node that cleanly merged to some name, but that name was somehow
// forbidden.  (Currently, the only forbidden name is "_MTN" in the root
// directory.)
struct invalid_name_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name; // renamed from (if any)
};

struct directory_loop_conflict
{
  node_id nid;
  std::pair<node_id, path_component> parent_name; // renamed from (if any)
};

// orphaned nodes always merged their name cleanly, so we simply put that name
// here.  the node in the resulting roster is detached.
struct orphaned_node_conflict
{
  node_id nid;
  // nid is the orphaned node; it exists in one parent, but the directory
  // containing it does not exist in the other.

  std::pair<node_id, path_component> parent_name;
  // parent_name is the name of the orphaned node, in the parent revision
  // where it exists. parent_name.first is the directory containing
  // parent_name.second
};

// our general strategy is to return a (possibly insane) roster, and a list of
// conflicts encountered in that roster.  Each conflict encountered in merging
// the roster creates an entry in this list.

// nodes with multiple name conflicts are left detached in the resulting
// roster, with null parent and name fields.
// note that it is possible that the parent node on the left, the right, or
// both, no longer exist in the merged roster.  also note that it is possible
// that on one or both sides, they do exist, but already have an entry with
// the given name.
struct multiple_name_conflict
{
  node_id nid;
  multiple_name_conflict(node_id nid) : nid(nid) {}
  std::pair<node_id, path_component> left, right;
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
struct duplicate_name_conflict
{
  node_id left_nid, right_nid;
  std::pair<node_id, path_component> parent_name;
  // file part of resolution must be a file_path if resolution is 'rename';
  // it may be a bookkeeping or system path if resolution is 'user'.
  resolve_conflicts::file_resolution_t left_resolution, right_resolution;

  duplicate_name_conflict ()
  {left_resolution.first = resolve_conflicts::none;
    right_resolution.first = resolve_conflicts::none;};
};

// nodes with attribute conflicts are left attached in the resulting tree (unless
// detached for some other reason), but with the given attribute left out of
// their full_attr_map_t.  Note that this doesn't actually leave the resulting
// roster insane (FIXME: we could put an invalid attr value in instead, like a
// pair (false, "foo") (since the second value can only be non-null if the
// first is 'true').  Should we do this?)
struct attribute_conflict
{
  node_id nid;
  attribute_conflict(node_id nid) : nid(nid) {}
  attr_key key; // attr_name?
  std::pair<bool, attr_value> left, right;
};

// files with content conflicts are left attached in resulting tree (unless
// detached for some other reason), but with a null content hash.
struct file_content_conflict
{
  node_id nid;
  file_id left, right;
  resolve_conflicts::file_resolution_t resolution;

  file_content_conflict () :
    nid(the_null_node)
    {resolution.first = resolve_conflicts::none;};

  file_content_conflict(node_id nid) :
    nid(nid) {resolution.first = resolve_conflicts::none;};
};

template <> void dump(invalid_name_conflict const & conflict, std::string & out);
template <> void dump(directory_loop_conflict const & conflict, std::string & out);

template <> void dump(orphaned_node_conflict const & conflict, std::string & out);
template <> void dump(multiple_name_conflict const & conflict, std::string & out);
template <> void dump(duplicate_name_conflict const & conflict, std::string & out);

template <> void dump(attribute_conflict const & conflict, std::string & out);
template <> void dump(file_content_conflict const & conflict, std::string & out);

struct roster_merge_result
{
  // three main types of conflicts
  // - structural conflicts  (which have the following subtypes)
  //   - missing root directory
  //   - invalid name conflicts
  //   - duplicate name conflicts
  //   - orphaned node conflicts
  //   - multiple name conflicts
  //   - directory loop conflicts
  // - attribute conflicts
  // - file content conflicts

  bool missing_root_conflict;
  std::vector<invalid_name_conflict> invalid_name_conflicts;
  std::vector<directory_loop_conflict> directory_loop_conflicts;

  std::vector<orphaned_node_conflict> orphaned_node_conflicts;
  std::vector<multiple_name_conflict> multiple_name_conflicts;
  std::vector<duplicate_name_conflict> duplicate_name_conflicts;

  std::vector<attribute_conflict> attribute_conflicts;
  std::vector<file_content_conflict> file_content_conflicts;


  // this roster is sane if is_clean() returns true
  roster_t roster;
  bool is_clean() const;
  bool has_content_conflicts() const;
  bool has_non_content_conflicts() const;
  int  count_unsupported_resolution() const;
  void log_conflicts() const;

  void report_missing_root_conflicts(roster_t const & left,
                                     roster_t const & right,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output) const;
  void report_invalid_name_conflicts(roster_t const & left,
                                     roster_t const & right,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output) const;
  void report_directory_loop_conflicts(roster_t const & left,
                                       roster_t const & right,
                                       content_merge_adaptor & adaptor,
                                       bool const basic_io,
                                       std::ostream & output) const;

  void report_orphaned_node_conflicts(roster_t const & left,
                                      roster_t const & right,
                                      content_merge_adaptor & adaptor,
                                      bool const basic_io,
                                      std::ostream & output) const;
  void report_multiple_name_conflicts(roster_t const & left,
                                      roster_t const & right,
                                      content_merge_adaptor & adaptor,
                                      bool const basic_io,
                                      std::ostream & output) const;

  void report_duplicate_name_conflicts(roster_t const & left,
                                       roster_t const & right,
                                       content_merge_adaptor & adaptor,
                                       bool const basic_io,
                                       std::ostream & output) const;
  void resolve_duplicate_name_conflicts(lua_hooks & lua,
                                        roster_t const & left_roster,
                                        roster_t const & right_roster,
                                        content_merge_adaptor & adaptor);

  void report_attribute_conflicts(roster_t const & left,
                                  roster_t const & right,
                                  content_merge_adaptor & adaptor,
                                  bool const basic_io,
                                  std::ostream & output) const;

  // not 'const' because this sets resolution to 'resolved_internal' if the
  // internal merger would succeed.
  void report_file_content_conflicts(lua_hooks & lua,
                                     roster_t const & left_roster,
                                     roster_t const & right_roster,
                                     content_merge_adaptor & adaptor,
                                     bool const basic_io,
                                     std::ostream & output);
  void resolve_file_content_conflicts(lua_hooks & lua,
                                      roster_t const & left_roster,
                                      roster_t const & right_roster,
                                      content_merge_adaptor & adaptor);
  void clear();

  // Conflict file editing

  // If validate, compare file contents to existing conflicts, and add
  // resolutions. Otherwise just read into conflicts.
  void read_conflict_file(database & db,
                          bookkeeping_path const & file_name,
                          revision_id & ancestor_rid,
                          revision_id & left_rid,
                          revision_id & right_rid,
                          roster_t & left_roster,
                          marking_map & left_marking,
                          roster_t & right_roster,
                          marking_map & r_marking);

  void write_conflict_file(database & db,
                           lua_hooks & lua,
                           bookkeeping_path const & file_name,
                           revision_id const & ancestor_rid,
                           revision_id const & left_rid,
                           revision_id const & right_rid,
                           boost::shared_ptr<roster_t> left_roster,
                           marking_map const & left_marking,
                           boost::shared_ptr<roster_t> right_roster,
                           marking_map const & right_marking);
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

void
parse_resolve_conflicts_opts (options const & opts,
                              roster_t const & left_roster,
                              roster_t const & right_roster,
                              roster_merge_result & result,
                              bool & resolutions_given);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
