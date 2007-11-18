// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <set>

#include "vocab.hh"
#include "roster_merge.hh"
#include "parallel_iter.hh"
#include "safe_map.hh"

using std::make_pair;
using std::pair;
using std::set;
using std::string;

bool
roster_merge_result::is_clean() const
{
  return is_clean_except_for_content()
    && file_content_conflicts.empty();
}

bool
roster_merge_result::is_clean_except_for_content() const
{
  return node_name_conflicts.empty()
    && node_attr_conflicts.empty()
    && orphaned_node_conflicts.empty()
    && rename_target_conflicts.empty()
    && directory_loop_conflicts.empty()
    && illegal_name_conflicts.empty()
    && !missing_root_dir;
}

static void
debug_describe_conflicts(roster_merge_result const & result, string & out)
{
  out = (FL("unclean roster_merge: %d name conflicts, %d content conflicts, %d attr conflicts, "
            "%d orphaned node conflicts, %d rename target conflicts, %d directory loop conflicts\n")
         % result.node_name_conflicts.size()
         % result.file_content_conflicts.size()
         % result.node_attr_conflicts.size()
         % result.orphaned_node_conflicts.size()
         % result.rename_target_conflicts.size()
         % result.directory_loop_conflicts.size())
    .str();

  for (size_t i = 0; i < result.node_name_conflicts.size(); ++i)
    out += (FL("name conflict on node %d: [parent %d, self %s] vs. [parent %d, self %s]")
            % result.node_name_conflicts[i].nid
            % result.node_name_conflicts[i].left.first
            % result.node_name_conflicts[i].left.second
            % result.node_name_conflicts[i].right.first
            % result.node_name_conflicts[i].right.second)
      .str();

  for (size_t i = 0; i < result.file_content_conflicts.size(); ++i)
    out += (FL("content conflict on node %d: [%s] vs. [%s]")
            % result.file_content_conflicts[i].nid
            % result.file_content_conflicts[i].left
            % result.file_content_conflicts[i].right)
      .str();

  for (size_t i = 0; i < result.node_attr_conflicts.size(); ++i)
    out += (FL("attribute conflict on node %d, key %s: [%d, %s] vs. [%d, %s]")
            % result.node_attr_conflicts[i].nid
            % result.node_attr_conflicts[i].key
            % result.node_attr_conflicts[i].left.first
            % result.node_attr_conflicts[i].left.second
            % result.node_attr_conflicts[i].right.first
            % result.node_attr_conflicts[i].right.second)
      .str();

  for (size_t i = 0; i < result.orphaned_node_conflicts.size(); ++i)
    out += (FL("orphaned node conflict on node %d, dead parent %d, name %s")
            % result.orphaned_node_conflicts[i].nid
            % result.orphaned_node_conflicts[i].parent_name.first
            % result.orphaned_node_conflicts[i].parent_name.second)
      .str();

  for (size_t i = 0; i < result.rename_target_conflicts.size(); ++i)
    out += (FL("rename target conflict: nodes %d, %d, both want parent %d, name %s")
            % result.rename_target_conflicts[i].nid1
            % result.rename_target_conflicts[i].nid2
            % result.rename_target_conflicts[i].parent_name.first
            % result.rename_target_conflicts[i].parent_name.second)
      .str();

  for (size_t i = 0; i < result.directory_loop_conflicts.size(); ++i)
    out += (FL("directory loop conflict: node %d, wanted parent %d, name %s")
            % result.directory_loop_conflicts[i].nid
            % result.directory_loop_conflicts[i].parent_name.first
            % result.directory_loop_conflicts[i].parent_name.second)
      .str();

  for (size_t i = 0; i < result.illegal_name_conflicts.size(); ++i)
    out += (FL("illegal name conflict: node %d, wanted parent %d, name %s")
            % result.illegal_name_conflicts[i].nid
            % result.illegal_name_conflicts[i].parent_name.first
            % result.illegal_name_conflicts[i].parent_name.second)
      .str();
}

template <> void
dump(roster_merge_result const & result, string & out)
{
  debug_describe_conflicts(result, out);
  string roster_part;
  dump(result.roster, roster_part);
  out += "\n\n";
  out += roster_part;
}

void
roster_merge_result::log_conflicts() const
{
  string str;
  debug_describe_conflicts(*this, str);
  L(FL("%s") % str);
}

void
roster_merge_result::warn_non_content_conflicts() const
{
  for (size_t i = 0; i < node_name_conflicts.size(); ++i)
    W(F("name conflict on node %d: [parent %d, self %s] vs. [parent %d, self %s]")
      % node_name_conflicts[i].nid
      % node_name_conflicts[i].left.first
      % node_name_conflicts[i].left.second
      % node_name_conflicts[i].right.first
      % node_name_conflicts[i].right.second);

  for (size_t i = 0; i < node_attr_conflicts.size(); ++i)
    W(F("attribute conflict on node %d, key %s: [%d, %s] vs. [%d, %s]")
      % node_attr_conflicts[i].nid
      % node_attr_conflicts[i].key
      % node_attr_conflicts[i].left.first
      % node_attr_conflicts[i].left.second
      % node_attr_conflicts[i].right.first
      % node_attr_conflicts[i].right.second);

  for (size_t i = 0; i < orphaned_node_conflicts.size(); ++i)
    W(F("orphaned node conflict on node %d, dead parent %d, name %s")
      % orphaned_node_conflicts[i].nid
      % orphaned_node_conflicts[i].parent_name.first
      % orphaned_node_conflicts[i].parent_name.second);

  for (size_t i = 0; i < rename_target_conflicts.size(); ++i)
    W(F("rename target conflict: nodes %d, %d, both want parent %d, name %s")
      % rename_target_conflicts[i].nid1
      % rename_target_conflicts[i].nid2
      % rename_target_conflicts[i].parent_name.first
      % rename_target_conflicts[i].parent_name.second);

  for (size_t i = 0; i < directory_loop_conflicts.size(); ++i)
    W(F("directory loop conflict: node %d, wanted parent %d, name %s")
      % directory_loop_conflicts[i].nid
      % directory_loop_conflicts[i].parent_name.first
      % directory_loop_conflicts[i].parent_name.second);

  for (size_t i = 0; i < illegal_name_conflicts.size(); ++i)
    W(F("illegal name conflict: node %d, wanted parent %d, name %s")
      % illegal_name_conflicts[i].nid
      % illegal_name_conflicts[i].parent_name.first
      % illegal_name_conflicts[i].parent_name.second);
}

