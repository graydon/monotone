// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <cctype>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <iterator>
#include <functional>
#include <list>

#include "lexical_cast.hh"
#include <boost/dynamic_bitset.hpp>
#include <boost/shared_ptr.hpp>

#include "botan/botan.h"

#include "app_state.hh"
#include "basic_io.hh"
#include "cert.hh"
#include "cset.hh"
#include "constants.hh"
#include "interner.hh"
#include "keys.hh"
#include "numeric_vocab.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "ui.hh"
#include "vocab.hh"
#include "safe_map.hh"
#include "legacy.hh"
#include "rev_height.hh"
#include "cmd.hh"

using std::back_inserter;
using std::copy;
using std::deque;
using std::list;
using std::make_pair;
using std::map;
using std::max;
using std::multimap;
using std::ostringstream;
using std::pair;
using std::queue;
using std::set;
using std::stack;
using std::string;
using std::vector;

using boost::dynamic_bitset;
using boost::shared_ptr;

void revision_t::check_sane() const
{
  // null id in current manifest only permitted if previous
  // state was null and no changes
  // FIXME: above comment makes no sense.  This should just be
  // I(!null_id(new_manifest)), and the only reason I am not making it so
  // right now is that I don't have time to immediately track down all the
  // fallout.
  if (null_id(new_manifest))
    {
      for (edge_map::const_iterator i = edges.begin();
           i != edges.end(); ++i)
        I(null_id(edge_old_revision(i)));
    }

  if (edges.size() == 1)
    {
      // no particular checks to be done right now
    }
  else if (edges.size() == 2)
    {
      // merge nodes cannot have null revisions
      for (edge_map::const_iterator i = edges.begin(); i != edges.end(); ++i)
        I(!null_id(edge_old_revision(i)));
    }
  else
    // revisions must always have either 1 or 2 edges
    I(false);

  // we used to also check that if there were multiple edges that had patches
  // for the same file, then the new hashes on each edge matched each other.
  // this is not ported over to roster-style revisions because it's an
  // inadequate check, and the real check, that the new manifest id is correct
  // (done in put_revision, for instance) covers this case automatically.
}

bool
revision_t::is_merge_node() const
{
  return edges.size() > 1;
}

bool
revision_t::is_nontrivial() const
{
  check_sane();
  // merge revisions are never trivial, because even if the resulting node
  // happens to be identical to both parents, the merge is still recording
  // that fact.
  if (is_merge_node())
    return true;
  else
    return !edge_changes(edges.begin()).empty();
}

revision_t::revision_t(revision_t const & other)
{
  /* behave like normal constructor if other is empty */
  made_for = made_for_nobody;
  if (null_id(other.new_manifest) && other.edges.empty()) return;
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  made_for = other.made_for;
}

revision_t const &
revision_t::operator=(revision_t const & other)
{
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  made_for = other.made_for;
  return *this;
}


// For a surprisingly long time, we have been using an algorithm which
// is nonsense, based on a misunderstanding of what "LCA" means. The
// LCA of two nodes is *not* the first common ancestor which you find
// when iteratively expanding their ancestor sets. Instead, the LCA is
// the common ancestor which is a descendent of all other common
// ancestors.
//
// In general, a set of nodes in a DAG doesn't always have an
// LCA. There might be multiple common ancestors which are not parents
// of one another. So we implement something which is "functionally
// useful" for finding a merge point (and moreover, which always
// terminates): we find an LCA of the input set if it exists,
// otherwise we replace the input set with the nodes we did find and
// repeat.
//
// All previous discussions in monotone-land, before say August 2005,
// of LCA (and LCAD) are essentially wrong due to our silly
// misunderstanding. It's unfortunate, but our half-baked
// approximations worked almost well enough to take us through 3 years
// of deployed use. Hopefully this more accurate new use will serve us
// even longer.

typedef unsigned long ctx;
typedef dynamic_bitset<> bitmap;
typedef shared_ptr<bitmap> shared_bitmap;

static void
calculate_ancestors_from_graph(interner<ctx> & intern,
                               revision_id const & init,
                               multimap<revision_id, revision_id> const & graph,
                               map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union);

void
find_common_ancestor_for_merge(revision_id const & left,
                               revision_id const & right,
                               revision_id & anc,
                               app_state & app)
{
  interner<ctx> intern;
  set<ctx> leaves;
  map<ctx, shared_bitmap> ancestors;

  shared_bitmap isect = shared_bitmap(new bitmap());
  shared_bitmap isect_ancs = shared_bitmap(new bitmap());

  leaves.insert(intern.intern(left.inner()()));
  leaves.insert(intern.intern(right.inner()()));


  multimap<revision_id, revision_id> inverse_graph;
  {
    multimap<revision_id, revision_id> graph;
    app.db.get_revision_ancestry(graph);
    typedef multimap<revision_id, revision_id>::const_iterator gi;
    for (gi i = graph.begin(); i != graph.end(); ++i)
      inverse_graph.insert(make_pair(i->second, i->first));
  }


  while (leaves.size() != 1)
    {
      isect->clear();
      isect_ancs->clear();

      // First intersect all ancestors of current leaf set
      for (set<ctx>::const_iterator i = leaves.begin(); i != leaves.end(); ++i)
        {
          ctx curr_leaf = *i;
          shared_bitmap curr_leaf_ancestors;
          map<ctx, shared_bitmap >::const_iterator j = ancestors.find(*i);
          if (j != ancestors.end())
            curr_leaf_ancestors = j->second;
          else
            {
              curr_leaf_ancestors = shared_bitmap(new bitmap());
              calculate_ancestors_from_graph(intern, revision_id(intern.lookup(curr_leaf)),
                                             inverse_graph, ancestors,
                                             curr_leaf_ancestors);
            }
          if (isect->size() > curr_leaf_ancestors->size())
            curr_leaf_ancestors->resize(isect->size());

          if (curr_leaf_ancestors->size() > isect->size())
            isect->resize(curr_leaf_ancestors->size());

          if (i == leaves.begin())
            *isect = *curr_leaf_ancestors;
          else
            (*isect) &= (*curr_leaf_ancestors);
        }

      // isect is now the set of common ancestors of leaves, but that is not enough.
      // We need the set of leaves of isect; to do that we calculate the set of
      // ancestors of isect, in order to subtract it from isect (below).
      set<ctx> new_leaves;
      for (ctx i = 0; i < isect->size(); ++i)
        {
          if (isect->test(i))
            {
              calculate_ancestors_from_graph(intern, revision_id(intern.lookup(i)),
                                             inverse_graph, ancestors, isect_ancs);
            }
        }

      // Finally, the subtraction step: for any element i of isect, if
      // it's *not* in isect_ancs, it survives as a new leaf.
      leaves.clear();
      for (ctx i = 0; i < isect->size(); ++i)
        {
          if (!isect->test(i))
            continue;
          if (i < isect_ancs->size() && isect_ancs->test(i))
            continue;
          safe_insert(leaves, i);
        }
    }

  I(leaves.size() == 1);
  anc = revision_id(intern.lookup(*leaves.begin()));
}

