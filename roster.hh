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

template <> void dump(full_attr_map_t const & val, std::string & out);


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
  bool has_child(path_component const & pc) const;
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

inline bool
is_root_dir_t(node_t n) 
{
  if (is_dir_t(n) && null_name(n->name))
    {
      I(null_node(n->parent));
      return true;
    }

  return false;
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

bool
shallow_equal(node_t a, node_t b, bool shallow_compare_dir_children);

template <> void dump(node_t const & n, std::string & out);

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
template <> void dump(marking_t const & marking, std::string & out);
template <> void dump(marking_map const & marking_map, std::string & out);

namespace basic_io { struct printer; struct parser; }

class roster_t
{
public:
  roster_t() {}
  roster_t(roster_t const & other);
  roster_t & operator=(roster_t const & other);
  bool has_root() const;
  bool has_node(split_path const & sp) const;
  bool has_node(node_id nid) const;
  node_t get_node(split_path const & sp) const;
  node_t get_node(node_id nid) const;
  void get_name(node_id nid, split_path & sp) const;
  void replace_node_id(node_id from, node_id to);

  // editable_tree operations
  node_id detach_node(split_path const & src);
  void drop_detached_node(node_id nid);
  node_id create_dir_node(node_id_source & nis);
  void create_dir_node(node_id nid);
  node_id create_file_node(file_id const & content,
                           node_id_source & nis);
  void create_file_node(file_id const & content,
                        node_id nid);
  void insert_node(node_t n);
  void attach_node(node_id nid, split_path const & dst);
  void attach_node(node_id nid, node_id parent, path_component name);
  void apply_delta(split_path const & pth, 
                   file_id const & old_id, 
                   file_id const & new_id);
  void clear_attr(split_path const & pth,
                  attr_key const & name);
  void set_attr(split_path const & pth,
                attr_key const & name,
                attr_value const & val);
  void set_attr(split_path const & pth,
                attr_key const & name,
                std::pair<bool, attr_value> const & val);

  void extract_path_set(path_set & paths) const;

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

  dir_t const & root() { return root_dir; }

private:
  void do_deep_copy_from(roster_t const & other);
  dir_t root_dir;
  node_map nodes;
  // this attribute holds the previous location of detached nodes.  when
  // applying a cset, we pass through intermediate states where some nodes are
  // detached from the directory tree.  it is illegal to re-attach these nodes
  // to where they started -- you shouldn't have detached them in the first
  // place, if you were just going to put them back!  this checking verifies
  // that csets are in normalized form.
  std::map<node_id, std::pair<node_id, path_component> > old_locations;
  template <typename T> friend void dump(T const & val, std::string & out);
};

struct temp_node_id_source 
  : public node_id_source
{
  temp_node_id_source();
  virtual node_id next();
  node_id curr;
};

template <> void dump(roster_t const & val, std::string & out);

struct app_state;
struct revision_set;

// adaptor class to enable cset application on rosters.
class editable_roster_base 
  : public editable_tree
{
public:
  editable_roster_base(roster_t & r, node_id_source & nis);
  virtual node_id detach_node(split_path const & src);
  virtual void drop_detached_node(node_id nid);
  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, split_path const & dst);
  virtual void apply_delta(split_path const & pth, 
                           file_id const & old_id, 
                           file_id const & new_id);
  virtual void clear_attr(split_path const & pth,
                          attr_key const & name);
  virtual void set_attr(split_path const & pth,
                        attr_key const & name,
                        attr_value const & val);
  virtual void commit();
protected:
  roster_t & r;
  node_id_source & nis;
};


void
make_cset(roster_t const & from, 
          roster_t const & to, 
          cset & cs);

bool
equal_up_to_renumbering(roster_t const & a, marking_map const & a_markings,
                        roster_t const & b, marking_map const & b_markings);

void
select_nodes_modified_by_cset(cset const & cs,
                              roster_t const & old_roster,
                              roster_t const & new_roster,
                              std::set<node_id> & nodes_changed,
                              std::set<node_id> & nodes_born);

void 
classify_roster_paths(roster_t const & ros,
                      path_set & unchanged,
                      path_set & changed,
                      path_set & missing,
                      app_state & app);

void 
update_restricted_roster_from_filesystem(roster_t & ros, 
                                         app_state & app);

void
extract_roster_path_set(roster_t const & ros, 
                        path_set & paths);

void
make_roster_for_base_plus_cset(revision_id const & base, 
                               cset const & cs,
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

#ifdef BUILD_UNIT_TESTS

struct testing_node_id_source 
  : public node_id_source
{
  testing_node_id_source();
  virtual node_id next();
  node_id curr;
};

#endif // BUILD_UNIT_TESTS

#endif

