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
// 32 bits should be sufficient; even with half of them temporary, 2 billion
// distinct files would use 2 terabytes of disk space, assuming each file
// requires only a single sqlite page.  easy to change in a few years, in any
// case.
// FIXME: we have too many integer types.  make them type-distinct.
typedef uint32_t element_soul;

const element_soul the_null_soul = 0;
const uint32_t first_element_soul = 1;

inline bool null_soul(element_soul es)
{
  return es = the_null_soul;
}

const element_soul first_temp_soul = 0x80000000;

inline bool temp_soul(element_soul es)
{
  return es & first_temp_soul;
}

struct temp_soul_source
{
  temp_soul_source() : curr(first_temp_soul) {}
  element_soul next()
  {
    element_soul r = curr++;
    I(temp_soul(r));
    return r;
  }
  element_soul curr;
};
  
typedef enum { etype_dir, etype_file } etype;

struct element_t
{
  etype type;
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

typedef std::vector<path_component> split_path;

class dir_tree
{
public:
  dir_tree() : root_dir(the_null_soul) {};
  dir_tree(element_map const & elements) {};
  element_soul lookup(file_path const & fp);
  element_soul lookup(split_path const & sp);
  element_soul lookup(element_soul parent, path_component child);
  // returns the soul of the removed element
  element_soul detach(split_path const & sp);
  void attach(split_path const & sp, element_soul es, etype type);
  void attach(element_soul parent, element_soul es, etype type);
private:
  std::map<element_soul, dir_map> children;
  element_soul root_dir;
};

// FIXME: should we just make the root_dir always have the null soul for now?
struct roster_t
{
  roster_t() : root_dir(the_null_soul) {}
  // might be better to make this destructive -- eat the element_map given...
  roster_t(element_map const & elements)
    : elements(elements), tree(elements)
    {}
  element_t & element(element_soul es);
  void assert_type(element_soul es, etype type);
  element_map elements;
  dir_tree tree;
};

typedef std::map<element_soul, mark_item> marking_t;

element_soul lookup(roster_t const & roster, file_path const & fp);
element_soul lookup(roster_t const & roster,
                    std::vector<path_component> const & path);
void get_name(roster_t const & roster, element_soul es, file_path & fp);

// This generates a roster containing temp souls
void apply_change_set(change_set const & cs, roster_t & roster,
                      temp_soul_source & tss);

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