// FIXME: this algorithm is incredibly inefficient; it's O(n) where n is the
// size of the entire revision graph.

template<typename T> static bool
is_ancestor(T const & ancestor_id,
            T const & descendent_id,
            multimap<T, T> const & graph)
{

  set<T> visited;
  queue<T> queue;

  queue.push(ancestor_id);

  while (!queue.empty())
    {
      T current_id = queue.front();
      queue.pop();

      if (current_id == descendent_id)
        return true;
      else
        {
          typedef typename multimap<T, T>::const_iterator gi;
          pair<gi, gi> children = graph.equal_range(current_id);
          for (gi i = children.first; i != children.second; ++i)
            {
              if (visited.find(i->second) == visited.end())
                {
                  queue.push(i->second);
                  visited.insert(i->second);
                }
            }
        }
    }
  return false;
}

bool
is_ancestor(revision_id const & ancestor_id,
            revision_id const & descendent_id,
            app_state & app)
{
  L(FL("checking whether %s is an ancestor of %s") % ancestor_id % descendent_id);

  multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  return is_ancestor(ancestor_id, descendent_id, graph);
}


static void
add_bitset_to_union(shared_bitmap src,
                    shared_bitmap dst)
{
  if (dst->size() > src->size())
    src->resize(dst->size());
  if (src->size() > dst->size())
    dst->resize(src->size());
  *dst |= *src;
}


static void
calculate_ancestors_from_graph(interner<ctx> & intern,
                               revision_id const & init,
                               multimap<revision_id, revision_id> const & graph,
                               map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  stack<ctx> stk;

  stk.push(intern.intern(init.inner()()));

  while (! stk.empty())
    {
      ctx us = stk.top();
      revision_id rev(hexenc<id>(intern.lookup(us)));

      pair<gi,gi> parents = graph.equal_range(rev);
      bool pushed = false;

      // first make sure all parents are done
      for (gi i = parents.first; i != parents.second; ++i)
        {
          ctx parent = intern.intern(i->second.inner()());
          if (ancestors.find(parent) == ancestors.end())
            {
              stk.push(parent);
              pushed = true;
              break;
            }
        }

      // if we pushed anything we stop now. we'll come back later when all
      // the parents are done.
      if (pushed)
        continue;

      shared_bitmap b = shared_bitmap(new bitmap());

      for (gi i = parents.first; i != parents.second; ++i)
        {
          ctx parent = intern.intern(i->second.inner()());

          // set all parents
          if (b->size() <= parent)
            b->resize(parent + 1);
          b->set(parent);

          // ensure all parents are loaded into the ancestor map
          I(ancestors.find(parent) != ancestors.end());

          // union them into our map
          map< ctx, shared_bitmap >::const_iterator j = ancestors.find(parent);
          I(j != ancestors.end());
          add_bitset_to_union(j->second, b);
        }

      add_bitset_to_union(b, total_union);
      ancestors.insert(make_pair(us, b));
      stk.pop();
    }
}

void
toposort(set<revision_id> const & revisions,
         vector<revision_id> & sorted,
         app_state & app)
{
  map<rev_height, revision_id> work;

  for (set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i) 
    {
      rev_height height;
      app.db.get_rev_height(*i, height);
      work.insert(make_pair(height, *i));
    }

  sorted.clear();
  
  for (map<rev_height, revision_id>::const_iterator i = work.begin();
       i != work.end(); ++i)
    {
      sorted.push_back(i->second);
    }
}

static void
accumulate_strict_ancestors(revision_id const & start,
                            set<revision_id> & all_ancestors,
                            multimap<revision_id, revision_id> const & inverse_graph,
                            app_state & app,
                            rev_height const & min_height)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;

  vector<revision_id> frontier;
  frontier.push_back(start);

  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      pair<gi, gi> parents = inverse_graph.equal_range(rid);
      for (gi i = parents.first; i != parents.second; ++i)
        {
          revision_id const & parent = i->second;
          if (all_ancestors.find(parent) == all_ancestors.end())
            {
              // prune if we're below min_height
              rev_height h;
              app.db.get_rev_height(parent, h);
              if (h >= min_height)
                {
                  all_ancestors.insert(parent);
                  frontier.push_back(parent);
                }
            }
        }
    }
}

// this call is equivalent to running:
//   erase(remove_if(candidates.begin(), candidates.end(), p));
//   erase_ancestors(candidates, app);
// however, by interleaving the two operations, it can in common cases make
// many fewer calls to the predicate, which can be a significant speed win.

void
erase_ancestors_and_failures(std::set<revision_id> & candidates,
                             is_failure & p,
                             app_state & app,
                             multimap<revision_id, revision_id> *inverse_graph_cache_ptr)
{
  // Load up the ancestry graph
  multimap<revision_id, revision_id> inverse_graph;
  
  if (candidates.empty())
    return;
  
  if (inverse_graph_cache_ptr == NULL)
    inverse_graph_cache_ptr = &inverse_graph;
  if (inverse_graph_cache_ptr->empty())
  {
    multimap<revision_id, revision_id> graph;
    app.db.get_revision_ancestry(graph);
    for (multimap<revision_id, revision_id>::const_iterator i = graph.begin();
         i != graph.end(); ++i)
      inverse_graph_cache_ptr->insert(make_pair(i->second, i->first));
  }

  // Keep a set of all ancestors that we've traversed -- to avoid
  // combinatorial explosion.
  set<revision_id> all_ancestors;

  rev_height min_height;
  app.db.get_rev_height(*candidates.begin(), min_height);
  for (std::set<revision_id>::const_iterator it = candidates.begin(); it != candidates.end(); it++)
    {
      rev_height h;
      app.db.get_rev_height(*it, h);
      if (h < min_height)
        min_height = h;
    }

  vector<revision_id> todo(candidates.begin(), candidates.end());
  std::random_shuffle(todo.begin(), todo.end());

  size_t predicates = 0;
  while (!todo.empty())
    {
      revision_id rid = todo.back();
      todo.pop_back();
      // check if this one has already been eliminated
      if (all_ancestors.find(rid) != all_ancestors.end())
        continue;
      // and then whether it actually should stay in the running:
      ++predicates;
      if (p(rid))
        {
          candidates.erase(rid);
          continue;
        }
      // okay, it is good enough that all its ancestors should be
      // eliminated
      accumulate_strict_ancestors(rid, all_ancestors, *inverse_graph_cache_ptr, app, min_height);
    }

  // now go and eliminate the ancestors
  for (set<revision_id>::const_iterator i = all_ancestors.begin();
       i != all_ancestors.end(); ++i)
    candidates.erase(*i);

  L(FL("called predicate %s times") % predicates);
}

