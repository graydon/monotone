#ifndef __REVISION_HH__
#define __REVISION_HH__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <set>
#include <map>

#include <boost/shared_ptr.hpp>

#include "cset.hh"
#include "vocab.hh"
#include "database.hh"
#include "commands.hh"

// a revision is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// new_manifest [16afa28e8783987223993d67f54700f0ecfedfaa]
//
// old_revision [d023242b16cbdfd46686a5d217af14e3c339f2b4]
//
// delete "deleted-file.cc"
//
// rename "old-file.cc"
//     to "new-file.cc"
//
// add_file "added-file.cc"
//  content [da39a3ee5e6b4b0d3255bfef95601890afd80709]
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

enum made_for { made_for_nobody, made_for_workspace, made_for_database };

struct
revision_t
{
  void check_sane() const;
  bool is_merge_node() const;
  // trivial revisions are ones that have no effect -- e.g., commit should
  // refuse to commit them, saying that there are no changes to commit.
  bool is_nontrivial() const;
  revision_t() : made_for(made_for_nobody) {}
  revision_t(revision_t const & other);
  revision_t const & operator=(revision_t const & other);
  manifest_id new_manifest;
  edge_map edges;
  // workspace::put_work_rev refuses to apply a rev that doesn't have this
  // set to "workspace", and database::put_revision refuses to apply a rev
  // that doesn't have it set to "database".  the default constructor sets
  // it to "nobody".
  enum made_for made_for;
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

template <> void
dump(revision_t const & rev, std::string & out);

void
read_revision(data const & dat,
              revision_t & rev);

void
read_revision(revision_data const & dat,
              revision_t & rev);

void
write_revision(revision_t const & rev,
               data & dat);

void
write_revision(revision_t const & rev,
               revision_data & dat);

void calculate_ident(revision_t const & cs,
                     revision_id & ident);

// sanity checking

void
find_common_ancestor_for_merge(revision_id const & left,
                               revision_id const & right,
                               revision_id & anc,
                               database & db);

bool
is_ancestor(revision_id const & ancestor,
            revision_id const & descendent,
            database & db);

void
toposort(std::set<revision_id> const & revisions,
         std::vector<revision_id> & sorted,
         database & db);

void
erase_ancestors(std::set<revision_id> & revisions, database & db);

struct is_failure
{
  virtual bool operator()(revision_id const & rid) = 0;
  virtual ~is_failure() {};
};
void
erase_ancestors_and_failures(std::set<revision_id> & revisions,
                             is_failure & p,
                             database & db,
                             std::multimap<revision_id, revision_id> *inverse_graph_cache_ptr = NULL);

void
ancestry_difference(revision_id const & a, std::set<revision_id> const & bs,
                    std::set<revision_id> & new_stuff,
                    database & db);


// FIXME: can probably optimize this passing a lookaside cache of the active
// frontier set of shared_ptr<roster_t>s, while traversing history.
void
select_nodes_modified_by_rev(revision_t const & rev,
                             roster_t const roster,
                             std::set<node_id> & nodes_modified,
                             database & db);

void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              roster_t const & new_roster,
              revision_t & rev);

void
make_revision(parent_map const & old_rosters,
              roster_t const & new_roster,
              revision_t & rev);

// This overload takes a base roster and a changeset instead.
void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              cset const & changes,
              revision_t & rev);

// These functions produce a faked "new_manifest" id and discard all
// content-only changes from the cset.  They are only to be used to
// construct a revision that will be written to the workspace.  Don't use
// them for revisions written to the database or presented to the user.
void
make_revision_for_workspace(revision_id const & old_rev_id,
                            cset const & changes,
                            revision_t & rev);

void
make_revision_for_workspace(revision_id const & old_rev_id,
                            roster_t const & old_roster,
                            roster_t const & new_roster,
                            revision_t & rev);

void
make_revision_for_workspace(parent_map const & old_rosters,
                            roster_t const & new_roster,
                            revision_t & rev);

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev);

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev,
                         cset & excluded,
                         commands::command_id const & cmd_name);

void
build_changesets_from_manifest_ancestry(database & db,
                                        std::set<std::string> const & attrs_to_drop);

void
build_roster_style_revs_from_manifest_style_revs(database & db,
                                                 std::set<std::string> const & attrs_to_drop);

void
regenerate_caches(database & db);

// basic_io access to printers and parsers

namespace basic_io { struct printer; struct parser; }

void
print_revision(basic_io::printer & printer,
               revision_t const & rev);

void
parse_revision(basic_io::parser & parser,
               revision_t & rev);

void
print_edge(basic_io::printer & printer,
           edge_entry const & e);

void
parse_edge(basic_io::parser & parser,
           edge_map & es);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __REVISION_HH__
