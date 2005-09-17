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
#include "sanity.hh"

typedef std::map<attr_key, attr_value> attr_map_t;

typedef std::vector<path_component> split_path;

// Virtual interface to a tree-of-files which you can edit
// destructively; this may be the filesystem or an in-memory
// representation (a roster / mfest).

typedef u32 node_id;

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
                           file_id const & new_id) = 0;
  virtual void clear_attr(split_path const & pth,
                          attr_key const & name) = 0;
  virtual void set_attr(split_path const & pth,
                        attr_key const & name,
                        attr_value const & val) = 0;

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
  std::set<std::pair<split_path, attr_key> > attrs_cleared;
  std::map<std::pair<split_path, attr_key>, attr_value> attrs_set;

  void apply_to(editable_tree & t) const;
  bool empty() const;
  void clear();
};

namespace basic_io { struct printer; struct parser; }

void 
print_cset(basic_io::printer & printer,
	   cset const & cs);

void 
parse_cset(basic_io::parser & parser,
	   cset & cs);


// Some helpers.

template <typename T>
void
safe_erase(T & container, typename T::key_type const & key)
{
  I(container.erase(key));
}

template <typename T>
typename T::iterator
safe_insert(T & container, typename T::value_type const & val)
{
  std::pair<typename T::iterator, bool> r = container.insert(val);
  I(r.second);
  return r.first;
}

template <typename T>
typename T::mapped_type const &
safe_get(T & container, typename T::key_type const & key)
{
  typename T::const_iterator i = container.find(key);
  I(i != container.end());
  return i->second;
}

inline split_path
internal_string_to_split_path(std::string const & str)
{
  split_path sp;
  file_path_internal(str).split(sp);
  return sp;
}

#endif // __CSET_HH__
