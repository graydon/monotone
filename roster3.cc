// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "roster3.hh"

// FIXME: move this to a header somewhere
template <typename T, typename K>
safe_erase(T & container, K const & key)
{
  I(container.erase(key));
}
template <typename T, typename V>
safe_insert(T & container, V const & val)
{
  I(container.insert(val).second);
}

// FIXME: we assume split and join paths always start with a null component

node_id
roster_t::lookup(split_path const & sp) const
{
  node_id nid = the_null_node;
  for (split_path::const_iterator i = sp.begin(); i != sp.end(); ++i)
    nid = lookup(nid, *i);
  return nid;
}

node_id
roster_t::lookup(node_id parent, path_component child) const
{
  if (null_node(parent))
    {
      I(null_name(child));
      I(!null_node(root_dir));
      return root_dir;
    }
  dir_map & dir = children(parent);
  dir_map::const_iterator i = dir.find(child);
  I(i != dir.end());
  return i->second;
}

node_id
roster_t::get_name(node_id nid, split_path & sp) const
{
  sp.clear();
  while (!null_node(nid))
    {
      node_t & n = element(nid);
      sp.push_back(n.name);
      nid = n.parent;
    }
  std::reverse(sp.begin(), sp.end());
}

dir_map & children
roster_t::children(node_id nid)
{
  std::map<node_id, dir_map>::iterator i = children_map.find(nid);
  I(i != children_map.end());
  return *i;
}

node_t & node(node_id nid)
{
  std::map<node_id, node_t>::iterator i = nodes.find(nid);
  I(i != nodes.end());
  return *i;
}

void replace_node_id(node_id from, node_id to)
{
  {
    std::map<node_id, node_t>::iterator i = nodes.find(from);
    I(i != nodes.end());
    safe_insert(nodes, std::make_pair(to, i->second));
    nodes.erase(i);
  }
  node_t n = node(to);
  if (root_dir == from)
    root_dir = to;
  else
    {
      dir_map & dir = children(n.parent);
      dir_map::iterator i = dir.find(n.name);
      I(i != dir.end());
      I(i->second == from);
      i->second = to;
    }
  if (n.type == etype_dir)
    {
      std::map<node_id, dir_map>::iterator i = children_map.find(from);
      I(i != children_map.end());
      for (dir_map::iterator j = i->second.begin(); j != i->second.end(); ++j)
        {
          node_t & child_n = node(j->second);
          I(child_n.parent == from);
          child_n.parent = to;
        }
      safe_insert(children_map, std::make_pair(to, i->second));
      children_map.erase(i);
    }
}

static inline void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename)
{
  I(!sp.empty());
  split_path::const_iterator penultimate = sp.end();
  --penultimate;
  dirname = split_path(sp.begin(), penultimate);
  basename = *penultimate;
}

// FIXME: we assume split and join paths always start with a null component
// split_path [] means the_null_component (which is the parent of the root dir)
// [""] means the root dir
// ["", "foo"] means root dir's sub-element "foo"
// etc.

void
roster_t::detach_node(split_path const & src)
{
  node_id nid = lookup(src);
  node_t & n = node(nid);
  // for now, the root dir can be created, but cannot be removed
  I(nid != root_dir);
  node_id parent = n.parent;
  safe_erase(children(parent), nid);
  n.parent = the_null_node;
  n.name = the_null_component;
  if (n.type == ntype_dir)
    {
      std::map<esoul, dir_map>::iterator i = children_map.find(es);
      I(i != children_map.end());
      I(i->second.empty());
      children_map.erase(i);
    }
}

void
roster_t::drop_detached_node(node_id nid)
{
  safe_erase(nodes, nid);
}

node_id
roster_t::create_dir_node(node_id_source & nis)
{
  node_id nid = nis.next();
  safe_insert(nodes, std::make_pair(node_t(ntype_dir), n));
  return nid;
}

node_id
roster_t::create_file_node(file_id const & content, node_id_source & nis)
{
  node_id nid = nis.next();
  safe_insert(nodes, std::make_pair(node_t(ntype_file, content), n));
  return nid;
}

// FIXME: we assume split and join paths always start with a null component
// split_path [] means the_null_component (which is the parent of the root dir)
// [""] means the root dir
// ["", "foo"] means root dir's sub-element "foo"
// etc.

void
roster_t::attach_node(node_id nid, split_path const & dst)
{
  split_path dirname;
  path_component basename;
  dirname_basename(dst, dirname, basename);
  node_id parent = lookup(dirname);
  node_t & n = node(nid);
  if (null_node(parent))
    {
      // this is the root dir
      root_dir = nid;
      I(n.type == ntype_dir);
    }
  else
    safe_insert(children(parent), std::make_pair(basename, nid));
  if (n.type == ntype_dir)
    safe_insert(children_map, std::make_pair(nid, dir_map()));
}