void
roster_merge_result::clear()
{
  node_name_conflicts.clear();
  file_content_conflicts.clear();
  node_attr_conflicts.clear();
  orphaned_node_conflicts.clear();
  rename_target_conflicts.clear();
  directory_loop_conflicts.clear();
  illegal_name_conflicts.clear();
  missing_root_dir = false;
  roster = roster_t();
}

namespace
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(set<revision_id> const & b_marks,
         set<revision_id> const & b_uncommon_ancestors)
  {
    for (set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               set<revision_id> const & left_marks,
               set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               set<revision_id> const & right_marks,
               set<revision_id> const & right_uncommon_ancestors,
               T & result,
               C & conflict_descriptor)
  {
    if (left == right)
      {
        result = left;
        return true;
      }
    MM(left_marks);
    MM(left_uncommon_ancestors);
    MM(right_marks);
    MM(right_uncommon_ancestors);
    bool left_wins = a_wins(right_marks, right_uncommon_ancestors);
    bool right_wins = a_wins(left_marks, left_uncommon_ancestors);
    // two bools means 4 cases:
    //   left_wins && right_wins
    //     this is ambiguous clean merge, which is theoretically impossible.
    I(!(left_wins && right_wins));
    //   left_wins && !right_wins
    if (left_wins && !right_wins)
      {
        result = left;
        return true;
      }
    //   !left_wins && right_wins
    if (!left_wins && right_wins)
      {
        result = right;
        return true;
      }
    //   !left_wins && !right_wins
    if (!left_wins && !right_wins)
      {
        conflict_descriptor.left = left;
        conflict_descriptor.right = right;
        return false;
      }
    I(false);
  }

  inline void
  create_node_for(node_t const & n, roster_t & new_roster)
  {
    if (is_dir_t(n))
      new_roster.create_dir_node(n->self);
    else if (is_file_t(n))
      new_roster.create_file_node(file_id(), n->self);
    else
      I(false);
  }

  inline void
  insert_if_unborn(node_t const & n,
                   marking_map const & markings,
                   set<revision_id> const & uncommon_ancestors,
                   roster_t const & parent_roster,
                   roster_t & new_roster)
  {
    revision_id const & birth = safe_get(markings, n->self).birth_revision;
    if (uncommon_ancestors.find(birth) != uncommon_ancestors.end())
      create_node_for(n, new_roster);
    else
      {
        // In this branch we are NOT inserting the node into the new roster as it
        // has been deleted from the other side of the merge.
        // In this case, output a warning if there are changes to the file on the
        // side of the merge where it still exists.
        set<revision_id> const & content_marks = safe_get(markings, n->self).file_content;
        bool found_one_ignored_content = false;
        for (set<revision_id>::const_iterator it = content_marks.begin(); it != content_marks.end(); it++)
          {
            if (uncommon_ancestors.find(*it) != uncommon_ancestors.end())
              {
                if (!found_one_ignored_content)
                  {
                    file_path fp;
                    parent_roster.get_name(n->self, fp);
                    W(F("Content changes to the file \"%s\"") % fp);
                    W(F("will be ignored during this merge as the file has been"));
                    W(F("removed on one side of the merge.  Affected revisions include:"));
                  }
                found_one_ignored_content = true;
                W(F("Revision : %s") % *it);
              }
          }
      }
  }

  bool
  would_make_dir_loop(roster_t const & r, node_id nid, node_id parent)
  {
    // parent may not be fully attached yet; that's okay.  that just means
    // we'll run into a node with a null parent somewhere before we hit the
    // actual root; whether we hit the actual root or not, hitting a node
    // with a null parent will tell us that this particular attachment won't
    // create a loop.
    for (node_id curr = parent; !null_node(curr); curr = r.get_node(curr)->parent)
      {
        if (curr == nid)
          return true;
      }
    return false;
  }

  void
  assign_name(roster_merge_result & result, node_id nid,
              node_id parent, path_component name)
  {
    // this function is reponsible for detecting structural conflicts.  by the
    // time we've gotten here, we have a node that's unambiguously decided on
    // a name; but it might be that that name does not exist (because the
    // parent dir is gone), or that it's already taken (by another node), or
    // that putting this node there would create a directory loop.  In all
    // such cases, rather than actually attach the node, we write a conflict
    // structure and leave it detached.

    // the root dir is somewhat special.  it can't be orphaned, and it can't
    // make a dir loop.  it can, however, have a name collision.
    if (null_node(parent))
      {
        I(name.empty());
        if (result.roster.has_root())
          {
            // see comments below about name collisions.
            rename_target_conflict c;
            c.nid1 = nid;
            c.nid2 = result.roster.root()->self;
            c.parent_name = make_pair(parent, name);
            result.roster.detach_node(file_path());
            result.rename_target_conflicts.push_back(c);
            return;
          }
      }
    else
      {
        // orphan:
        if (!result.roster.has_node(parent))
          {
            orphaned_node_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.orphaned_node_conflicts.push_back(c);
            return;
          }

        dir_t p = downcast_to_dir_t(result.roster.get_node(parent));

        // name conflict:
        // see the comment in roster_merge.hh for the analysis showing that at
        // most two nodes can participate in a rename target conflict.  this code
        // exploits that; after this code runs, there will be no node at the given
        // location in the tree, which means that in principle, if there were a
        // third node that _also_ wanted to go here, when we got around to
        // attaching it we'd have no way to realize it should be a conflict.  but
        // that never happens, so we don't have to keep a lookaside set of
        // "poisoned locations" or anything.
        if (p->has_child(name))
          {
            rename_target_conflict c;
            c.nid1 = nid;
            c.nid2 = p->get_child(name)->self;
            c.parent_name = make_pair(parent, name);
            p->detach_child(name);
            result.rename_target_conflicts.push_back(c);
            return;
          }

        if (would_make_dir_loop(result.roster, nid, parent))
          {
            directory_loop_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.directory_loop_conflicts.push_back(c);
            return;
          }
      }
    // hey, we actually made it.  attach the node!
    result.roster.attach_node(nid, parent, name);
  }

  void
  copy_node_forward(roster_merge_result & result, node_t const & n,
                    node_t const & old_n)
  {
    I(n->self == old_n->self);
    n->attrs = old_n->attrs;
    if (is_file_t(n))
      downcast_to_file_t(n)->content = downcast_to_file_t(old_n)->content;
    assign_name(result, n->self, old_n->parent, old_n->name);
  }

} // end anonymous namespace

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result)
{
  L(FL("Performing a roster_merge"));

  result.clear();
  MM(left_parent);
  MM(left_markings);
  MM(right_parent);
  MM(right_markings);
  MM(result);

  // First handle lifecycles, by die-die-die merge -- our result will contain
  // everything that is alive in both parents, or alive in one and unborn in
  // the other, exactly.
  {
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            insert_if_unborn(i.left_data(),
                             left_markings, left_uncommon_ancestors, left_parent,
                             result.roster);
            break;

          case parallel::in_right:
            insert_if_unborn(i.right_data(),
                             right_markings, right_uncommon_ancestors, right_parent,
                             result.roster);
            break;

          case parallel::in_both:
            create_node_for(i.left_data(), result.roster);
            break;
          }
      }
  }

  // okay, our roster now contains a bunch of empty, detached nodes.  fill
  // them in one at a time with *-merge.
  {
    node_map::const_iterator left_i, right_i;
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    node_map::const_iterator new_i = result.roster.all_nodes().begin();
    marking_map::const_iterator left_mi = left_markings.begin();
    marking_map::const_iterator right_mi = right_markings.begin();
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            {
              node_t const & left_n = i.left_data();
              // we skip nodes that aren't in the result roster (were
              // deleted in the lifecycles step above)
              if (result.roster.has_node(left_n->self))
                {
                  copy_node_forward(result, new_i->second, left_n);
                  ++new_i;
                }
              ++left_mi;
              break;
            }

          case parallel::in_right:
            {
              node_t const & right_n = i.right_data();
              // we skip nodes that aren't in the result roster
              if (result.roster.has_node(right_n->self))
                {
                  copy_node_forward(result, new_i->second, right_n);
                  ++new_i;
                }
              ++right_mi;
              break;
            }

          case parallel::in_both:
            {
              I(new_i->first == i.left_key());
              I(left_mi->first == i.left_key());
              I(right_mi->first == i.right_key());
              node_t const & left_n = i.left_data();
              marking_t const & left_marking = left_mi->second;
              node_t const & right_n = i.right_data();
              marking_t const & right_marking = right_mi->second;
              node_t const & new_n = new_i->second;
              // merge name
              {
                pair<node_id, path_component> new_name;
                node_name_conflict conflict(new_n->self);
                if (merge_scalar(make_pair(left_n->parent, left_n->name),
                                 left_marking.parent_name,
                                 left_uncommon_ancestors,
                                 make_pair(right_n->parent, right_n->name),
                                 right_marking.parent_name,
                                 right_uncommon_ancestors,
                                 new_name, conflict))
                  {
                    assign_name(result, new_n->self,
                                new_name.first, new_name.second);
                  }
                else
                  {
                    // unsuccessful merge; leave node detached and save
                    // conflict object
                    result.node_name_conflicts.push_back(conflict);
                  }
              }
              // if a file, merge content
              if (is_file_t(new_n))
                {
                  file_content_conflict conflict(new_n->self);
                  if (merge_scalar(downcast_to_file_t(left_n)->content,
                                   left_marking.file_content,
                                   left_uncommon_ancestors,
                                   downcast_to_file_t(right_n)->content,
                                   right_marking.file_content,
                                   right_uncommon_ancestors,
                                   downcast_to_file_t(new_n)->content,
                                   conflict))
                    {
                      // successful merge
                    }
                  else
                    {
                      downcast_to_file_t(new_n)->content = file_id();
                      result.file_content_conflicts.push_back(conflict);
                    }
                }
              // merge attributes
              {
                full_attr_map_t::const_iterator left_ai = left_n->attrs.begin();
                full_attr_map_t::const_iterator right_ai = right_n->attrs.begin();
                parallel::iter<full_attr_map_t> attr_i(left_n->attrs,
                                                       right_n->attrs);
                while(attr_i.next())
                {
                  switch (attr_i.state())
                    {
                    case parallel::invalid:
                      I(false);
                    case parallel::in_left:
                      safe_insert(new_n->attrs, attr_i.left_value());
                      break;
                    case parallel::in_right:
                      safe_insert(new_n->attrs, attr_i.right_value());
                      break;
                    case parallel::in_both:
                      pair<bool, attr_value> new_value;
                      node_attr_conflict conflict(new_n->self);
                      conflict.key = attr_i.left_key();
                      I(conflict.key == attr_i.right_key());
                      if (merge_scalar(attr_i.left_data(),
                                       safe_get(left_marking.attrs,
                                                attr_i.left_key()),
                                       left_uncommon_ancestors,
                                       attr_i.right_data(),
                                       safe_get(right_marking.attrs,
                                                attr_i.right_key()),
                                       right_uncommon_ancestors,
                                       new_value,
                                       conflict))
                        {
                          // successful merge
                          safe_insert(new_n->attrs,
                                      make_pair(attr_i.left_key(),
                                                     new_value));
                        }
                      else
                        {
                          // unsuccessful merge
                          // leave out the attr entry entirely, and save the
                          // conflict
                          result.node_attr_conflicts.push_back(conflict);
                        }
                      break;
                    }

                }
              }
            }
            ++left_mi;
            ++right_mi;
            ++new_i;
            break;
          }
      }
    I(left_mi == left_markings.end());
    I(right_mi == right_markings.end());
    I(new_i == result.roster.all_nodes().end());
  }

  // now check for the possible global problems
  if (!result.roster.has_root())
    result.missing_root_dir = true;
  else
    {
      // we can't have an illegal _MTN dir unless we have a root node in the
      // first place...
      dir_t result_root = result.roster.root();

      if (result_root->has_child(bookkeeping_root_component))
        {
          illegal_name_conflict conflict;
          node_t n = result_root->get_child(bookkeeping_root_component);
          conflict.nid = n->self;
          conflict.parent_name.first = n->parent;
          conflict.parent_name.second = n->name;
          I(n->name == bookkeeping_root_component);

          result.roster.detach_node(n->self);
          result.illegal_name_conflicts.push_back(conflict);
        }
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

#include "roster_delta.hh"

// cases for testing:
//
// (DONE:)
//
// lifecycle, file and dir
//    alive in both
//    alive in one and unborn in other (left vs. right)
//    alive in one and dead in other (left vs. right)
//
// mark merge:
//   same in both, same mark
//   same in both, diff marks
//   different, left wins with 1 mark
//   different, right wins with 1 mark
//   different, conflict with 1 mark
//   different, left wins with 2 marks
//   different, right wins with 2 marks
//   different, conflict with 1 mark winning, 1 mark losing
//   different, conflict with 2 marks both conflicting
//
// for:
//   node name and parent, file and dir
//   node attr, file and dir
//   file content
//
// attr lifecycle:
//   seen in both -->mark merge cases, above
//   live in one and unseen in other -->live
//   dead in one and unseen in other -->dead
//
// two diff nodes with same name
// directory loops
// orphans
// illegal node ("_MTN")
// missing root dir
//
// (NEEDED:)
//
// interactions:
//   in-node name conflict prevents other problems:
//     in-node name conflict + possible between-node name conflict
//        a vs. b, plus a, b, exist in result
//        left: 1: a
//              2: b
//        right: 1: b
//               3: a
//     in-node name conflict + both possible names orphaned
//        a/foo vs. b/foo conflict, + a, b exist in parents but deleted in
//        children
//        left: 1: a
//              2: a/foo
//        right:
//              3: b
//              2: b/foo
//     in-node name conflict + directory loop conflict
//        a/bottom vs. b/bottom, with a and b both moved inside it
//     in-node name conflict + one name illegal
//        _MTN vs. foo
//   in-node name conflict causes other problems:
//     in-node name conflict + causes missing root dir
//        "" vs. foo and bar vs. ""
//   between-node name conflict prevents other problems:
//     between-node name conflict + both nodes orphaned
//        this is not possible
//     between-node name conflict + both nodes cause loop
//        this is not possible
//     between-node name conflict + both nodes illegal
//        two nodes that both merge to _MTN
//        this is not possible
//   between-node name conflict causes other problems:
//     between-node name conflict + causes missing root dir
//        two nodes that both want ""

typedef enum { scalar_a, scalar_b, scalar_conflict } scalar_val;

template <> void
dump(scalar_val const & v, string & out)
{
  switch (v)
    {
    case scalar_a:
      out = "scalar_a";
      break;
    case scalar_b:
      out = "scalar_b";
      break;
    case scalar_conflict:
      out = "scalar_conflict";
      break;
    }
}

void string_to_set(string const & from, set<revision_id> & to)
{
  to.clear();
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      string rid_str(40, *i);
      to.insert(revision_id(rid_str));
    }
}