// This function looks at a set of revisions, and for every pair A, B in that
// set such that A is an ancestor of B, it erases A.

namespace
{
  struct no_failures : public is_failure
  {
    virtual bool operator()(revision_id const & rid)
    {
      return false;
    }
  };
}
void
erase_ancestors(set<revision_id> & revisions, app_state & app)
{
  no_failures p;
  erase_ancestors_and_failures(revisions, p, app);
}

// This function takes a revision A and a set of revision Bs, calculates the
// ancestry of each, and returns the set of revisions that are in A's ancestry
// but not in the ancestry of any of the Bs.  It tells you 'what's new' in A
// that's not in the Bs.  If the output set if non-empty, then A will
// certainly be in it; but the output set might be empty.
void
ancestry_difference(revision_id const & a, set<revision_id> const & bs,
                    set<revision_id> & new_stuff,
                    app_state & app)
{
  new_stuff.clear();
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  multimap<revision_id, revision_id> graph;
  multimap<revision_id, revision_id> inverse_graph;

  app.db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(make_pair(i->second, i->first));

  interner<ctx> intern;
  map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (set<revision_id>::const_iterator i = bs.begin();
       i != bs.end(); ++i)
    {
      calculate_ancestors_from_graph(intern, *i, inverse_graph, ancestors, u);
      ctx c = intern.intern(i->inner()());
      if (u->size() <= c)
        u->resize(c + 1);
      u->set(c);
    }

  shared_bitmap au = shared_bitmap(new bitmap());
  calculate_ancestors_from_graph(intern, a, inverse_graph, ancestors, au);
  {
    ctx c = intern.intern(a.inner()());
    if (au->size() <= c)
      au->resize(c + 1);
    au->set(c);
  }

  au->resize(max(au->size(), u->size()));
  u->resize(max(au->size(), u->size()));

  *au -= *u;

  for (unsigned int i = 0; i != au->size(); ++i)
  {
    if (au->test(i))
      {
        revision_id rid(intern.lookup(i));
        if (!null_id(rid))
          new_stuff.insert(rid);
      }
  }
}

void
select_nodes_modified_by_rev(revision_t const & rev,
                             roster_t const new_roster,
                             set<node_id> & nodes_modified,
                             app_state & app)
{
  nodes_modified.clear();

  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); ++i)
    {
      set<node_id> edge_nodes_modified;
      roster_t old_roster;
      app.db.get_roster(edge_old_revision(i), old_roster);
      select_nodes_modified_by_cset(edge_changes(i),
                                    old_roster,
                                    new_roster,
                                    edge_nodes_modified);

      copy(edge_nodes_modified.begin(), edge_nodes_modified.end(),
                inserter(nodes_modified, nodes_modified.begin()));
    }
}


void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              roster_t const & new_roster,
              revision_t & rev)
{
  shared_ptr<cset> cs(new cset());

  rev.edges.clear();
  make_cset(old_roster, new_roster, *cs);

  calculate_ident(new_roster, rev.new_manifest);
  L(FL("new manifest_id is %s") % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;
}

void
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              cset const & changes,
              revision_t & rev)
{
  roster_t new_roster = old_roster;
  {
    temp_node_id_source nis;
    editable_roster_base er(new_roster, nis);
    changes.apply_to(er);
  }

  shared_ptr<cset> cs(new cset(changes));
  rev.edges.clear();

  calculate_ident(new_roster, rev.new_manifest);
  L(FL("new manifest_id is %s") % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;
}

void
make_revision(parent_map const & old_rosters,
              roster_t const & new_roster,
              revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs(new cset());
      make_cset(parent_roster(i), new_roster, *cs);
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  rev.edges = edges;
  calculate_ident(new_roster, rev.new_manifest);
  L(FL("new manifest_id is %s") % rev.new_manifest);
}

static void
recalculate_manifest_id_for_restricted_rev(parent_map const & old_rosters,
                                           edge_map & edges,
                                           revision_t & rev)
{
  // In order to get the correct manifest ID, recalculate the new roster
  // using one of the restricted csets.  It doesn't matter which of the
  // parent roster/cset pairs we use for this; by construction, they must
  // all produce the same result.
  revision_id id = parent_id(old_rosters.begin());
  roster_t restricted_roster = *(safe_get(old_rosters, id).first);

  temp_node_id_source nis;
  editable_roster_base er(restricted_roster, nis);
  safe_get(edges, id)->apply_to(er);

  calculate_ident(restricted_roster, rev.new_manifest);
  rev.edges = edges;
  L(FL("new manifest_id is %s") % rev.new_manifest);
}

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> included(new cset());
      roster_t restricted_roster;

      make_restricted_roster(parent_roster(i), new_roster, 
                             restricted_roster, mask);
      make_cset(parent_roster(i), restricted_roster, *included);
      safe_insert(edges, make_pair(parent_id(i), included));
    }

  recalculate_manifest_id_for_restricted_rev(old_rosters, edges, rev);
}

void
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         revision_t & rev,
                         cset & excluded,
                         commands::command_id const & cmd_name)
{
  edge_map edges;
  bool no_excludes = true;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> included(new cset());
      roster_t restricted_roster;

      make_restricted_roster(parent_roster(i), new_roster, 
                             restricted_roster, mask);
      make_cset(parent_roster(i), restricted_roster, *included);
      make_cset(restricted_roster, new_roster, excluded);
      safe_insert(edges, make_pair(parent_id(i), included));
      if (!excluded.empty())
        no_excludes = false;
    }

  N(old_rosters.size() == 1 || no_excludes,
    F("the command '%s %s' cannot be restricted in a two-parent workspace")
    % ui.prog_name % join_words(cmd_name)());

  recalculate_manifest_id_for_restricted_rev(old_rosters, edges, rev);
}

// Workspace-only revisions, with fake rev.new_manifest and content
// changes suppressed.
void
make_revision_for_workspace(revision_id const & old_rev_id,
                            cset const & changes,
                            revision_t & rev)
{
  MM(old_rev_id);
  MM(changes);
  MM(rev);
  shared_ptr<cset> cs(new cset(changes));
  cs->deltas_applied.clear();

  rev.edges.clear();
  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  if (!null_id(old_rev_id))
    rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
}

void
make_revision_for_workspace(revision_id const & old_rev_id,
                            roster_t const & old_roster,
                            roster_t const & new_roster,
                            revision_t & rev)
{
  MM(old_rev_id);
  MM(old_roster);
  MM(new_roster);
  MM(rev);
  cset changes;
  make_cset(old_roster, new_roster, changes);
  make_revision_for_workspace(old_rev_id, changes, rev);
}

