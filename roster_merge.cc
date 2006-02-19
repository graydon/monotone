// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include "vocab.hh"
#include "roster_merge.hh"
#include "parallel_iter.hh"
#include "safe_map.hh"

bool
roster_merge_result::is_clean()
{
  return is_clean_except_for_content()
    && file_content_conflicts.empty();
}

bool
roster_merge_result::is_clean_except_for_content()
{
  return node_name_conflicts.empty()
    && node_attr_conflicts.empty()
    && orphaned_node_conflicts.empty()
    && rename_target_conflicts.empty()
    && directory_loop_conflicts.empty();
}

void
roster_merge_result::log_conflicts()
{
  L(FL("unclean roster_merge: %d name conflicts, %d content conflicts, %d attr conflicts, "
       "%d orphaned node conflicts, %d rename target conflicts, %d directory loop conflicts\n") 
    % node_name_conflicts.size()
    % file_content_conflicts.size()
    % node_attr_conflicts.size()
    % orphaned_node_conflicts.size()
    % rename_target_conflicts.size()
    % directory_loop_conflicts.size());
  
  for (size_t i = 0; i < node_name_conflicts.size(); ++i)
    L(FL("name conflict on node %d: [parent %d, self %s] vs. [parent %d, self %s]\n") 
      % node_name_conflicts[i].nid 
      % node_name_conflicts[i].left.first 
      % node_name_conflicts[i].left.second
      % node_name_conflicts[i].right.first 
      % node_name_conflicts[i].right.second);
  
  for (size_t i = 0; i < file_content_conflicts.size(); ++i)
    L(FL("content conflict on node %d: [%s] vs. [%s]\n") 
      % file_content_conflicts[i].nid
      % file_content_conflicts[i].left
      % file_content_conflicts[i].right);
  
  for (size_t i = 0; i < node_attr_conflicts.size(); ++i)
    L(FL("attribute conflict on node %d, key %s: [%d, %s] vs. [%d, %s]\n") 
      % node_attr_conflicts[i].nid
      % node_attr_conflicts[i].key
      % node_attr_conflicts[i].left.first
      % node_attr_conflicts[i].left.second
      % node_attr_conflicts[i].right.first
      % node_attr_conflicts[i].right.second);
  
  for (size_t i = 0; i < orphaned_node_conflicts.size(); ++i)
    L(FL("orphaned node conflict on node %d, dead parent %d, name %s")
      % orphaned_node_conflicts[i].nid
      % orphaned_node_conflicts[i].parent_name.first
      % orphaned_node_conflicts[i].parent_name.second);
  
  for (size_t i = 0; i < rename_target_conflicts.size(); ++i)
    L(FL("rename target conflict: nodes %d, %d, both want parent %d, name %s")
      % rename_target_conflicts[i].nid1
      % rename_target_conflicts[i].nid2
      % rename_target_conflicts[i].parent_name.first
      % rename_target_conflicts[i].parent_name.second);
  
  for (size_t i = 0; i < directory_loop_conflicts.size(); ++i)
    L(FL("directory loop conflict: node %d, wanted parent %d, name %s")
      % directory_loop_conflicts[i].nid
      % directory_loop_conflicts[i].parent_name.first
      % directory_loop_conflicts[i].parent_name.second);
}