void
roster_t::apply_delta(split_path const & pth,
                      file_id const & old_id,
                      fild_id const & new_id);
{
  node_id nid = lookup(pth);
  node_t & n = node(nid);
  I(n.type = ntype_file);
  I(n.content == old_id);
  I(n.content != new_id);
  n.content = new_id;
}

void
roster_t::clear_attr(split_path const & pth,
                     attr_name const & name)
{
  node_id nid = lookup(pth);
  node_t & n = node(nid);
  safe_erase(n.attrs, name);
}

void
roster_t::set_attr(split_path const & pth,
                   attr_name const & name,
                   attr_val const & val)
{
  node_id nid = lookup(pth);
  node_t & n = node(nid);
  std::map<attr_key, attr_value>::iterator i = n.attrs.find(name);
  I(i != n.attrs.end());
  I(i->second != val);
  i->second = val;
}

namespace 
{
  const node_id the_null_node = 0;
  const node_id first_node = 1;
  inline bool null_node(node_id n)
  {
    return n == the_null_node;
  }
  const node_id first_temp_node = widen<node_id, int>(1) << (sizeof(node_id) * 8 - 1);
  inline bool temp_node(node_id n)
  {
    return n & first_temp_node;
  }
  
  struct temp_node_id_source : public node_id_source
  {
    temp_node_id_source() : curr(first_temp_node) {}
    virtual node_id next()
    {
      node_id n = curr++;
      I(temp_node(n));
      return n;
    }
    node_id curr;
  };

  struct true_node_id_source : public node_id_source
  {
    true_node_id_source(app_state & app) : app(app) {}
    virtual node_id next()
    {
      node_id n = app.db.next_node_id();
      I(!temp_node(n));
      return n;
    }
    app_state & app;
  };

  // this handles all the stuff in a_new
  void unify_roster_oneway(roster_t & a, std::set<node_id> & a_new,
                           roster_t & b, std::set<node_id> & b_new,
                           std::set<node_id> & new_ids,
                           node_id_source & nis)
  {
    for (std::set<node_id>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        node_id const aid = *i;
        split_path sp;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        a.get_name(aid, sp);
        node_id const bid = b.lookup(aid);
        if (temp_node(bid))
          {
            node_id new_nid = nis.next();
            a.replace_node_id(ls, new_nid);
            b.replace_node_id(rs, new_nid);
            new_ids.insert(new_nid);
            b_new.erase(bid);
          }
        else
          {
            a.replace_node_id(aid, bid);
            a.node(bid).birth_revision = b.node(bid).birth_revision;
          }
      }
  }

  // after this, left should == right, and there should be no temporary ids
  // destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, std::set<node_id> & left_new,
                roster_t & right, std::set<node_id> & right_new,
                // these new_souls all come from the given soul source
                std::set<node_id> & new_ids,
                node_id_source & nis)
  {
    unify_roster_oneway(left, left_new, right, right_new, new_souls, ss);
    unify_roster_oneway(right, right_new, left, left_new, new_souls, ss);
  }

  // this function implements the case
  //   a   b1
  //    \ /
  //     b2
  void
  mark_won_merge(std::set<revision_id> const & a_marks,
                 std::set<revision_id> const & a_uncommon_ancestors,
                 std::set<revision_id> const & b1_marks,
                 revision_id const & new_rid,
                 std::set<revision_id> & new_marks)
  {
    for (std::set<revision_id>::const_iterator i = a_marks.begin();
         i != a_marks.end(); ++i)
      {
        if (a_uncommon_ancestors.find(*j) != a_uncommon_ancestors.end())
          {
            // at least one element of *(a) is not an ancestor of b1
            new_marks.clear();
            new_marks.insert(new_rid);
            return;
          }
      }
    // all elements of *(a) are ancestors of b1; this was a clean merge to b,
    // so copy forward the marks.
    new_marks = b1_marks;
  }
}

