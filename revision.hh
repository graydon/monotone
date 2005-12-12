#ifndef __REVISION_HH__
#define __REVISION_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <string>

#include <boost/shared_ptr.hpp>

#include "app_state.hh"
#include "cset.hh"
#include "vocab.hh"

// a revision is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// new_manifest [16afa28e8783987223993d67f54700f0ecfedfaa]
//
// old_revision [d023242b16cbdfd46686a5d217af14e3c339f2b4]
// old_manifest [2dc4a99e27a0026395fbd4226103614928c55c77]
//
// delete_file "deleted-file.cc"
//
// rename_file "old-file.cc"
//          to "new-file.cc"
//
// add_file "added-file.cc"
//
// patch "added-file.cc"
//  from []
//    to [da39a3ee5e6b4b0d3255bfef95601890afd80709]
//
// patch "changed-file.cc"
//  from [588fd8a7bcde43a46f0bde1dd1d13e9e77cf25a1]
//    to [559133b166c3154c864f912e9f9452bfc452dfdd]
//
// patch "new-file.cc"
//  from [95b50ede90037557fd0fbbfad6a9fdd67b0bf413]
//    to [bd39086b9da776fc22abd45734836e8afb59c8c0]

typedef std::map<revision_id, boost::shared_ptr<cset> >
edge_map;

typedef edge_map::value_type
edge_entry;

struct 
revision_set
{
  void check_sane() const;
  bool is_merge_node() const;
  // trivial revisions are ones that have no effect -- e.g., commit should
  // refuse to commit them, saying that there are no changes to commit.
  bool is_nontrivial() const;
  revision_set() {}
  revision_set(revision_set const & other);
  revision_set const & operator=(revision_set const & other);
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

inline cset const & 
edge_changes(edge_entry const & e) 
{ 
  return *(e.second); 
}

inline cset const & 
edge_changes(edge_map::const_iterator i) 
{ 
  return *(i->second); 
}

void
dump(revision_set const & rev, std::string & out);

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

// sanity checking

void
find_common_ancestor_for_merge(revision_id const & left,
                               revision_id const & right,
                               revision_id & anc,
                               app_state & app);

bool
is_ancestor(revision_id const & ancestor,
            revision_id const & descendent,
            app_state & app);

void
toposort(std::set<revision_id> const & revisions,
         std::vector<revision_id> & sorted,
         app_state & app);

void
erase_ancestors(std::set<revision_id> & revisions, app_state & app);

void
ancestry_difference(revision_id const & a, std::set<revision_id> const & bs,
                    std::set<revision_id> & new_stuff,
                    app_state & app);


// FIXME: can probably optimize this passing a lookaside cache of the active 
// frontier set of shared_ptr<roster_t>s, while traversing history.
void
select_nodes_modified_by_rev(revision_id const & rid,
                             revision_set const & rev,
                             std::set<node_id> & nodes_changed,
                             std::set<node_id> & nodes_born,
                             app_state & app);

/*
void 
calculate_composite_cset(revision_id const & ancestor,
                         revision_id const & child,
                         app_state & app,
                         cset & composed);

void
calculate_arbitrary_cset(revision_id const & start,
                         revision_id const & end,
                         app_state & app,
                         cset & composed);

*/

void 
build_changesets_from_manifest_ancestry(app_state & app);

void 
build_roster_style_revs_from_manifest_style_revs(app_state & app);

// basic_io access to printers and parsers

namespace basic_io { struct printer; struct parser; }

void 
print_revision(basic_io::printer & printer,
               revision_set const & rev);

void 
parse_revision(basic_io::parser & parser,
               revision_set & rev);

void 
print_edge(basic_io::printer & printer,
           edge_entry const & e);

void 
parse_edge(basic_io::parser & parser,
           edge_map & es);

#endif // __REVISION_HH__