void
roster_merge_result::warn_non_content_conflicts()
{
  for (size_t i = 0; i < node_name_conflicts.size(); ++i)
    W(F("name conflict on node %d: [parent %d, self %s] vs. [parent %d, self %s]\n") 
      % node_name_conflicts[i].nid 
      % node_name_conflicts[i].left.first 
      % node_name_conflicts[i].left.second
      % node_name_conflicts[i].right.first 
      % node_name_conflicts[i].right.second);
  
  for (size_t i = 0; i < node_attr_conflicts.size(); ++i)
    W(F("attribute conflict on node %d, key %s: [%d, %s] vs. [%d, %s]\n") 
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
}

void
roster_merge_result::clear()
{
  node_attr_conflicts.clear();
  file_content_conflicts.clear();
  node_attr_conflicts.clear();
  orphaned_node_conflicts.clear();
  rename_target_conflicts.clear();
  directory_loop_conflicts.clear();
  roster = roster_t();
}

namespace 
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(std::set<revision_id> const & b_marks,
         std::set<revision_id> const & b_uncommon_ancestors)
  {
    for (std::set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               std::set<revision_id> const & left_marks,
               std::set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               std::set<revision_id> const & right_marks,
               std::set<revision_id> const & right_uncommon_ancestors,
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
                   std::set<revision_id> const & uncommon_ancestors,
                   roster_t & new_roster)
  {
    revision_id const & birth = safe_get(markings, n->self).birth_revision;
    if (uncommon_ancestors.find(birth) != uncommon_ancestors.end())
      create_node_for(n, new_roster);
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
        I(null_name(name));
        if (result.roster.has_root())
          {
            // see comments below about name collisions.
            rename_target_conflict c;
            c.nid1 = nid;
            c.nid2 = result.roster.root()->self;
            c.parent_name = std::make_pair(parent, name);
            split_path root_sp;
            file_path().split(root_sp);
            // this line will currently cause an abort, because we don't
            // support detaching the root node
            result.roster.detach_node(root_sp);
            result.rename_target_conflicts.push_back(c);
          }
      }
    else
      {
        // orphan:
        if (!result.roster.has_node(parent))
          {
            orphaned_node_conflict c;
            c.nid = nid;
            c.parent_name = std::make_pair(parent, name);
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
            c.parent_name = std::make_pair(parent, name);
            p->detach_child(name);
            result.rename_target_conflicts.push_back(c);
            return;
          }

        if (would_make_dir_loop(result.roster, nid, parent))
          {
            directory_loop_conflict c;
            c.nid = nid;
            c.parent_name = std::make_pair(parent, name);
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
             std::set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             std::set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result)
{
  result.clear();
  MM(left_parent);
  MM(left_markings);
  MM(right_parent);
  MM(right_markings);
  MM(result.roster);
  
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
                             left_markings, left_uncommon_ancestors,
                             result.roster);
            break;

          case parallel::in_right:
            insert_if_unborn(i.right_data(),
                             right_markings, right_uncommon_ancestors,
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
                std::pair<node_id, path_component> new_name;
                node_name_conflict conflict(new_n->self);
                if (merge_scalar(std::make_pair(left_n->parent, left_n->name),
                                 left_marking.parent_name,
                                 left_uncommon_ancestors,
                                 std::make_pair(right_n->parent, right_n->name),
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
                      std::pair<bool, attr_value> new_value;
                      node_attr_conflict conflict(new_n->self);
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
                                      std::make_pair(attr_i.left_key(),
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
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

// cases for testing:
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
//   node name, name and parent, file and dir
//   file content
//   node attr, file and dir
//
// attr lifecycle:
//   seen in both -- -->mark merge cases
//   live in one and unseen in other
//   dead in one and unseen in other
//
// two diff nodes with same name
// directory loops
// orphans
// name collision on root dir
//
// interactions:
//   in-node name conflict + possible between-node name conflict
//   in-node name conflict + both possible names orphaned
//   in-node name conflict + directory loop conflict
//   between-node name conflict + both nodes orphaned
//   between-node name conflict + both nodes cause loop

// need roster, marking, birth revs, and uncommon ancestors for each side...

namespace
{
  const revision_id a_uncommon1 = revision_id(std::string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  const revision_id a_uncommon2 = revision_id(std::string("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  const revision_id b_uncommon1 = revision_id(std::string("cccccccccccccccccccccccccccccccccccccccc"));
  const revision_id b_uncommon2 = revision_id(std::string("dddddddddddddddddddddddddddddddddddddddd"));
  const revision_id common1 = revision_id(std::string("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
  const revision_id common2 = revision_id(std::string("ffffffffffffffffffffffffffffffffffffffff"));

  const file_id fid1 = file_id(std::string("1111111111111111111111111111111111111111"));
  const file_id fid2 = file_id(std::string("2222222222222222222222222222222222222222"));

  split_path
  split(std::string const & s)
  {
    split_path sp;
    file_path_internal(s).split(sp);
    return sp;
  }
}

static void
make_dir(roster_t & r, marking_map & markings,
         revision_id const & birth_rid, revision_id const & parent_name_rid,
         std::string const & name, node_id nid)
{
  r.create_dir_node(nid);
  r.attach_node(nid, split(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  safe_insert(markings, std::make_pair(nid, marking));
}

static void
make_file(roster_t & r, marking_map & markings,
          revision_id const & birth_rid, revision_id const & parent_name_rid,
          revision_id const & file_content_rid,
          std::string const & name, file_id const & content,
          node_id nid)
{
  r.create_file_node(content, nid);
  r.attach_node(nid, split(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  marking.file_content.insert(file_content_rid);
  safe_insert(markings, std::make_pair(nid, marking));
}

static void
make_lifecycle_objs(roster_t & r, marking_map & markings, revision_id uncommon,
                    std::string const & name, node_id common_dir_nid, node_id common_file_nid,
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


static void
test_roster_merge_node_lifecycle()
{
  roster_t a_roster, b_roster;
  marking_map a_markings, b_markings;
  std::set<revision_id> a_uncommon, b_uncommon;
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
  make_lifecycle_objs(a_roster, a_markings, a_uncommon1, "a", common_dir_nid, common_file_nid,
                      a_safe_dir_nid, a_safe_file_nid, nis);
  make_lifecycle_objs(b_roster, b_markings, b_uncommon1, "b", common_dir_nid, common_file_nid,
                      b_safe_dir_nid, b_safe_file_nid, nis);
  // do the merge
  roster_merge_result result;
  roster_merge(a_roster, a_markings, a_uncommon, b_roster, b_markings, b_uncommon, result);
  I(result.is_clean());
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

void
add_roster_merge_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_roster_merge_node_lifecycle));
}

#endif // BUILD_UNIT_TESTS
