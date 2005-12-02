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

// Virtual interface to a tree-of-files which you can edit
// destructively; this may be the filesystem or an in-memory
// representation (a roster / mfest).

typedef u32 node_id;

struct editable_tree
{
  // Detaching existing nodes (for renaming or deleting)
  virtual node_id detach_node(split_path const & src) = 0;
  virtual void drop_detached_node(node_id nid) = 0;

  // Attaching new nodes (via creation or as the tail end of renaming)
  virtual node_id create_dir_node() = 0;
  virtual node_id create_file_node(file_id const & content) = 0;
  virtual void attach_node(node_id nid, split_path const & dst) = 0;

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
  path_set nodes_deleted;

  // Additions.
  path_set dirs_added;
  std::map<split_path, file_id> files_added;

  // Pure renames.
  std::map<split_path, split_path> nodes_renamed;

  // Pure deltas.
  std::map<split_path, std::pair<file_id, file_id> > deltas_applied;

  // Attribute changes.
  std::set<std::pair<split_path, attr_key> > attrs_cleared;
  std::map<std::pair<split_path, attr_key>, attr_value> attrs_set;

  bool operator==(cset const & other) const
  {
    return nodes_deleted == other.nodes_deleted
      && dirs_added == other.dirs_added
      && files_added == other.files_added
      && nodes_renamed == other.nodes_renamed
      && deltas_applied == other.deltas_applied
      && attrs_cleared == other.attrs_cleared
      && attrs_set == other.attrs_set
      ;
  }
  
  void apply_to(editable_tree & t) const;
  bool empty() const;
  void clear();
};

inline split_path const & 
delta_entry_path(std::map<split_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->first;
}

inline file_id const & 
delta_entry_src(std::map<split_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->second.first;
}

inline file_id const & 
delta_entry_dst(std::map<split_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->second.second;
}


namespace basic_io { struct printer; struct parser; }

void 
print_cset(basic_io::printer & printer,
           cset const & cs);

void
write_cset(cset const & cs, data & dat);

void 
parse_cset(basic_io::parser & parser,
           cset & cs);

void
read_cset(data const & dat, cset & cs);

void
dump(cset const & cs, std::string & out);


// Some helpers.

inline split_path
internal_string_to_split_path(std::string const & str)
{
  split_path sp;
  file_path_internal(str).split(sp);
  return sp;
}

#endif // __CSET_HH__