void
make_revision_for_workspace(parent_map const & old_rosters,
                            roster_t const & new_roster,
                            revision_t & rev)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs(new cset());
      make_cset(parent_roster(i), new_roster, *cs);
      cs->deltas_applied.clear();
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  rev.edges = edges;
  rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
}


// Stuff related to rebuilding the revision graph. Unfortunately this is a
// real enough error case that we need support code for it.

typedef map<u64, pair<shared_ptr<roster_t>, shared_ptr<marking_map> > >
parent_roster_map;

template <> void
dump(parent_roster_map const & prm, string & out)
{
  ostringstream oss;
  for (parent_roster_map::const_iterator i = prm.begin(); i != prm.end(); ++i)
    {
      oss << "roster: " << i->first << '\n';
      string roster_str, indented_roster_str;
      dump(*i->second.first, roster_str);
      prefix_lines_with("    ", roster_str, indented_roster_str);
      oss << indented_roster_str;
      oss << "\nroster's marking:\n";
      string marking_str, indented_marking_str;
      dump(*i->second.second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str;
      oss << "\n\n";
    }
  out = oss.str();
}

struct anc_graph
{
  anc_graph(bool existing, app_state & a) :
    existing_graph(existing),
    app(a),
    max_node(0),
    n_nodes("nodes", "n", 1),
    n_certs_in("certs in", "c", 1),
    n_revs_out("revs out", "r", 1),
    n_certs_out("certs out", "C", 1)
  {}

  bool existing_graph;
  app_state & app;
  u64 max_node;

  ticker n_nodes;
  ticker n_certs_in;
  ticker n_revs_out;
  ticker n_certs_out;

  map<u64,manifest_id> node_to_old_man;
  map<manifest_id,u64> old_man_to_node;

  map<u64,revision_id> node_to_old_rev;
  map<revision_id,u64> old_rev_to_node;

  map<u64,revision_id> node_to_new_rev;
  map<revision_id,u64> new_rev_to_node;

  map<u64, legacy::renames_map> node_to_renames;

  multimap<u64, pair<cert_name, cert_value> > certs;
  multimap<u64, u64> ancestry;
  set<string> branches;

  void add_node_ancestry(u64 child, u64 parent);
  void write_certs();
  void kluge_for_bogus_merge_edges();
  void rebuild_ancestry();
  void get_node_manifest(u64 node, manifest_id & man);
  u64 add_node_for_old_manifest(manifest_id const & man);
  u64 add_node_for_oldstyle_revision(revision_id const & rev);
  void construct_revisions_from_ancestry();
  void fixup_node_identities(parent_roster_map const & parent_rosters,
                             roster_t & child_roster,
                             legacy::renames_map const & renames);
};


void anc_graph::add_node_ancestry(u64 child, u64 parent)
{
  L(FL("noting ancestry from child %d -> parent %d") % child % parent);
  ancestry.insert(make_pair(child, parent));
}

void anc_graph::get_node_manifest(u64 node, manifest_id & man)
{
  map<u64,manifest_id>::const_iterator i = node_to_old_man.find(node);
  I(i != node_to_old_man.end());
  man = i->second;
}

void anc_graph::write_certs()
{
  {
    // regenerate epochs on all branches to random states

    for (set<string>::const_iterator i = branches.begin(); i != branches.end(); ++i)
      {
        char buf[constants::epochlen_bytes];
        Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf), constants::epochlen_bytes);
        hexenc<data> hexdata;
        encode_hexenc(data(string(buf, buf + constants::epochlen_bytes)), hexdata);
        epoch_data new_epoch(hexdata);
        L(FL("setting epoch for %s to %s") % *i % new_epoch);
        app.db.set_epoch(branch_name(*i), new_epoch);
      }
  }


  typedef multimap<u64, pair<cert_name, cert_value> >::const_iterator ci;

  for (map<u64,revision_id>::const_iterator i = node_to_new_rev.begin();
       i != node_to_new_rev.end(); ++i)
    {
      revision_id rev(i->second);

      pair<ci,ci> range = certs.equal_range(i->first);

      for (ci j = range.first; j != range.second; ++j)
        {
          cert_name name(j->second.first);
          cert_value val(j->second.second);

          cert new_cert;
          make_simple_cert(rev.inner(), name, val, app, new_cert);
          revision<cert> rcert(new_cert);
          if (app.db.put_revision_cert(rcert))
            ++n_certs_out;
        }
    }
}