template <typename S> void
test_a_scalar_merge_impl(scalar_val left_val, string const & left_marks_str,
                         string const & left_uncommon_str,
                         scalar_val right_val, string const & right_marks_str,
                         string const & right_uncommon_str,
                         scalar_val expected_outcome)
{
  MM(left_val);
  MM(left_marks_str);
  MM(left_uncommon_str);
  MM(right_val);
  MM(right_marks_str);
  MM(right_uncommon_str);
  MM(expected_outcome);

  S scalar;
  roster_t left_parent, right_parent;
  marking_map left_markings, right_markings;
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  roster_merge_result result;

  set<revision_id> left_marks, right_marks;

  MM(left_parent);
  MM(right_parent);
  MM(left_markings);
  MM(right_markings);
  MM(left_uncommon_ancestors);
  MM(right_uncommon_ancestors);
  MM(left_marks);
  MM(right_marks);
  MM(result);

  string_to_set(left_marks_str, left_marks);
  scalar.setup_parent(left_val, left_marks, left_parent, left_markings);
  string_to_set(right_marks_str, right_marks);
  scalar.setup_parent(right_val, right_marks, right_parent, right_markings);

  string_to_set(left_uncommon_str, left_uncommon_ancestors);
  string_to_set(right_uncommon_str, right_uncommon_ancestors);

  roster_merge(left_parent, left_markings, left_uncommon_ancestors,
               right_parent, right_markings, right_uncommon_ancestors,
               result);

  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_parent, left_markings, right_parent, right_markings);

  scalar.check_result(left_val, right_val, result, expected_outcome);
}