void
make_roster_for_merge(cset const & left_cs, revision_id const & left_rid,
                      cset const & right_cs, revision_id const & right_rid,
                      revision_id const & new_rid,
                      roster_t & result, marking_map & marking, app_state & app)
{
  I(!null_id(left_rid) && !null_id(right_rid));
  roster_t left_r, right_r;
  marking_map left_marking, right_marking;
  app.db.get_roster(left_rid, left_r, left_marking);
  app.db.get_roster(right_rid, right_r, right_marking);
  {
    temp_node_id_source nis;
    // SPEEDUP?: the copies on the next two lines are probably the main
    // bottleneck in this code
    result = left_r;
    roster_t from_right_r(right_r);
    editable_roster from_left_er(result, nis), from_right_er(from_right_r, nis);
    left_cs.apply_to(from_left_er);
    right_cs.apply_to(from_right_er);
    std::set<node_id> new_ids;
    unify_rosters(result, from_left_er.new_nodes,
                  from_right_r, from_right_er.new_nodes,
                  new_ids, true_node_id_source(app));
    I(result == new_from_right);
  }
  // SPEEDUP?: instead of constructing new marking from scratch, track which
  // nodes were modified, and scan only them
  // load one of the parent markings directly into the new marking map
  marking.clear();
  std::set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  app.db.get_uncommon_ancestors(left_rid, right_rid,
                                left_uncommon_ancestors, right_uncommon_ancestors);
  for (std::map<node_id, node_t>::const_iterator i = result.all_nodes().begin();
       i != result.all_nodes().end(); ++i)
    {
      node_t const & n = i->second;
      // SPEEDUP?: instead of using find repeatedly, iterate everything in
      // parallel
      std::map<node_id, node_t>::const_iterator lni = left_r.all_nodes().find(i->first);
      std::map<node_id, node_t>::const_iterator rni = right_r.all_nodes().find(i->first);
      marking_map::const_iterator lmi = left_marking.find(i->first);
      marking_map::const_iterator rmi = right_marking.find(i->first);
      bool exists_in_left = (lni != left_r.all_nodes.end());
      bool exists_in_right = (rni != right_r.all_nodes.end());
      if (!exists_in_left && !exists_in_right)
        {
          marking.insert(std::make_pair(i->first, marking_t(new_rid)));
          I(n.birth_revision == new_rid);
        }
      else if (!exists_in_left && exists_in_right)
        {
          marking.insert(*rni);
          node_t const & rn = rni->second;
          I(n.type == rn.type && n.birth_revision == rn.birth_revision);
          I(right_uncommon_ancestors.find(n.birth_revision)
            != right_uncommon_ancestors.end());
        }
      else if (exists_in_left && !exists_in_right)
        {
          marking.insert(*lni);
          node_t const & ln = lni->second;
          I(n.type == ln.type && n.birth_revision == ln.birth_revision);
          I(left_uncommon_ancestors.find(n.birth_revision)
            != left_uncommon_ancestors.end());
        }
      else
        {
          node_t const & ln = lni->second;
          node_t const & rn = rni->second;
          I(n.type == rn.type && n.birth_revision == rn.birth_revision);
          I(n.type == ln.type && n.birth_revision == ln.birth_revision);
          marking_t marks;
          marking_t const & lmarks = lmi->second;
          marking_t const & rmarks = rmi->second;
          // name
          {
            bool diff_from_left = (n.parent != ln.parent || n.name != ln.name);
            bool diff_from_right = (n.parent != rn.parent || n.name != rn.name);
            if (diff_from_left && diff_from_right)
              new_marks.parent_name.insert(new_rid);
            else if (diff_from_left && !diff_from_right)
              mark_won_merge(lmarks.parent_name, left_uncommon_ancestors,
                             rmarks.parent_name, new_rid,
                             marks.parent_name);
            else if (!diff_from_left && diff_from_right)
              mark_won_merge(rmarks.parent_name, right_uncommon_ancestors,
                             lmarks.parent_name, new_rid,
                             marks.parent_name);
            else
              {
                // this is the case
                //   a   a
                //    \ /
                //     a
                // so we simply union the mark sets.  This is technically not
                // quite the canonical multi-*-merge thing to do; in the case
                //    a1*
                //   / \
                //  b   a2
                //  |   |
                //  a3* |
                //   \ /
                //    a4
                // we will set *(a4) = {a1, a3}, even though the minimal
                // common ancestor set is {a3}.  we could fix this by running
                // erase_ancestors.  However, there isn't really any point;
                // the only operation performed on *(a4) is to test *(a4) > R
                // for some revision R.  The truth-value of this test cannot
                // be affected by added new revisions to *(a4) that are
                // ancestors of revisions that are already in *(a4).
                std::set_union(lmarks.parent_name.begin(), lmarks.parent_name.end(),
                               rmarks.parent_name.begin(), rmarks.parent_name.end(),
                               std::inserter(marks.parent_name));
              }
          }
          // content
          if (n.type == ntype_file)
            {
              bool diff_from_left = (n.content != ln.content);
              bool diff_from_right = (n.content != rn.content);
              if (diff_from_left && diff_from_right)
                new_marks.file_content.insert(new_rid);
              else if (diff_from_left && !diff_from_right)
                mark_won_merge(lmarks.file_content, left_uncommon_ancestors,
                               rmarks.file_content, new_rid,
                               marks.file_content);
              else if (!diff_from_left && diff_from_right)
                mark_won_merge(rmarks.file_content, right_uncommon_ancestors,
                               lmarks.file_content, new_rid,
                               marks.file_content);
            else
                std::set_union(lmarks.file_content.begin(), lmarks.file_content.end(),
                               rmarks.file_content.begin(), rmarks.file_content.end(),
                               std::inserter(marks.file_content));
            }
          
    }
  check_sane(result);
}