void
anc_graph::kluge_for_bogus_merge_edges()
{
  // This kluge exists because in the 0.24-era monotone databases, several
  // bad merges still existed in which one side of the merge is an ancestor
  // of the other side of the merge. In other words, graphs which look like
  // this:
  //
  //  a ----------------------> e
  //   \                       /
  //    \---> b -> c -> d ----/
  //
  // Such merges confuse the roster-building algorithm, because they should
  // never have occurred in the first place: a was not a head at the time
  // of the merge, e should simply have been considered an extension of d.
  //
  // So... we drop the a->e edges entirely.
  //
  // Note: this kluge drops edges which are a struct superset of those
  // dropped by a previous kluge ("3-ancestor") so we have removed that
  // code.

  P(F("scanning for bogus merge edges"));

  multimap<u64,u64> parent_to_child_map;
    for (multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      parent_to_child_map.insert(make_pair(i->second, i->first));

  map<u64, u64> edges_to_kill;
  for (multimap<u64, u64>::const_iterator i = ancestry.begin();
       i != ancestry.end(); ++i)
    {
      multimap<u64, u64>::const_iterator j = i;
      ++j;
      u64 child = i->first;
      // NB: ancestry is a multimap from child->parent(s)
      if (j != ancestry.end())
        {
          if (j->first == i->first)
            {
              L(FL("considering old merge edge %s") %
                safe_get(node_to_old_rev, i->first));
              u64 parent1 = i->second;
              u64 parent2 = j->second;
              if (is_ancestor (parent1, parent2, parent_to_child_map))
                safe_insert(edges_to_kill, make_pair(child, parent1));
              else if (is_ancestor (parent2, parent1, parent_to_child_map))
                safe_insert(edges_to_kill, make_pair(child, parent2));
            }
        }
    }

  for (map<u64, u64>::const_iterator i = edges_to_kill.begin();
       i != edges_to_kill.end(); ++i)
    {
      u64 child = i->first;
      u64 parent = i->second;
      bool killed = false;
      for (multimap<u64, u64>::iterator j = ancestry.lower_bound(child);
           j->first == child; ++j)
        {
          if (j->second == parent)
            {
              P(F("optimizing out redundant edge %d -> %d")
                % parent % child);
              ancestry.erase(j);
              killed = true;
              break;
            }
        }

      if (!killed)
        W(F("failed to eliminate edge %d -> %d")
          % parent % child);
    }
}


void
anc_graph::rebuild_ancestry()
{
  kluge_for_bogus_merge_edges();

  P(F("rebuilding %d nodes") % max_node);
  {
    transaction_guard guard(app.db);
    if (existing_graph)
      app.db.delete_existing_revs_and_certs();
    construct_revisions_from_ancestry();
    write_certs();
    if (existing_graph)
      app.db.delete_existing_manifests();
    guard.commit();
  }
}

u64
anc_graph::add_node_for_old_manifest(manifest_id const & man)
{
  I(!existing_graph);
  u64 node = 0;
  if (old_man_to_node.find(man) == old_man_to_node.end())
    {
      node = max_node++;
      ++n_nodes;
      L(FL("node %d = manifest %s") % node % man);
      old_man_to_node.insert(make_pair(man, node));
      node_to_old_man.insert(make_pair(node, man));

      // load certs
      vector< manifest<cert> > mcerts;
      app.db.get_manifest_certs(man, mcerts);
      erase_bogus_certs(mcerts, app);
      for(vector< manifest<cert> >::const_iterator i = mcerts.begin();
          i != mcerts.end(); ++i)
        {
          L(FL("loaded '%s' manifest cert for node %s") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(make_pair(node,
                                      make_pair(i->inner().name, tv)));
        }
    }
  else
    {
      node = old_man_to_node[man];
    }
  return node;
}

u64 anc_graph::add_node_for_oldstyle_revision(revision_id const & rev)
{
  I(existing_graph);
  I(!null_id(rev));
  u64 node = 0;
  if (old_rev_to_node.find(rev) == old_rev_to_node.end())
    {
      node = max_node++;
      ++n_nodes;

      manifest_id man;
      legacy::renames_map renames;
      legacy::get_manifest_and_renames_for_rev(app, rev, man, renames);

      L(FL("node %d = revision %s = manifest %s") % node % rev % man);
      old_rev_to_node.insert(make_pair(rev, node));
      node_to_old_rev.insert(make_pair(node, rev));
      node_to_old_man.insert(make_pair(node, man));
      node_to_renames.insert(make_pair(node, renames));

      // load certs
      vector< revision<cert> > rcerts;
      app.db.get_revision_certs(rev, rcerts);
      erase_bogus_certs(rcerts, app);
      for(vector< revision<cert> >::const_iterator i = rcerts.begin();
          i != rcerts.end(); ++i)
        {
          L(FL("loaded '%s' revision cert for node %s") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(make_pair(node,
                                      make_pair(i->inner().name, tv)));

          if (i->inner().name == branch_cert_name)
            branches.insert(tv());
        }
    }
  else
    {
      node = old_rev_to_node[rev];
    }

  return node;
}

static bool
not_dead_yet(node_id nid, u64 birth_rev,
             parent_roster_map const & parent_rosters,
             multimap<u64, u64> const & child_to_parents)
{
  // Any given node, at each point in the revision graph, is in one of the
  // states "alive", "unborn", "dead".  The invariant we must maintain in
  // constructing our revision graph is that if a node is dead in any parent,
  // then it must also be dead in the child.  The purpose of this function is
  // to take a node, and a list of parents, and determine whether that node is
  // allowed to be alive in a child of the given parents.

  // "Alive" means, the node currently exists in the revision's tree.
  // "Unborn" means, the node does not exist in the revision's tree, and the
  // node's birth revision is _not_ an ancestor of the revision.
  // "Dead" means, the node does not exist in the revision's tree, and the
  // node's birth revision _is_ an ancestor of the revision.

  // L(FL("testing liveliness of node %d, born in rev %d") % nid % birth_rev);
  for (parent_roster_map::const_iterator r = parent_rosters.begin();
       r != parent_rosters.end(); ++r)
    {
      shared_ptr<roster_t> parent = r->second.first;
      // L(FL("node %d %s in parent roster %d")
      //             % nid
      //             % (parent->has_node(n->first) ? "exists" : "does not exist" )
      //             % r->first);

      if (!parent->has_node(nid))
        {
          deque<u64> work;
          set<u64> seen;
          work.push_back(r->first);
          while (!work.empty())
            {
              u64 curr = work.front();
              work.pop_front();
              // L(FL("examining ancestor %d of parent roster %d, looking for anc=%d")
              //                     % curr % r->first % birth_rev);

              if (seen.find(curr) != seen.end())
                continue;
              seen.insert(curr);

              if (curr == birth_rev)
                {
                  // L(FL("node is dead in %d") % r->first);
                  return false;
                }
              typedef multimap<u64, u64>::const_iterator ci;
              pair<ci,ci> range = child_to_parents.equal_range(curr);
              for (ci i = range.first; i != range.second; ++i)
                {
                  if (i->first != curr)
                    continue;
                  work.push_back(i->second);
                }
            }
        }
    }
  // L(FL("node is alive in all parents, returning true"));
  return true;
}


static file_path
find_old_path_for(map<file_path, file_path> const & renames,
                  file_path const & new_path)
{
  map<file_path, file_path>::const_iterator i = renames.find(new_path);
  if (i != renames.end())
    return i->second;

  // ??? root directory rename possible in the old schema?
  // if not, do this first.
  if (new_path.empty())
    return new_path;

  file_path dir;
  path_component base;
  new_path.dirname_basename(dir, base);
  return find_old_path_for(renames, dir) / base;
}

static file_path
find_new_path_for(map<file_path, file_path> const & renames,
                  file_path const & old_path)
{
  map<file_path, file_path> reversed;
  for (map<file_path, file_path>::const_iterator i = renames.begin();
       i != renames.end(); ++i)
    reversed.insert(make_pair(i->second, i->first));
  // this is a hackish kluge.  seems to work, though.
  return find_old_path_for(reversed, old_path);
}

// Recursive helper function for insert_into_roster.
static void
insert_parents_into_roster(roster_t & child_roster,
                           temp_node_id_source & nis,
                           file_path const & pth,
                           file_path const & full)
{
  if (child_roster.has_node(pth))
    {
      E(is_dir_t(child_roster.get_node(pth)),
        F("Directory %s for path %s cannot be added, "
          "as there is a file in the way") % pth % full);
      return;
    }

  if (!pth.empty())
    insert_parents_into_roster(child_roster, nis, pth.dirname(), full);

  child_roster.attach_node(child_roster.create_dir_node(nis), pth);
}

static void
insert_into_roster(roster_t & child_roster,
                   temp_node_id_source & nis,
                   file_path const & pth,
                   file_id const & fid)
{
  if (child_roster.has_node(pth))
    {
      node_t n = child_roster.get_node(pth);
      E(is_file_t(n),
        F("Path %s cannot be added, as there is a directory in the way") % pth);
      file_t f = downcast_to_file_t(n);
      E(f->content == fid,
        F("Path %s added twice with differing content") % pth);
      return;
    }

  insert_parents_into_roster(child_roster, nis, pth.dirname(), pth);
  child_roster.attach_node(child_roster.create_file_node(fid, nis), pth);
}

void
anc_graph::fixup_node_identities(parent_roster_map const & parent_rosters,
                                 roster_t & child_roster,
                                 legacy::renames_map const & renames)
{
  // Our strategy here is to iterate over every node in every parent, and
  // for each parent node P find zero or one tmp nodes in the child which
  // represents the fate of P:
  //
  //   - If any of the parents thinks that P has died, we do not search for
  //     it in the child; we leave it as "dropped".
  //
  //   - We fetch the name N of the parent node P, and apply the rename map
  //     to N, getting "remapped name" M.  If we find a child node C with
  //     name M in the child roster, with the same type as P, we identify P
  //     and C, and swap P for C in the child.


  // Map node_id -> birth rev
  map<node_id, u64> nodes_in_any_parent;

  // Stage 1: collect all nodes (and their birth revs) in any parent.
  for (parent_roster_map::const_iterator i = parent_rosters.begin();
       i != parent_rosters.end(); ++i)
    {
      shared_ptr<roster_t> parent_roster = i->second.first;
      shared_ptr<marking_map> parent_marking = i->second.second;

      node_map const & nodes = parent_roster->all_nodes();
      for (node_map::const_iterator j = nodes.begin(); j != nodes.end(); ++j)
        {
          node_id n = j->first;
          revision_id birth_rev = safe_get(*parent_marking, n).birth_revision;
          u64 birth_node = safe_get(new_rev_to_node, birth_rev);
          map<node_id, u64>::const_iterator i = nodes_in_any_parent.find(n);
          if (i != nodes_in_any_parent.end())
            I(i->second == birth_node);
          else
            safe_insert(nodes_in_any_parent,
                        make_pair(n, birth_node));
        }
    }

  // Stage 2: For any node which is actually live, try to locate a mapping
  // from a parent instance of it to a child node.
  for (map<node_id, u64>::const_iterator i = nodes_in_any_parent.begin();
       i != nodes_in_any_parent.end(); ++i)
    {
      node_id n = i->first;
      u64 birth_rev = i->second;

      if (child_roster.has_node(n))
        continue;

      if (not_dead_yet(n, birth_rev, parent_rosters, ancestry))
        {
          for (parent_roster_map::const_iterator j = parent_rosters.begin();
               j != parent_rosters.end(); ++j)
            {
              shared_ptr<roster_t> parent_roster = j->second.first;

              if (!parent_roster->has_node(n))
                continue;

              file_path fp;
              parent_roster->get_name(n, fp);

              // Try remapping the name.
              if (node_to_old_rev.find(j->first) != node_to_old_rev.end())
                {
                  legacy::renames_map::const_iterator rmap;
                  revision_id parent_rid = safe_get(node_to_old_rev, j->first);
                  rmap = renames.find(parent_rid);
                  if (rmap != renames.end())
                    fp = find_new_path_for(rmap->second, fp);
                }

              // See if we can match this node against a child.
              if ((!child_roster.has_node(n))
                  && child_roster.has_node(fp))
                {
                  node_t pn = parent_roster->get_node(n);
                  node_t cn = child_roster.get_node(fp);
                  if (is_file_t(pn) == is_file_t(cn))
                    {
                      child_roster.replace_node_id(cn->self, n);
                      break;
                    }
                }
            }
        }
    }
}

struct
current_rev_debugger
{
  u64 node;
  anc_graph const & agraph;
  current_rev_debugger(u64 n, anc_graph const & ag)
    : node(n), agraph(ag)
  {
  }
};

template <> void
dump(current_rev_debugger const & d, string & out)
{
  typedef multimap<u64, pair<cert_name, cert_value> >::const_iterator ci;
  pair<ci,ci> range = d.agraph.certs.equal_range(d.node);
  for(ci i = range.first; i != range.second; ++i)
    {
      if (i->first == d.node)
        {
          out += "cert '" + i->second.first() + "'";
          out += "= '" + i->second.second() + "'\n";
        }
    }
}


void
anc_graph::construct_revisions_from_ancestry()
{
  // This is an incredibly cheesy, and also reasonably simple sorting
  // system: we put all the root nodes in the work queue. we take a
  // node out of the work queue and check if its parents are done. if
  // they are, we process it and insert its children. otherwise we put
  // it back on the end of the work queue. This both ensures that we're
  // always processing something *like* a frontier, while avoiding the
  // need to worry about one side of the frontier advancing faster than
  // another.

  typedef multimap<u64,u64>::const_iterator ci;
  multimap<u64,u64> parent_to_child_map;
  deque<u64> work;
  set<u64> done;

  {
    // Set up the parent->child mapping and prime the work queue

    set<u64> children, all;
    for (multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      {
        parent_to_child_map.insert(make_pair(i->second, i->first));
        children.insert(i->first);
      }
    for (map<u64,manifest_id>::const_iterator i = node_to_old_man.begin();
         i != node_to_old_man.end(); ++i)
      {
        all.insert(i->first);
      }

    set_difference(all.begin(), all.end(),
                   children.begin(), children.end(),
                   back_inserter(work));
  }

  while (!work.empty())
    {

      u64 child = work.front();

      current_rev_debugger dbg(child, *this);
      MM(dbg);

      work.pop_front();

      if (done.find(child) != done.end())
        continue;

      pair<ci,ci> parent_range = ancestry.equal_range(child);
      set<u64> parents;
      bool parents_all_done = true;
      for (ci i = parent_range.first; parents_all_done && i != parent_range.second; ++i)
      {
        if (i->first != child)
          continue;
        u64 parent = i->second;
        if (done.find(parent) == done.end())
          {
            work.push_back(child);
            parents_all_done = false;
          }
        else
          parents.insert(parent);
      }

      if (parents_all_done
          && (node_to_new_rev.find(child) == node_to_new_rev.end()))
        {
          L(FL("processing node %d") % child);

          manifest_id old_child_mid;
          legacy::manifest_map old_child_man;

          get_node_manifest(child, old_child_mid);
          manifest_data mdat;
          app.db.get_manifest_version(old_child_mid, mdat);
          legacy::read_manifest_map(mdat, old_child_man);

          // Load all the parent rosters into a temporary roster map
          parent_roster_map parent_rosters;
          MM(parent_rosters);

          for (ci i = parent_range.first; parents_all_done && i != parent_range.second; ++i)
            {
              if (i->first != child)
                continue;
              u64 parent = i->second;
              if (parent_rosters.find(parent) == parent_rosters.end())
                {
                  shared_ptr<roster_t> ros = shared_ptr<roster_t>(new roster_t());
                  shared_ptr<marking_map> mm = shared_ptr<marking_map>(new marking_map());
                  app.db.get_roster(safe_get(node_to_new_rev, parent), *ros, *mm);
                  safe_insert(parent_rosters, make_pair(parent, make_pair(ros, mm)));
                }
            }

          file_path attr_path = file_path_internal(".mt-attrs");
          file_path old_ignore_path = file_path_internal(".mt-ignore");
          file_path new_ignore_path = file_path_internal(".mtn-ignore");

          roster_t child_roster;
          MM(child_roster);
          temp_node_id_source nis;

          // all rosters shall have a root node.
          child_roster.attach_node(child_roster.create_dir_node(nis),
                                   file_path_internal(""));

          for (legacy::manifest_map::const_iterator i = old_child_man.begin();
               i != old_child_man.end(); ++i)
            {
              if (i->first == attr_path)
                continue;
              // convert .mt-ignore to .mtn-ignore... except if .mtn-ignore
              // already exists, just leave things alone.
              else if (i->first == old_ignore_path
                       && old_child_man.find(new_ignore_path) == old_child_man.end())
                insert_into_roster(child_roster, nis, new_ignore_path, i->second);
              else
                insert_into_roster(child_roster, nis, i->first, i->second);
            }

          // migrate attributes out of .mt-attrs
          {
            legacy::manifest_map::const_iterator i = old_child_man.find(attr_path);
            if (i != old_child_man.end())
              {
                file_data dat;
                app.db.get_file_version(i->second, dat);
                legacy::dot_mt_attrs_map attrs;
                legacy::read_dot_mt_attrs(dat.inner(), attrs);
                for (legacy::dot_mt_attrs_map::const_iterator j = attrs.begin();
                     j != attrs.end(); ++j)
                  {
                    if (child_roster.has_node(j->first))
                      {
                        map<string, string> const &
                          fattrs = j->second;
                        for (map<string, string>::const_iterator
                               k = fattrs.begin();
                             k != fattrs.end(); ++k)
                          {
                            string key = k->first;
                            if (app.opts.attrs_to_drop.find(key) != app.opts.attrs_to_drop.end())
                              {
                                // ignore it
                              }
                            else if (key == "execute" || key == "manual_merge")
                              child_roster.set_attr(j->first,
                                                    attr_key("mtn:" + key),
                                                    attr_value(k->second));
                            else
                              E(false, F("unknown attribute '%s' on path '%s'\n"
                                         "please contact %s so we can work out the right way to migrate this\n"
                                         "(if you just want it to go away, see the switch --drop-attr, but\n"
                                         "seriously, if you'd like to keep it, we're happy to figure out how)")
                                % key % j->first % PACKAGE_BUGREPORT);
                          }
                      }
                  }
              }
          }

          // Now knit the parent node IDs into child node IDs (which are currently all
          // tmpids), wherever possible.
          fixup_node_identities(parent_rosters, child_roster, node_to_renames[child]);

          revision_t rev;
          rev.made_for = made_for_database;
          MM(rev);
          calculate_ident(child_roster, rev.new_manifest);

          // For each parent, construct an edge in the revision structure by analyzing the
          // relationship between the parent roster and the child roster (and placing the
          // result in a cset)

          for (parent_roster_map::const_iterator i = parent_rosters.begin();
               i != parent_rosters.end(); ++i)
            {
              u64 parent = i->first;
              revision_id parent_rid = safe_get(node_to_new_rev, parent);
              shared_ptr<roster_t> parent_roster = i->second.first;
              shared_ptr<cset> cs = shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs);
              safe_insert(rev.edges, make_pair(parent_rid, cs));
            }

          // It is possible that we're at a "root" node here -- a node
          // which had no parent in the old rev graph -- in which case we
          // synthesize an edge from the empty revision to the current,
          // containing a cset which adds all the files in the child.

          if (rev.edges.empty())
            {
              revision_id parent_rid;
              shared_ptr<roster_t> parent_roster = shared_ptr<roster_t>(new roster_t());
              shared_ptr<cset> cs = shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs);
              safe_insert(rev.edges, make_pair (parent_rid, cs));

            }

          // Finally, put all this excitement into the database and save
          // the new_rid for use in the cert-writing pass.

          revision_id new_rid;
          calculate_ident(rev, new_rid);
          node_to_new_rev.insert(make_pair(child, new_rid));
          new_rev_to_node.insert(make_pair(new_rid, child));

          /*
          P(F("------------------------------------------------"));
          P(F("made revision %s with %d edges, manifest id = %s")
            % new_rid % rev.edges.size() % rev.new_manifest);

          {
            string rtmp;
            data dtmp;
            dump(dbg, rtmp);
            write_revision(rev, dtmp);
            P(F("%s") % rtmp);
            P(F("%s") % dtmp);
          }
          P(F("------------------------------------------------"));
          */

          L(FL("mapped node %d to revision %s") % child % new_rid);
          if (app.db.put_revision(new_rid, rev))
            ++n_revs_out;
          
          // Mark this child as done, hooray!
          safe_insert(done, child);

          // Extend the work queue with all the children of this child
          pair<ci,ci> grandchild_range = parent_to_child_map.equal_range(child);
          for (ci i = grandchild_range.first;
               i != grandchild_range.second; ++i)
            {
              if (i->first != child)
                continue;
              if (done.find(i->second) == done.end())
                work.push_back(i->second);
            }
        }
    }
}

void
build_roster_style_revs_from_manifest_style_revs(app_state & app)
{
  app.db.ensure_open_for_format_changes();
  app.db.check_is_not_rosterified();

  anc_graph graph(true, app);

  P(F("converting existing revision graph to new roster-style revisions"));
  multimap<revision_id, revision_id> existing_graph;

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    get_user_key(key,app);
    require_password(key, app);
  }

  // cross-check that we're getting everything
  // in fact the code in this function is wrong, because if a revision has no
  // parents and no children (it is a root revision, and no children have been
  // committed under it), then we will simply drop it!
  // This code at least causes this case to throw an assertion; FIXME: make
  // this case actually work.
  set<revision_id> all_rev_ids;
  app.db.get_revision_ids(all_rev_ids);

  app.db.get_revision_ancestry(existing_graph);
  for (multimap<revision_id, revision_id>::const_iterator i = existing_graph.begin();
       i != existing_graph.end(); ++i)
    {
      // FIXME: insert for the null id as well, and do the same for the
      // changesetify code, and then reach rebuild_ancestry how to deal with
      // such things.  (I guess u64(0) should represent the null parent?)
      if (!null_id(i->first))
        {
          u64 parent_node = graph.add_node_for_oldstyle_revision(i->first);
          all_rev_ids.erase(i->first);
          u64 child_node = graph.add_node_for_oldstyle_revision(i->second);
          all_rev_ids.erase(i->second);
          graph.add_node_ancestry(child_node, parent_node);
        }
    }

  for (set<revision_id>::const_iterator i = all_rev_ids.begin();
       i != all_rev_ids.end(); ++i)
    {
      graph.add_node_for_oldstyle_revision(*i);
    }

  graph.rebuild_ancestry();
}


void
build_changesets_from_manifest_ancestry(app_state & app)
{
  app.db.ensure_open_for_format_changes();
  app.db.check_is_not_rosterified();

  anc_graph graph(false, app);

  P(F("rebuilding revision graph from manifest certs"));

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    get_user_key(key,app);
    require_password(key, app);
  }

  vector< manifest<cert> > tmp;
  app.db.get_manifest_certs(cert_name("ancestor"), tmp);
  erase_bogus_certs(tmp, app);

  for (vector< manifest<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id child, parent;
      child = manifest_id(i->inner().ident);
      parent = manifest_id(tv());

      u64 parent_node = graph.add_node_for_old_manifest(parent);
      u64 child_node = graph.add_node_for_old_manifest(child);
      graph.add_node_ancestry(child_node, parent_node);
    }

  graph.rebuild_ancestry();
}