static const revision_id root_rid = revision_id(string("0000000000000000000000000000000000000000"));
static const file_id arbitrary_file = file_id(string("0000000000000000000000000000000000000000"));

struct base_scalar
{
  testing_node_id_source nis;
  node_id root_nid;
  node_id thing_nid;
  base_scalar() : root_nid(nis.next()), thing_nid(nis.next())
  {}

  void
  make_dir(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_dir_node(nid);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_file(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_file_node(arbitrary_file, nid);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    marking.file_content.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_root(roster_t & r, marking_map & markings)
  {
    make_dir("", root_nid, r, markings);
  }
};

struct file_scalar : public virtual base_scalar
{
  file_path thing_name;
  file_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_file("thing", thing_nid, r, markings);
  }
};

struct dir_scalar : public virtual base_scalar
{
  file_path thing_name;
  dir_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_dir("thing", thing_nid, r, markings);
  }
};

struct name_shared_stuff : public virtual base_scalar
{
  virtual file_path path_for(scalar_val val) = 0;
  path_component pc_for(scalar_val val)
  {
    return path_for(val).basename();
  }
  virtual node_id parent_for(scalar_val val) = 0;

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        {
          file_path fp;
          result.roster.get_name(thing_nid, fp);
          I(fp == path_for(expected_val));
        }
        break;
      case scalar_conflict:
        node_name_conflict const & c = idx(result.node_name_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.left == make_pair(parent_for(left_val), pc_for(left_val)));
        I(c.right == make_pair(parent_for(right_val), pc_for(right_val)));
        I(null_node(result.roster.get_node(thing_nid)->parent));
        I(result.roster.get_node(thing_nid)->name.empty());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        // attach implicitly checks that we were already detached
        result.roster.attach_node(thing_nid, file_path_internal("thing"));
        result.node_name_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }

  virtual ~name_shared_stuff() {};
};

