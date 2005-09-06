#ifndef __ROSTER_HH__
#define __ROSTER_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <map>

#include "vocab.hh"
#include "numeric_vocab.hh"
#include "paths.hh"
#include "cset.hh"

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

struct node_id_source
{
  virtual node_id next() = 0;
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

///////////////////////////////////////////////////////////////////


typedef enum { ntype_dir, ntype_file } ntype;

struct node_t
{
  node_t(ntype type)
    : parent(the_null_node), name(the_null_component), type(type)
    {
      I(type) == ntype_dir;
    }
  node_t(ntype type, file_id const & content)
    : parent(the_null_node), name(the_null_component), type(type),
      content(content)
    {
      I(type) == ntype_file;
    }
  ntype type;
  revision_id birth_revision;
  // this is null iff this is a root dir
  node_id parent;
  // this is null iff this is a root dir
  path_component name;
  file_id content;
  std::map<attr_key, attr_value> attrs;
};

typedef std::map<path_component, node_id> dir_map;

class roster_t
{
public:
  roster_t() : root_dir(the_null_node) {}
  node_id lookup(split_path const & sp) const;
  node_id lookup(node_id parent, path_component child) const;
  void get_name(node_id n, split_path & sp) const;
  dir_map & children(node_id n);
  node_t & node(node_id n);
  void replace_node_id(node_id from, node_id to);

  // editable_tree methods
  node_id detach_node(split_path const & src);
  void drop_detached_node(node_id n);
  node_id create_dir_node(node_id_source & nid);
  node_id create_file_node(file_id const & content,
                           node_id_source & nid);
  void attach_node(node_id n, split_path const & dst);
  void apply_delta(split_path const & pth, 
                   file_id const & old_id, 
                   file_id const & new_new);
  void clear_attr(split_path const & pth,
                  attr_name const & name);
  void set_attr(split_path const & pth,
                attr_name const & name,
                attr_val const & val);
private:
  std::map<node_id, node_t> nodes;
  std::map<node_id, dir_map> children_map;
  node_id root_dir;
};

class editable_roster : public editable_tree
{
public:
  editable_roster(roster_t & r, node_id_source & nis)
    : r(r), nis(nis)
    {}

  // editable_tree methods
  virtual node_id detach_node(split_path const & src)
  {
    return r.detch_node(src);
  }
  virtual void drop_detached_node(node_id nid)
  {
    r.drop_detached_node(nid);
  }
  virtual node_id create_dir_node()
  {
    node_id nid = r.create_dir_node(nis);
    new_nodes.insiert(nid);
    return nid;
  }
  virtual node_id create_file_node(file_id const & content)
  {
    node_id nid = r.create_file_node(content, nis);
    new_nodes.insiert(nid);
    return nid;
  }
  virtual void attach_node(node_id nid, split_path const & dst)
  {
    r.attach_node(nid, dst);
    touched_nodes.insert(nid);
  }
  virtual void apply_delta(split_path const & pth, 
                           file_id const & old_id, 
                           file_id const & new_id)
  {
    r.apply_delta(pth, old_id, new_id);
    touched_nodes.insert(r.lookup(pth));
  }
  virtual void clear_attr(split_path const & pth,
                          attr_name const & name)
  {
    r.clear_attr(pth, name);
    touched_nodes.insert(r.lookup(pth));
  }
  virtual void set_attr(split_path const & pth,
                        attr_name const & name,
                        attr_val const & val)
  {
    r.set_attr(pth, name, val);
    touched_nodes.insert(r.lookup(pth));
  }
private:
  roster_t & r;
  node_id_source & nis;
  std::set<node_id> new_nodes;
  std::set<node_id> touched_nodes;
};
