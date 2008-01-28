#ifndef __GRAPH__HH__
#define __GRAPH__HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains generic graph algorithms.  They are split out from any
// particular concrete graph (e.g., the revision graph, the delta storage
// graphs) to easy re-use, and to make them easier to test on their own.  We
// have a number of graph algorithms that are not genericized in this way
// (e.g., in revision.cc); FIXME it would be good to move them in here as
// opportunity permits.

#include <set>
#include "vector.hh"
#include <utility>

#include "vocab.hh"
#include "rev_height.hh"

struct reconstruction_graph
{
  virtual bool is_base(hexenc<id> const & node) const = 0;
  virtual void get_next(hexenc<id> const & from, std::set< hexenc<id> > & next) const = 0;
  virtual ~reconstruction_graph() {};
};

typedef std::vector< hexenc<id> > reconstruction_path;

void
get_reconstruction_path(hexenc<id> const & start,
                        reconstruction_graph const & graph,
                        reconstruction_path & path);

typedef std::multimap<revision_id, revision_id> rev_ancestry_map;

void toposort_rev_ancestry(rev_ancestry_map const & graph,
                          std::vector<revision_id> & revisions);

struct rev_graph
{
  virtual void get_parents(revision_id const & rev, std::set<revision_id> & parents) const = 0;
  virtual void get_children(revision_id const & rev, std::set<revision_id> & children) const = 0;
  virtual void get_height(revision_id const & rev, rev_height & h) const = 0;
  virtual ~rev_graph() {};
};

void
get_uncommon_ancestors(revision_id const & a,
                       revision_id const & b,
                       rev_graph const & hg,
                       std::set<revision_id> & a_uncommon_ancs,
                       std::set<revision_id> & b_uncommon_ancs);
                       


#endif // __GRAPH__HH__


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