template <typename T>
struct basename_scalar : public name_shared_stuff, public T
{
  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a" : "b");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return root_nid;
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~basename_scalar() {}
};

template <typename T>
struct parent_scalar : public virtual name_shared_stuff, public T
{
  node_id a_dir_nid, b_dir_nid;
  parent_scalar() : a_dir_nid(nis.next()), b_dir_nid(nis.next())
  {}

  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a/thing" : "b/thing");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return ((val == scalar_a) ? a_dir_nid : b_dir_nid);
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    make_dir("a", a_dir_nid, r, markings);
    make_dir("b", b_dir_nid, r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~parent_scalar() {}
};

template <typename T>
struct attr_scalar : public virtual base_scalar, public T
{
  attr_value attr_value_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return attr_value((val == scalar_a) ? "a" : "b");
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.set_attr(this->T::thing_name, attr_key("test_key"), attr_value_for(val));
    markings.find(thing_nid)->second.attrs[attr_key("test_key")] = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(result.roster.get_node(thing_nid)->attrs[attr_key("test_key")]
          == make_pair(true, attr_value_for(expected_val)));
        break;
      case scalar_conflict:
        node_attr_conflict const & c = idx(result.node_attr_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.key == attr_key("test_key"));
        I(c.left == make_pair(true, attr_value_for(left_val)));
        I(c.right == make_pair(true, attr_value_for(right_val)));
        full_attr_map_t const & attrs = result.roster.get_node(thing_nid)->attrs;
        I(attrs.find(attr_key("test_key")) == attrs.end());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        result.roster.set_attr(this->T::thing_name, attr_key("test_key"),
                               attr_value("conflict -- RESOLVED"));
        result.node_attr_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

struct file_content_scalar : public virtual file_scalar
{
  file_id content_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_id(string((val == scalar_a)
                               ? "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                               : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    make_thing(r, markings);
    downcast_to_file_t(r.get_node(thing_name))->content = content_for(val);
    markings.find(thing_nid)->second.file_content = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(downcast_to_file_t(result.roster.get_node(thing_nid))->content
          == content_for(expected_val));
        break;
      case scalar_conflict:
        file_content_conflict const & c = idx(result.file_content_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.left == content_for(left_val));
        I(c.right == content_for(right_val));
        file_id & content = downcast_to_file_t(result.roster.get_node(thing_nid))->content;
        I(null_id(content));
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        content = file_id(string("ffffffffffffffffffffffffffffffffffffffff"));
        result.file_content_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

void
test_a_scalar_merge(scalar_val left_val, string const & left_marks_str,
                    string const & left_uncommon_str,
                    scalar_val right_val, string const & right_marks_str,
                    string const & right_uncommon_str,
                    scalar_val expected_outcome)
{
  test_a_scalar_merge_impl<basename_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                          right_val, right_marks_str, right_uncommon_str,
                                                          expected_outcome);
  test_a_scalar_merge_impl<basename_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                         right_val, right_marks_str, right_uncommon_str,
                                                         expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                        right_val, right_marks_str, right_uncommon_str,
                                                        expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                       right_val, right_marks_str, right_uncommon_str,
                                                       expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                      right_val, right_marks_str, right_uncommon_str,
                                                      expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                     right_val, right_marks_str, right_uncommon_str,
                                                     expected_outcome);
  test_a_scalar_merge_impl<file_content_scalar>(left_val, left_marks_str, left_uncommon_str,
                                                right_val, right_marks_str, right_uncommon_str,
                                                expected_outcome);
}

UNIT_TEST(roster_merge, scalar_merges)
{
  // Notation: a1* means, "value is a, this is node 1 in the graph, it is
  // marked".  ".2" means, "value is unimportant and different from either a
  // or b, this is node 2 in the graph, it is not marked".
  //
  // Backslashes with dots after them mean, the C++ line continuation rules
  // are annoying when it comes to drawing ascii graphs -- the dot is only to
  // stop the backslash from having special meaning to the parser.  So just
  // ignore them :-).

  //   same in both, same mark
  //               a1*
  //              / \.
  //             a2  a3
  test_a_scalar_merge(scalar_a, "1", "2", scalar_a, "1", "3", scalar_a);

  //   same in both, diff marks
  //               .1*
  //              / \.
  //             a2* a3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_a, "3", "3", scalar_a);

  //   different, left wins with 1 mark
  //               a1*
  //              / \.
  //             b2* a3
  test_a_scalar_merge(scalar_b, "2", "2", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 1 mark
  //               a1*
  //              / \.
  //             a2  b3*
   test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "3", "3", scalar_b);

  //   different, conflict with 1 mark
  //               .1*
  //              / \.
  //             a2* b3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "3", "3", scalar_conflict);

  //   different, left wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "45", "3456", scalar_b);

  //   different, conflict with 1 mark winning, 1 mark losing
  //               .1*
  //              / \.
  //             a2* a3*
  //              \ / \.
  //               a4  b5*
  test_a_scalar_merge(scalar_a, "23", "24", scalar_b, "5", "5", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* a3*
  //            / \ /
  //           b4* a5
  test_a_scalar_merge(scalar_b, "4", "4", scalar_a, "23", "35", scalar_conflict);

  //   different, conflict with 2 marks both conflicting
  //
  //               .1*
  //              / \.
  //             .2  a3*
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "3", "3", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* .3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "45", "3456", scalar_conflict);

  //
  //               _.1*_
  //              /     \.
  //             .2      .3
  //            / \     / \.
  //           a4* a5* b6* b7*
  //            \ /     \ /
  //             a8      b9
  test_a_scalar_merge(scalar_a, "45", "2458", scalar_b, "67", "3679", scalar_conflict);
}

namespace
{
  const revision_id a_uncommon1 = revision_id(string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  const revision_id a_uncommon2 = revision_id(string("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  const revision_id b_uncommon1 = revision_id(string("cccccccccccccccccccccccccccccccccccccccc"));
  const revision_id b_uncommon2 = revision_id(string("dddddddddddddddddddddddddddddddddddddddd"));
  const revision_id common1 = revision_id(string("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
  const revision_id common2 = revision_id(string("ffffffffffffffffffffffffffffffffffffffff"));

  const file_id fid1 = file_id(string("1111111111111111111111111111111111111111"));
  const file_id fid2 = file_id(string("2222222222222222222222222222222222222222"));

}

static void
make_dir(roster_t & r, marking_map & markings,
         revision_id const & birth_rid, revision_id const & parent_name_rid,
         string const & name, node_id nid)
{
  r.create_dir_node(nid);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_file(roster_t & r, marking_map & markings,
          revision_id const & birth_rid, revision_id const & parent_name_rid,
          revision_id const & file_content_rid,
          string const & name, file_id const & content,
          node_id nid)
{
  r.create_file_node(content, nid);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  marking.file_content.insert(file_content_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_node_lifecycle_objs(roster_t & r, marking_map & markings, revision_id const & uncommon,
                         string const & name, node_id common_dir_nid, node_id common_file_nid,
                         node_id & safe_dir_nid, node_id & safe_file_nid, node_id_source & nis)
{
  make_dir(r, markings, common1, common1, "common_old_dir", common_dir_nid);
  make_file(r, markings, common1, common1, common1, "common_old_file", fid1, common_file_nid);
  safe_dir_nid = nis.next();
  make_dir(r, markings, uncommon, uncommon, name + "_safe_dir", safe_dir_nid);
  safe_file_nid = nis.next();
  make_file(r, markings, uncommon, uncommon, uncommon, name + "_safe_file", fid1, safe_file_nid);
  make_dir(r, markings, common1, common1, name + "_dead_dir", nis.next());
  make_file(r, markings, common1, common1, common1, name + "_dead_file", fid1, nis.next());
}

UNIT_TEST(roster_merge, node_lifecycle)
{
  roster_t a_roster, b_roster;
  marking_map a_markings, b_markings;
  set<revision_id> a_uncommon, b_uncommon;
  // boilerplate to get uncommon revision sets...
  a_uncommon.insert(a_uncommon1);
  a_uncommon.insert(a_uncommon2);
  b_uncommon.insert(b_uncommon1);
  b_uncommon.insert(b_uncommon2);
  testing_node_id_source nis;
  // boilerplate to set up a root node...
  {
    node_id root_nid = nis.next();
    make_dir(a_roster, a_markings, common1, common1, "", root_nid);
    make_dir(b_roster, b_markings, common1, common1, "", root_nid);
  }
  // create some nodes on each side
  node_id common_dir_nid = nis.next();
  node_id common_file_nid = nis.next();
  node_id a_safe_dir_nid, a_safe_file_nid, b_safe_dir_nid, b_safe_file_nid;
  make_node_lifecycle_objs(a_roster, a_markings, a_uncommon1, "a", common_dir_nid, common_file_nid,
                           a_safe_dir_nid, a_safe_file_nid, nis);
  make_node_lifecycle_objs(b_roster, b_markings, b_uncommon1, "b", common_dir_nid, common_file_nid,
                           b_safe_dir_nid, b_safe_file_nid, nis);
  // do the merge
  roster_merge_result result;
  roster_merge(a_roster, a_markings, a_uncommon, b_roster, b_markings, b_uncommon, result);
  I(result.is_clean());
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(a_roster, a_markings, b_roster, b_markings);
  // 7 = 1 root + 2 common + 2 safe a + 2 safe b
  I(result.roster.all_nodes().size() == 7);
  // check that they're the right ones...
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  a_roster.get_node(common_dir_nid), false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  a_roster.get_node(common_file_nid), false));
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  b_roster.get_node(common_dir_nid), false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  b_roster.get_node(common_file_nid), false));
  I(shallow_equal(result.roster.get_node(a_safe_dir_nid),
                  a_roster.get_node(a_safe_dir_nid), false));
  I(shallow_equal(result.roster.get_node(a_safe_file_nid),
                  a_roster.get_node(a_safe_file_nid), false));
  I(shallow_equal(result.roster.get_node(b_safe_dir_nid),
                  b_roster.get_node(b_safe_dir_nid), false));
  I(shallow_equal(result.roster.get_node(b_safe_file_nid),
                  b_roster.get_node(b_safe_file_nid), false));
}

UNIT_TEST(roster_merge, attr_lifecycle)
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  MM(left_roster);
  MM(left_markings);
  MM(right_roster);
  MM(right_markings);
  set<revision_id> old_revs, left_revs, right_revs;
  string_to_set("0", old_revs);
  string_to_set("1", left_revs);
  string_to_set("2", right_revs);
  revision_id old_rid = *old_revs.begin();
  testing_node_id_source nis;
  node_id dir_nid = nis.next();
  make_dir(left_roster, left_markings, old_rid, old_rid, "", dir_nid);
  make_dir(right_roster, right_markings, old_rid, old_rid, "", dir_nid);
  node_id file_nid = nis.next();
  make_file(left_roster, left_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);
  make_file(right_roster, right_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);

  // put one live and one dead attr on each thing on each side, with uncommon
  // marks on them
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_dead"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_dead"), left_revs));

  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_dead"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_dead"), right_revs));

