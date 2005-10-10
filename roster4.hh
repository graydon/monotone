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

// FIXME: perhaps move this to paths.{cc,hh}
void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename);

node_id const the_null_node = 0;

inline bool 
null_node(node_id n)
{
  return n == the_null_node;
}


// (true, "val") or (false, "") are both valid attr values (for proper
// merging, we have to widen the attr_value type to include a first-class
// "undefined" value).
typedef std::map<attr_key, std::pair<bool, attr_value> > full_attr_map_t;
typedef std::map<path_component, node_t> dir_map;
typedef std::map<node_id, node_t> node_map;

void dump(full_attr_map_t const & val, std::string & out);


struct node 
{
  node();
  node(node_id i);
  node_id self;
  node_id parent; // the_null_node iff this is a root dir  
  path_component name; // the_null_component iff this is a root dir  
  full_attr_map_t attrs;

  // need a virtual function to make dynamic_cast work
  virtual node_t clone() = 0;
  virtual ~node() {}
};


struct dir_node
  : public node
{
  dir_node();
  dir_node(node_id i);
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
  file_node();
  file_node(node_id i, file_id const & f);
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

void dump(node_t const & n, std::string & out);


struct marking_t
{
  revision_id birth_revision;
  std::set<revision_id> parent_name;
  std::set<revision_id> file_content;
  std::map<attr_key, std::set<revision_id> > attrs;
  marking_t() {};
  bool operator==(marking_t const & other) const
  {
    return birth_revision == other.birth_revision
      && parent_name == other.parent_name
      && file_content == other.file_content
      && attrs == other.attrs;
  }
};

typedef std::map<node_id, marking_t> marking_map;

void dump(std::set<revision_id> & revids, std::string & out);
void dump(marking_t const & marking, std::string & out);
void dump(marking_map const & marking_map, std::string & out);

namespace basic_io { struct printer; struct parser; }

class roster_t
{
public:
  roster_t() {}
  roster_t(roster_t const & other);
  roster_t & operator=(roster_t const & other);
  bool has_root() const;
  bool has_node(split_path const & sp) const;
  bool has_node(node_id n) const;
  node_t get_node(split_path const & sp) const;
  node_t get_node(node_id n) const;
  void get_name(node_id n, split_path & sp) const;
  void replace_node_id(node_id from, node_id to);

  // editable_tree operations
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
                  attr_key const & name);
  void set_attr(split_path const & pth,
                attr_key const & name,
                attr_value const & val);
  void set_attr(split_path const & pth,
                attr_key const & name,
                std::pair<bool, attr_value> const & val);

  node_map const & all_nodes() const
  {
    return nodes;
  }

  bool operator==(roster_t const & other) const;

  void check_sane(bool temp_nodes_ok=false) const;

  // verify that this roster is sane, and corresponds to the given
  // marking map
  void check_sane_against(marking_map const & marks) const;

  void print_to(basic_io::printer & pr,
                marking_map const & mm,
                bool print_local_parts) const;

  void parse_from(basic_io::parser & pa,
                  marking_map & mm);

private:
  void do_deep_copy_from(roster_t const & other);
  void check_finite_depth() const;
  dir_t root_dir;
  node_map nodes;
  friend void dump(roster_t const & val, std::string & out);
};

struct temp_node_id_source 
  : public node_id_source
{
  temp_node_id_source();
  virtual node_id next();
  node_id curr;
};

void dump(roster_t const & val, std::string & out);

struct app_state;
struct revision_set;

void
make_cset(roster_t const & from, 
          roster_t const & to, 
          cset & cs);


void
make_roster_for_merge(cset const & left_cs, revision_id const & left_rid,
                      cset const & right_cs, revision_id const & right_rid,
                      revision_id const & new_rid,
                      roster_t & result, 
                      marking_map & marking, 
                      app_state & app);

void
make_roster_for_nonmerge(cset const & cs, 
                         revision_id const & parent_rid,
                         revision_id const & new_rid,
                         roster_t & result, 
                         marking_map & marking, 
                         app_state & app);

void 
make_roster_for_revision(revision_set const & rev, 
                         revision_id const & rid,
                         roster_t & result, 
                         marking_map & marking,
                         app_state & app);

void 
read_roster_and_marking(data const & dat,
                        roster_t & ros,
                        marking_map & mm);

void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         data & dat);

void
write_manifest_of_roster(roster_t const & ros,
                         data & dat);

#endif

