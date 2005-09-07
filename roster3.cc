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
  temp_node_id_source nis;
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
  for (std::set<node_id>::const_iterator i = new_ids.begin();
       i != new_ids.end(); ++i)
    {
      node_t & n = result.node(*i);
      n.birth_revision = new_rid;
      safe_insert(marking, std::make_pair(*i, marking_t(new_rid, n)));
    }
}