  roster_merge_result result;
  MM(result);
  roster_merge(left_roster, left_markings, left_revs,
               right_roster, right_markings, right_revs,
               result);
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);
  I(result.roster.all_nodes().size() == 2);
  I(result.roster.get_node(dir_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(result.roster.get_node(file_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
}

struct structural_conflict_helper
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  set<revision_id> old_revs, left_revs, right_revs;
  revision_id old_rid, left_rid, right_rid;
  testing_node_id_source nis;
  node_id root_nid;
  roster_merge_result result;

  virtual void setup() = 0;
  virtual void check() = 0;

  void test()
  {
    MM(left_roster);
    MM(left_markings);
    MM(right_roster);
    MM(right_markings);
    string_to_set("0", old_revs);
    string_to_set("1", left_revs);
    string_to_set("2", right_revs);
    old_rid = *old_revs.begin();
    left_rid = *left_revs.begin();
    right_rid = *right_revs.begin();
    root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "", root_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "", root_nid);

    setup();

    MM(result);
    roster_merge(left_roster, left_markings, left_revs,
                 right_roster, right_markings, right_revs,
                 result);
    // go ahead and check the roster_delta code too, while we're at it...
    test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);

    check();
  }

  virtual ~structural_conflict_helper() {}
};

// two diff nodes with same name
struct simple_rename_target_conflict : public structural_conflict_helper
{
  node_id left_nid, right_nid;
  virtual void setup()
  {
    left_nid = nis.next();
    make_dir(left_roster, left_markings, left_rid, left_rid, "thing", left_nid);
    right_nid = nis.next();
    make_dir(right_roster, right_markings, right_rid, right_rid, "thing", right_nid);
  }