// This is a special function solely for the use of regenerate_caches -- it
// must work even when caches (especially, the height cache!) do not exist.
// For all other purposes, use toposort above.
static void
allrevs_toposorted(vector<revision_id> & revisions,
                   app_state & app)
{
  // get the complete ancestry
  rev_ancestry_map graph;
  app.db.get_revision_ancestry(graph);
  toposort_rev_ancestry(graph, revisions);
}

void
regenerate_caches(app_state & app)
{
  P(F("regenerating cached rosters and heights"));

  app.db.ensure_open_for_format_changes();

  transaction_guard guard(app.db);

  app.db.delete_existing_rosters();
  app.db.delete_existing_heights();

  vector<revision_id> sorted_ids;
  allrevs_toposorted(sorted_ids, app);

  ticker done(_("regenerated"), "r", 5);
  done.set_total(sorted_ids.size());

  for (std::vector<revision_id>::const_iterator i = sorted_ids.begin();
       i != sorted_ids.end(); ++i)
    {
      revision_t rev;
      revision_id const & rev_id = *i;
      app.db.get_revision(rev_id, rev);
      app.db.put_roster_for_revision(rev_id, rev);
      app.db.put_height_for_revision(rev_id, rev);
      ++done;
    }

  guard.commit();

  P(F("finished regenerating cached rosters and heights"));
}

