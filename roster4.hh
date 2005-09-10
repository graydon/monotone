#ifndef __ROSTER_HH__
#define __ROSTER_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>

#include <boost/shared_ptr.hpp>

#include "cset.hh"
#include "numeric_vocab.hh"
#include "paths.hh"
#include "sanity.hh"
#include "vocab.hh"

struct node_id_source
{
  virtual node_id next() = 0;
  virtual ~node_id_source() {}
};

///////////////////////////////////////////////////////////////////

struct node;
struct dir_node;
struct file_node;
typedef boost::shared_ptr<node> node_t;
typedef boost::shared_ptr<file_node> file_t;
typedef boost::shared_ptr<dir_node> dir_t;

// (true, "val") or (false, "") are both valid attr values (for proper
// merging, we have to widen the attr_val type to include a first-class
// "undefined" value).
typedef std::map<attr_key, std::pair<bool, attr_val> > full_attr_map;
typedef std::map<path_component, node_t> dir_map;
typedef std::map<node_id, node_t> node_map;


struct node 
{
  node();
  revision_id birth_revision;
  node_id self;
  node_id parent; // the_null_node iff this is a root dir  
  path_component name; // the_null_component iff this is a root dir  
  full_attr_map attrs;

  // need a virtual function to make dynamic_cast work
  virtual node_t clone() = 0;
  virtual ~node() {}
};


struct dir_node
  : public node
{
  dir_map children;
  node_t get_child(path_component const & pc) const;
  void attach_child(path_component const & pc, node_t child);
  node_t detach_child(path_component const & pc);

  // need a virtual function to make dynamic_cast work
  virtual node_t clone();
  virtual ~dir_node() {}
};


struct file_node
  : public node
{
  file_id content;

  // need a virtual function to make dynamic_cast work
  virtual node_t clone();
  virtual ~file_node() {}
};

inline bool
is_dir_t(node_t n)
{
  dir_t d = boost::dynamic_pointer_cast<dir_node, node>(n);
  return static_cast<bool>(d);
}


inline bool
is_file_t(node_t n)
{
  file_t f = boost::dynamic_pointer_cast<file_node, node>(n);
  return static_cast<bool>(f);
}


inline dir_t
downcast_to_dir_t(node_t const n)
{
  dir_t d = boost::dynamic_pointer_cast<dir_node, node>(n);
  I(static_cast<bool>(d));
  return d;
}


inline file_t
downcast_to_file_t(node_t const n)
{
  file_t f = boost::dynamic_pointer_cast<file_node, node>(n);
  I(static_cast<bool>(f));
  return f;
}


struct marking_t
{
  std::set<revision_id> parent_name;
  std::set<revision_id> file_content;
  std::map<attr_key, std::set<revision_id> > attrs;
  marking_t(revision_id const & birth_rid, node_t n);
};

typedef std::map<node_id, marking_t> marking_map;


class roster_t
{
public:
  roster_t() {}
  bool has_root() const;
  node_t get_node(split_path const & sp) const;
  node_t get_node(node_id n) const;
  void get_name(node_id n, split_path & sp) const;
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
  void set_attr(split_path const & pth,
                attr_name const & name,
                std::pair<bool, attr_val> const & val);

  node_map const & all_nodes() const
  {
    return nodes;
  }

  // verify that this roster is sane, and corresponds to the given
  // marking map
  void check_sane(marking_map const & marks) const;

private:
  void check_finite_depth() const;
  dir_t root_dir;
  node_map nodes;
};

/*

void make_roster_for_revision(revision_set const & rev, revision_id const & rid,
                              roster_t & result, marking_map & marking,
                              app_state & app);
*/

#endif