  virtual void check()
  {
    I(!result.is_clean());
    rename_target_conflict const & c = idx(result.rename_target_conflicts, 0);
    I((c.nid1 == left_nid && c.nid2 == right_nid)
      || (c.nid1 == right_nid && c.nid2 == left_nid));
    I(c.parent_name == make_pair(root_nid, path_component("thing")));
    // this tests that they were detached, implicitly
    result.roster.attach_node(left_nid, file_path_internal("left"));
    result.roster.attach_node(right_nid, file_path_internal("right"));
    result.rename_target_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

// directory loops
struct simple_dir_loop_conflict : public structural_conflict_helper
{
  node_id left_top_nid, right_top_nid;

  virtual void setup()
    {
      left_top_nid = nis.next();
      right_top_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "top", left_top_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "top/bottom", right_top_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "top", right_top_nid);
      make_dir(right_roster, right_markings, old_rid, right_rid, "top/bottom", left_top_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      directory_loop_conflict const & c = idx(result.directory_loop_conflicts, 0);
      I((c.nid == left_top_nid && c.parent_name == make_pair(right_top_nid, path_component("bottom")))
        || (c.nid == right_top_nid && c.parent_name == make_pair(left_top_nid, path_component("bottom"))));
      // this tests it was detached, implicitly
      result.roster.attach_node(c.nid, file_path_internal("resolved"));
      result.directory_loop_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// orphans
struct simple_orphan_conflict : public structural_conflict_helper
{
  node_id a_dead_parent_nid, a_live_child_nid, b_dead_parent_nid, b_live_child_nid;

  // in ancestor, both parents are alive
  // in left, a_dead_parent is dead, and b_live_child is created
  // in right, b_dead_parent is dead, and a_live_child is created

  virtual void setup()
    {
      a_dead_parent_nid = nis.next();
      a_live_child_nid = nis.next();
      b_dead_parent_nid = nis.next();
      b_live_child_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "b_parent", b_dead_parent_nid);
      make_dir(left_roster, left_markings, left_rid, left_rid, "b_parent/b_child", b_live_child_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "a_parent", a_dead_parent_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "a_parent/a_child", a_live_child_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.orphaned_node_conflicts.size() == 2);
      orphaned_node_conflict a, b;
      if (idx(result.orphaned_node_conflicts, 0).nid == a_live_child_nid)
        {
          a = idx(result.orphaned_node_conflicts, 0);
          b = idx(result.orphaned_node_conflicts, 1);
        }
      else
        {
          a = idx(result.orphaned_node_conflicts, 1);
          b = idx(result.orphaned_node_conflicts, 0);
        }
      I(a.nid == a_live_child_nid);
      I(a.parent_name == make_pair(a_dead_parent_nid, path_component("a_child")));
      I(b.nid == b_live_child_nid);
      I(b.parent_name == make_pair(b_dead_parent_nid, path_component("b_child")));
      // this tests it was detached, implicitly
      result.roster.attach_node(a.nid, file_path_internal("resolved_a"));
      result.roster.attach_node(b.nid, file_path_internal("resolved_b"));
      result.orphaned_node_conflicts.pop_back();
      result.orphaned_node_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// illegal node ("_MTN")
struct simple_illegal_name_conflict : public structural_conflict_helper
{
  node_id new_root_nid, bad_dir_nid;

  // in left, new_root is the root (it existed in old, but was renamed in left)
  // in right, new_root is still a subdir, the old root still exists, and a
  // new dir has been created

  virtual void setup()
    {
      new_root_nid = nis.next();
      bad_dir_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "", new_root_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "root_to_be", new_root_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "root_to_be/_MTN", bad_dir_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      illegal_name_conflict const & c = idx(result.illegal_name_conflicts, 0);
      I(c.nid == bad_dir_nid);
      I(c.parent_name == make_pair(new_root_nid, bookkeeping_root_component));
      // this tests it was detached, implicitly
      result.roster.attach_node(bad_dir_nid, file_path_internal("dir_formerly_known_as__MTN"));
      result.illegal_name_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// missing root dir
struct simple_missing_root_dir : public structural_conflict_helper
{
  node_id other_root_nid;

  // left and right each have different root nodes, and each has deleted the
  // other's root node

  virtual void setup()
    {
      other_root_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, old_rid, "", other_root_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.missing_root_dir);
      result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
      result.missing_root_dir = false;
      I(result.is_clean());
      result.roster.check_sane();
    }
};

UNIT_TEST(roster_merge, simple_structural_conflicts)
{
  {
    simple_rename_target_conflict t;
    t.test();
  }
  {
    simple_dir_loop_conflict t;
    t.test();
  }
  {
    simple_orphan_conflict t;
    t.test();
  }
  {
    simple_illegal_name_conflict t;
    t.test();
  }
  {
    simple_missing_root_dir t;
    t.test();
  }
}

struct node_name_plus_helper : public structural_conflict_helper
{
  node_id name_conflict_nid;
  node_id left_parent, right_parent;
  path_component left_name, right_name;
  void make_nn_conflict(string const & left, string const & right)
  {
    file_path left_path = file_path_internal(left);
    file_path right_path = file_path_internal(right);
    name_conflict_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, left, name_conflict_nid);
    left_parent = left_roster.get_node(left_path)->parent;
    left_name = left_roster.get_node(left_path)->name;
    make_dir(right_roster, right_markings, old_rid, right_rid, right, name_conflict_nid);
    right_parent = right_roster.get_node(right_path)->parent;
    right_name = right_roster.get_node(right_path)->name;
  }
  void check_nn_conflict()
  {
    I(!result.is_clean());
    node_name_conflict const & c = idx(result.node_name_conflicts, 0);
    I(c.nid == name_conflict_nid);
    I(c.left == make_pair(left_parent, left_name));
    I(c.right == make_pair(right_parent, right_name));
    result.roster.attach_node(name_conflict_nid, file_path_internal("totally_other_name"));
    result.node_name_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct node_name_plus_rename_target : public node_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_nn_conflict("a", "b");
    make_dir(left_roster, left_markings, left_rid, left_rid, "b", b_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "a", a_nid);
  }

  virtual void check()
  {
    // there should just be a single conflict on name_conflict_nid, and a and
    // b should have landed fine
    I(result.roster.get_node(file_path_internal("a"))->self == a_nid);
    I(result.roster.get_node(file_path_internal("b"))->self == b_nid);
    check_nn_conflict();
  }
};

struct node_name_plus_orphan : public node_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b", b_nid);
    make_nn_conflict("a/foo", "b/foo");
  }

  virtual void check()
  {
    I(result.roster.all_nodes().size() == 2);
    check_nn_conflict();
  }
};

struct node_name_plus_directory_loop : public node_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "b", b_nid);
    make_nn_conflict("a/foo", "b/foo");
    make_dir(left_roster, left_markings, old_rid, left_rid, "a/foo/b", b_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b/foo/a", a_nid);
  }

  virtual void check()
  {
    I(downcast_to_dir_t(result.roster.get_node(name_conflict_nid))->children.size() == 2);
    check_nn_conflict();
  }
};

struct node_name_plus_illegal_name : public node_name_plus_helper
{
  node_id new_root_nid;

