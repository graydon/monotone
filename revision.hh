#ifndef __REVISION_HH__
#define __REVISION_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <string>

#include "patch_set.hh"
#include "vocab.hh"

// a revision is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// revision:
// {
//   new_manifest: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//   edge:
//   {
//     old_revision: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//     old_manifest: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//     changes:
//     {
//       delete: [s10:usr/bin/sh]
//       rename:
//       {
//         src: [s10:usr/bin/sh]
//         dst: [s10:usr/bin/sh]
//       }
//       delta:
//       {
//         path: [s10:/usr/bin/sh]
//         src: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]     
//         dst: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//       }
//       add:
//       {
//         path: [s10:usr/bin/sh]
//         data: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//       }
//     }
//   }
// }

extern std::string revision_file_name;

typedef std::set<file_path>
deletion_set;

typedef deletion_set::value_type
deletion_entry;

typedef std::map<file_path, file_id>
addition_map;

typedef addition_map::value_type
addition_entry;

typedef std::map<file_path, file_path>
rename_map;

typedef rename_map::value_type
rename_entry;

typedef std::map<file_path, std::pair<file_id, file_id> >
delta_map;

typedef delta_map::value_type
delta_entry;

// rename entry accessors 

inline file_path const &
rename_src(rename_entry const & a)
{
  return a.first;
}

inline file_path const &
rename_src(rename_map::const_iterator i)
{
  return i->first;
}

inline file_path const &
rename_dst(rename_entry const & a)
{
  return a.second;
}

inline file_path const &
rename_dst(rename_map::const_iterator const & i)
{
  return i->second;
}

// delta entry accessors

inline file_path const &
delta_path(delta_entry const & d)
{
  return d.first;
}

inline file_path const &
delta_path(delta_map::const_iterator i)
{
  return i->first;
}

inline file_id const &
delta_src_id(delta_entry const & d)
{
  return d.second.first;
}

inline file_id const &
delta_src_id(delta_map::const_iterator i)
{
  return i->second.first;
}

inline file_id const &
delta_dst_id(delta_entry const & d)
{
  return d.second.second;
}

inline file_id const &
delta_dst_id(delta_map::const_iterator i)
{
  return i->second.second;
}

// addition entry accessors 

inline file_path const &
addition_path(addition_entry const & a)
{
  return a.first;
}

inline file_path const &
addition_path(addition_map::const_iterator i)
{
  return i->first;
}

inline file_id const &
addition_id(addition_entry const & a)
{
  return a.second;
}

inline file_id const &
addition_id(addition_map::const_iterator const & i)
{
  return i->second;
}

struct 
change_set
{
  deletion_set dels;
  rename_map renames;
  delta_map deltas;
  addition_map adds;

  change_set();
  change_set(change_set const & other);
  change_set const & operator=(change_set const & other);
  change_set const & operator+(change_set const & other) const;
  change_set const & operator|(change_set const & other) const;
  bool is_applicable(manifest_map const &m) const;
  bool is_sane() const;
};

typedef std::map<revision_id, std::pair<manifest_id, change_set> > 
edge_map;

typedef edge_map::value_type
edge_entry;

struct 
revision_set
{
  manifest_id new_manifest;
  edge_map edges;
};

inline revision_id const & 
edge_old_revision(edge_entry const & e) 
{ 
  return e.first; 
}

inline revision_id const & 
edge_old_revision(edge_map::const_iterator i) 
{ 
  return i->first; 
}

inline manifest_id const & 
edge_old_manifest(edge_entry const & e) 
{ 
  return e.second.first; 
}

inline manifest_id const & 
edge_old_manifest(edge_map::const_iterator i) 
{ 
  return i->second.first; 
}

inline change_set  const & 
edge_changes(edge_entry const & e) 
{ 
  return e.second.second; 
}

inline change_set  const & 
edge_changes(edge_map::const_iterator i) 
{ 
  return i->second.second; 
}

void 
read_revision_set(data const & dat,
		  revision_set & rev);

void 
read_revision_set(revision_data const & dat,
		  revision_set & rev);

void
write_revision_set(revision_set const & rev,
		   data & dat);

void
write_revision_set(revision_set const & rev,
		   revision_data & dat);

bool operator==(const change_set & a, const change_set & b);
bool operator<(const change_set & a, const change_set & b);

#endif // __REVISION_HH__
