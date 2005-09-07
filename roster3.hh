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

struct node_id_source
{
  virtual node_id next() = 0;
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
  std::map<node_id, node_t> const & all_nodes() const
  {
    return nodes;
  }
private:
  std::map<node_id, node_t> nodes;
  std::map<node_id, dir_map> children_map;
  node_id root_dir;
};

// adaptor class to enable cset application on rosters.
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
    new_nodes.insert(nid);
    return nid;
  }
  virtual node_id create_file_node(file_id const & content)
  {
    node_id nid = r.create_file_node(content, nis);
    new_nodes.insert(nid);
    return nid;
  }
  virtual void attach_node(node_id nid, split_path const & dst)
  {
    r.attach_node(nid, dst);
  }
  virtual void apply_delta(split_path const & pth, 
                           file_id const & old_id, 
                           file_id const & new_id)
  {
    r.apply_delta(pth, old_id, new_id);
  }
  virtual void clear_attr(split_path const & pth,
                          attr_name const & name)
  {
    r.clear_attr(pth, name);
  }
  virtual void set_attr(split_path const & pth,
                        attr_name const & name,
                        attr_val const & val)
  {
    r.set_attr(pth, name, val);
  }
  std::set<node_id> new_nodes;
private:
  roster_t & r;
  node_id_source & nis;
};


struct marking_t
{
  std::set<revision_id> parent_name;
  std::set<revision_id> file_content;
  std::map<attr_key, std::set<revision_id> > attrs;
  marking_t(revision_id const & birth_rid, node_t const & n)
  {
    std::set<node_id> singleton;
    singleton.insert(birth_rid);
    parent_name = singleton;
    file_content = singleton;
    for (std::map<attr_key, attr_value>::const_iterator i = n.attrs.begin();
         i != n.attrs.end(); ++i)
      attrs.insert(std::make_pair(i->first, singleton));
  }
};

typedef std::map<node_id, marking_t> marking_map;
