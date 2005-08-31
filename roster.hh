#ifndef __ROSTER_HH__
#define __ROSTER_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "numeric_vocab.hh"
#include "paths.hh"

// a persistent element id
// but "file_id" means something else...
// "element" terminology is stolen from clearcase, it means (file|directory)
// 32 bits should be sufficient; 4 billion distinct files would use 4
// terabytes of disk space, assuming each file requires only a single sqlite
// page.  easy to change in a few years, in any case.
// FIXME: we have too many integer types.  make them type-distinct.
typedef uint32_t element_soul;

const element_soul the_null_soul = 0;
const uint32_t first_element_soul = 1;

inline bool null_soul(element_soul es)
{
  return es = the_null_soul;
}

struct element_t
{
  bool is_dir;
  revision_id birth_revision;
  // this is null iff this is a root dir
  element_soul parent;
  // this is null iff this is a root dir
  path_component name;
  file_id content;
  std::map<attr_key, attr_value> attrs;
  virtual ~element() {};
};

struct mark_item
{
  std::set<revision_id> name_marks;
  std::set<revision_id> content_marks;
  std::map<attr_key, std::set<revision_id> > attr_marks;
};

typedef std::map<element_soul, element_t> element_map;
typedef std::map<path_component, element_soul> dir_map;

// FIXME: should we just make the root_dir always have the null soul for now?
struct roster_t
{
  roster_t() : root_dir(the_null_element) {}
  // might be better to make this destructive -- eat the element_map given...
  roster_t(element_map const & elements);
  element_map elements;
  std::map<element_soul, dir_map> children;
  element_soul root_dir;
  void clear()
  {
    elements.clear(); children.clear(); root_dir = the_null_element;
  }
};

typedef std::map<element_soul, mark_item> marking_t;

element_soul lookup(roster_t const & roster, file_path const & fp);
void get_name(roster_t const & roster, element_soul es, file_path & fp);

// FIXME: how should this work?
void apply_change_set(change_set const & cs, element_map & em);

void markup(revision_id const & rid, roster_t const & root,
            marking_t & marking);
void markup(revision_id const & rid, roster_t const & child,
            revision_id const & parent_rid, roster_t const & parent,
            marking_t & marking);
void markup(revision_id const & rid, roster_t const & root,
            revision_id const & parent1_rid, roster_t const & parent1,
            revision_id const & parent2_rid, roster_t const & parent2,
            marking_t & marking);

void read_roster(data const & dat, roster_t & roster, marking_t & marking);
void write_roster(roster_t const & roster, marking_t const & marking, data & dat);
void write_manifest(roster_t const & roster, data & dat);

#endif  // header guard