  virtual void setup()
  {
    new_root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "new_root", new_root_nid);
    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", new_root_nid);
    make_nn_conflict("new_root/_MTN", "foo");
  }

  virtual void check()
  {
    I(result.roster.root()->self == new_root_nid);
    I(result.roster.all_nodes().size() == 2);
    check_nn_conflict();
  }
};

struct node_name_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "", left_root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "right_root", right_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", right_root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "left_root", left_root_nid);
  }
  void check_helper(node_name_conflict const & left_c, node_name_conflict const & right_c)
  {
    I(left_c.nid == left_root_nid);
    I(left_c.left == make_pair(the_null_node, path_component()));
    I(left_c.right == make_pair(right_root_nid, path_component("left_root")));

    I(right_c.nid == right_root_nid);
    I(right_c.left == make_pair(left_root_nid, path_component("right_root")));
    I(right_c.right == make_pair(the_null_node, path_component()));
  }
  virtual void check()
  {
    I(!result.is_clean());
    I(result.node_name_conflicts.size() == 2);

    if (idx(result.node_name_conflicts, 0).nid == left_root_nid)
      check_helper(idx(result.node_name_conflicts, 0),
                   idx(result.node_name_conflicts, 1));
    else
      check_helper(idx(result.node_name_conflicts, 1),
                   idx(result.node_name_conflicts, 0));

    I(result.missing_root_dir);

    result.roster.attach_node(left_root_nid, file_path());
    result.roster.attach_node(right_root_nid, file_path_internal("totally_other_name"));
    result.node_name_conflicts.pop_back();
    result.node_name_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct rename_target_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, left_rid, left_rid, "", left_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "", right_root_nid);
  }
  virtual void check()
  {
    I(!result.is_clean());
    rename_target_conflict const & c = idx(result.rename_target_conflicts, 0);
    I((c.nid1 == left_root_nid && c.nid2 == right_root_nid)
      || (c.nid1 == right_root_nid && c.nid2 == left_root_nid));
    I(c.parent_name == make_pair(the_null_node, path_component()));

    I(result.missing_root_dir);

    // we can't just attach one of these as the root -- see the massive
    // comment on the old_locations member of roster_t, in roster.hh.
    result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
    result.roster.attach_node(left_root_nid, file_path_internal("totally_left_name"));
    result.roster.attach_node(right_root_nid, file_path_internal("totally_right_name"));
    result.rename_target_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

UNIT_TEST(roster_merge, complex_structural_conflicts)
{
  {
    node_name_plus_rename_target t;
    t.test();
  }
  {
    node_name_plus_orphan t;
    t.test();
  }
  {
    node_name_plus_directory_loop t;
    t.test();
  }
  {
    node_name_plus_illegal_name t;
    t.test();
  }
  {
    node_name_plus_missing_root t;
    t.test();
  }
  {
    rename_target_plus_missing_root t;
    t.test();
  }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
