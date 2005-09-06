#ifndef __CSET_HH__
#define __CSET_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>
#include <string>
#include <vector>

#include "numeric_vocab.hh"
#include "paths.hh"
#include "vocab.hh"

typedef std::string attr_name;
typedef std::string attr_val;
typedef std::map<attr_name, attr_val> attr_map;

typedef std::vector<path_component> split_path;

// Virtual interface to a tree-of-files which you can edit
// destructively; this may be the filesystem or an in-memory
// representation (a roster / mfest).

typedef u64 node_id;

struct editable_tree
{
  // Detaching existing nodes (for renaming or deleting)
  virtual node_id detach_node(split_path const & src) = 0;
  virtual void drop_detached_node(node_id n) = 0;

  // Attaching new nodes (via creation or as the tail end of renaming)
  virtual node_id create_dir_node() = 0;
  virtual node_id create_file_node(file_id const & content) = 0;
  virtual void attach_node(node_id n, split_path const & dst) = 0;

  // Modifying elements in-place
  virtual void apply_delta(split_path const & pth, 
                           file_id const & old_id, 
                           file_id const & new_new) = 0;
  virtual void clear_attr(split_path const & pth,
                          attr_name const & name) = 0;
  virtual void set_attr(split_path const & pth,
                        attr_name const & name,
                        attr_val const & val) = 0;

  virtual ~editable_tree() {}
};


// In-memory representation of a change set. 

struct cset 
{
  // Deletions.
  std::set<split_path> nodes_deleted;

  // Additions.
  std::set<split_path> dirs_added;
  std::map<split_path, file_id> files_added;

  // Pure renames.
  std::map<split_path, split_path> nodes_renamed;

  // Pure deltas.
  std::map<split_path, std::pair<file_id, file_id> > deltas_applied;

  // Attribute changes.
  std::set<std::pair<split_path, attr_name> > attrs_cleared;
  std::map<std::pair<split_path, attr_name>, attr_val> attrs_set;

  void apply_to(editable_tree & t);
};

#endif // __CSET_HH__