CMD_HIDDEN(rev_height, "rev_height", "", CMD_REF(informative), N_("REV"),
           N_("Shows a revision's height"),
           "",
           options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  rev_height height;
  app.db.get_rev_height(rid, height);
  P(F("cached height: %s") % height);
}

// i/o stuff

namespace
{
  namespace syms
  {
    symbol const format_version("format_version");
    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
  }
}

void
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, edge_old_revision(e).inner());
  printer.print_stanza(st);
  print_cset(printer, edge_changes(e));
}

static void
print_insane_revision(basic_io::printer & printer,
                      revision_t const & rev)
{

  basic_io::stanza format_stanza;
  format_stanza.push_str_pair(syms::format_version, "1");
  printer.print_stanza(format_stanza);

  basic_io::stanza manifest_stanza;
  manifest_stanza.push_hex_pair(syms::new_manifest, rev.new_manifest.inner());
  printer.print_stanza(manifest_stanza);

  for (edge_map::const_iterator edge = rev.edges.begin();
       edge != rev.edges.end(); ++edge)
    print_edge(printer, *edge);
}

void
print_revision(basic_io::printer & printer,
               revision_t const & rev)
{
  rev.check_sane();
  print_insane_revision(printer, rev);
}


void
parse_edge(basic_io::parser & parser,
           edge_map & es)
{
  shared_ptr<cset> cs(new cset());
  MM(*cs);
  manifest_id old_man;
  revision_id old_rev;
  string tmp;

  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = revision_id(tmp);

  parse_cset(parser, *cs);

  es.insert(make_pair(old_rev, cs));
}


