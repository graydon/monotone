#ifndef __ROSTER_HH__
#define __ROSTER_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <map>

#include "vocab.hh"
#include "numeric_vocab.hh"
#include "paths.hh"
#include "changeset.hh"
#include "app_state.hh"

// a persistent element id
// but "file_id" means something else...
// "element" terminology is stolen from clearcase, it means (file|directory)
// 32 bits should be sufficient; even with half of them temporary, 2 billion
// distinct files would use 2 terabytes of disk space, assuming each file
// requires only a single sqlite page.  easy to change in a few years, in any
// case.
// FIXME: we have too many integer types.  make them type-distinct.
typedef uint32_t esoul;

const esoul the_null_soul = 0;
const uint32_t first_esoul = 1;

inline bool null_soul(esoul es)
{
  return es = the_null_soul;
}

const esoul first_temp_soul = 0x80000000;

inline bool temp_soul(esoul es)
{
  return es & first_temp_soul;
}

// returns either temp or real souls
struct soul_source
{
  virtual esoul next() = 0;
};

struct temp_soul_source : soul_source
{
  temp_soul_source() : curr(first_temp_soul) {}
  esoul next()
  {
    esoul r = curr++;
    I(temp_soul(r));
    return r;
  }
  esoul curr;
};
  
///////////////////////////////////////////////////////////////////

typedef enum { etype_dir, etype_file } etype;

struct element_t
{
  etype type;
  revision_id birth_revision;
  // this is null iff this is a root dir
  esoul parent;
  // this is null iff this is a root dir
  path_component name;
  file_id content;
  std::map<attr_key, attr_value> attrs;
  virtual ~element() {};
};


// FIXME: move this to paths.hh
typedef std::vector<path_component> split_path;
typedef std::map<path_component, element_soul> dir_map;

class roster_t
{
public:
  roster_t() : root_dir(the_null_soul) {}
  esoul lookup(file_path const & fp) const;
  esoul lookup(split_path const & sp) const;
  esoul lookup(esoul parent, path_component child) const;
  void get_name(esoul es, file_path & fp) const;
  void get_name(esoul es, split_path & sp) const;
  dir_map & children(esoul es);
  element_t & element(esoul es);
  void resoul(esoul from, esoul to);
  void remove(esoul es);
  void add(esoul es, split_path const & sp, element_t const & element);
  void apply_changeset(changeset const & cs, soul_source & ss,
                       revision_id const & new_id,
                       // these are mutually exclusive sets
                       // the new_souls souls all come from the given
                       // soul_source
                       std::set<esoul> & new_souls,
                       std::set<esoul> & touched_souls);
private:
  // sets parent to the parent attached to, sets name to the name given
  void attach(esoul es, split_path const & sp);
  // sets parent to the_null_soul, name to the_null_component
  void detach(esoul es);
  std::map<esoul, element_t> elements;
  std::map<esoul, dir_map> children_map;
  esoul root_dir;
};

// FIXME: add marking stuff here
void roster_for_revision(revision_id const & rid, revision_set const & rev,
                         roster_t & r,
                         app_state & app);
