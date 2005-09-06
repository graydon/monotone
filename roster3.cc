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