void
parse_revision(basic_io::parser & parser,
               revision_t & rev)
{
  MM(rev);
  rev.edges.clear();
  rev.made_for = made_for_database;
  string tmp;
  parser.esym(syms::format_version);
  parser.str(tmp);
  E(tmp == "1",
    F("encountered a revision with unknown format, version '%s'\n"
      "I only know how to understand the version '1' format\n"
      "a newer version of monotone is required to complete this operation")
    % tmp);
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
  rev.new_manifest = manifest_id(tmp);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, rev.edges);
  rev.check_sane();
}

void
read_revision(data const & dat,
              revision_t & rev)
{
  MM(rev);
  basic_io::input_source src(dat(), "revision");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_revision(pars, rev);
  I(src.lookahead == EOF);
  rev.check_sane();
}

void
read_revision(revision_data const & dat,
              revision_t & rev)
{
  read_revision(dat.inner(), rev);
  rev.check_sane();
}

static void write_insane_revision(revision_t const & rev,
                                  data & dat)
{
  basic_io::printer pr;
  print_insane_revision(pr, rev);
  dat = data(pr.buf);
}

template <> void
dump(revision_t const & rev, string & out)
{
  data dat;
  write_insane_revision(rev, dat);
  out = dat();
}

void
write_revision(revision_t const & rev,
               data & dat)
{
  rev.check_sane();
  write_insane_revision(rev, dat);
}

void
write_revision(revision_t const & rev,
               revision_data & dat)
{
  data d;
  write_revision(rev, d);
  dat = revision_data(d);
}

void calculate_ident(revision_t const & cs,
                     revision_id & ident)
{
  data tmp;
  hexenc<id> tid;
  write_revision(cs, tmp);
  calculate_ident(tmp, tid);
  ident = revision_id(tid);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"

UNIT_TEST(revision, find_old_new_path_for)
{
  map<file_path, file_path> renames;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path foo_baz = file_path_internal("foo/baz");
  file_path quux = file_path_internal("quux");
  file_path quux_baz = file_path_internal("quux/baz");
  I(foo == find_old_path_for(renames, foo));
  I(foo == find_new_path_for(renames, foo));
  I(foo_bar == find_old_path_for(renames, foo_bar));
  I(foo_bar == find_new_path_for(renames, foo_bar));
  I(quux == find_old_path_for(renames, quux));
  I(quux == find_new_path_for(renames, quux));
  renames.insert(make_pair(foo, quux));
  renames.insert(make_pair(foo_bar, foo_baz));
  I(quux == find_old_path_for(renames, foo));
  I(foo == find_new_path_for(renames, quux));
  I(quux_baz == find_old_path_for(renames, foo_baz));
  I(foo_baz == find_new_path_for(renames, quux_baz));
  I(foo_baz == find_old_path_for(renames, foo_bar));
  I(foo_bar == find_new_path_for(renames, foo_baz));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
