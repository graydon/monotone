#ifndef __CSET_HH__
#define __CSET_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <set>
#include "vector.hh"

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
  virtual node_id detach_node(file_path const & src) = 0;
  virtual void drop_detached_node(node_id nid) = 0;

  // Attaching new nodes (via creation or as the tail end of renaming)
  virtual node_id create_dir_node() = 0;
  virtual node_id create_file_node(file_id const & content) = 0;
  virtual void attach_node(node_id nid, file_path const & dst) = 0;

  // Modifying elements in-place
  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id) = 0;
  virtual void clear_attr(file_path const & pth,
                          attr_key const & name) = 0;
  virtual void set_attr(file_path const & pth,
                        attr_key const & name,
                        attr_value const & val) = 0;

  virtual void commit() = 0;

  virtual ~editable_tree() {}
};


// In-memory representation of a change set.

struct cset
{
  // Deletions.
  std::set<file_path> nodes_deleted;

  // Additions.
  std::set<file_path> dirs_added;
  std::map<file_path, file_id> files_added;

  // Pure renames.
  std::map<file_path, file_path> nodes_renamed;

  // Pure deltas.
  std::map<file_path, std::pair<file_id, file_id> > deltas_applied;

  // Attribute changes.
  std::set<std::pair<file_path, attr_key> > attrs_cleared;
  std::map<std::pair<file_path, attr_key>, attr_value> attrs_set;

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

inline file_path const &
delta_entry_path(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->first;
}

inline file_id const &
delta_entry_src(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
{
  return i->second.first;
}

inline file_id const &
delta_entry_dst(std::map<file_path, std::pair<file_id, file_id> >::const_iterator i)
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

template <> void
dump(cset const & cs, std::string & out);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CSET_HH__
