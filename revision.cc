// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <iterator>
#include <functional>
#include <list>

#include <boost/lexical_cast.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/shared_ptr.hpp>

#include "botan/botan.h"

#include "app_state.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "constants.hh"
#include "interner.hh"
#include "keys.hh"
#include "numeric_vocab.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab.hh"


void revision_set::check_sane() const
{
/*
// FIXME_ROSTERS: disabled until rewritten to use rosters
  I(!null_id(new_manifest));

  manifest_map fragment;
  for (edge_map::const_iterator i = edges.begin(); i != edges.end(); ++i)
    {
      change_set const & cs = edge_changes(i);
      cs.check_sane();
      if (!global_sanity.relaxed)
        {
          // null old revisions come with null old manifests
          I(!null_id(edge_old_revision(i)) || null_id(edge_old_manifest(i)));
        }
      for (change_set::delta_map::const_iterator j = cs.deltas.begin(); j != cs.deltas.end(); ++j)
        {
          manifest_map::const_iterator k = fragment.find(delta_entry_path(j));
          if (k == fragment.end())
            fragment.insert(std::make_pair(delta_entry_path(j),
                                           delta_entry_dst(j)));
          else
            {
              if (!global_sanity.relaxed)
                {                  
                  I(delta_entry_dst(j) == manifest_entry_id(k));
                }
            }
        }
    }
*/
}

bool 
revision_set::is_merge_node() const
{ 
  return edges.size() > 1; 
}

revision_set::revision_set(revision_set const & other)
{
  /* behave like normal constructor if other is empty */
  if (null_id(other.new_manifest) && other.edges.empty()) return;
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
}

revision_set const & 
revision_set::operator=(revision_set const & other)
{
  other.check_sane();
  new_manifest = other.new_manifest;
  edges = other.edges;
  return *this;
}


// calculating least common ancestors is a delicate thing.
// 
// it turns out that we cannot choose the simple "least common ancestor"
// for purposes of a merge, because it is possible that there are two
// equally reachable common ancestors, and this produces ambiguity in the
// merge. the result -- in a pathological case -- is silently accepting one
// set of edits while discarding another; not exactly what you want a
// version control tool to do.
//
// a conservative approximation is what we'll call a "subgraph recurring"
// LCA algorithm. this is somewhat like locating the least common dominator
// node, but not quite. it is actually just a vanilla LCA search, except
// that any time there's a fork (a historical merge looks like a fork from
// our perspective, working backwards from children to parents) it reduces
// the fork to a common parent via a sequence of pairwise recursive calls
// to itself before proceeding. this will always resolve to a common parent
// with no ambiguity, unless it falls off the root of the graph.
//
// unfortunately the subgraph recurring algorithm sometimes goes too far
// back in history -- for example if there is an unambiguous propagate from
// one branch to another, the entire subgraph preceeding the propagate on
// the recipient branch is elided, since it is a merge.
//
// our current hypothesis is that the *exact* condition we're looking for,
// when doing a merge, is the least node which dominates one side of the
// merge and is an ancestor of the other.

typedef unsigned long ctx;
typedef boost::dynamic_bitset<> bitmap;
typedef boost::shared_ptr<bitmap> shared_bitmap;

static void 
ensure_parents_loaded(ctx child,
                      std::map<ctx, shared_bitmap> & parents,
                      interner<ctx> & intern,
                      app_state & app)
{
  if (parents.find(child) != parents.end())
    return;

  L(F("loading parents for node %d\n") % child);

  std::set<revision_id> imm_parents;
  app.db.get_revision_parents(revision_id(intern.lookup(child)), imm_parents);

  // The null revision is not a parent for purposes of finding common
  // ancestors.
  for (std::set<revision_id>::iterator p = imm_parents.begin();
       p != imm_parents.end(); )
    {
      if (null_id(*p))
        imm_parents.erase(p++);
      else
        ++p;
    }
              
  shared_bitmap bits = shared_bitmap(new bitmap(parents.size()));
  
  for (std::set<revision_id>::const_iterator p = imm_parents.begin();
       p != imm_parents.end(); ++p)
    {
      ctx pn = intern.intern(p->inner()());
      L(F("parent %s -> node %d\n") % *p % pn);
      if (pn >= bits->size()) 
        bits->resize(pn+1);
      bits->set(pn);
    }
    
  parents.insert(std::make_pair(child, bits));
}

static bool 
expand_dominators(std::map<ctx, shared_bitmap> & parents,
                  std::map<ctx, shared_bitmap> & dominators,
                  interner<ctx> & intern,
                  app_state & app)
{
  bool something_changed = false;
  std::vector<ctx> nodes;

  nodes.reserve(dominators.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (std::map<ctx, shared_bitmap>::reverse_iterator e = dominators.rbegin(); 
       e != dominators.rend(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the dominator entries we can
  for (std::vector<ctx>::const_iterator n = nodes.begin(); 
       n != nodes.end(); ++n)
    {
      shared_bitmap bits = dominators[*n];
      bitmap saved(*bits);
      if (bits->size() <= *n)
        bits->resize(*n + 1);
      bits->set(*n);
      
      ensure_parents_loaded(*n, parents, intern, app);
      shared_bitmap n_parents = parents[*n];
      
      bitmap intersection(bits->size());
      
      bool first = true;
      for (unsigned long parent = 0; 
           parent != n_parents->size(); ++parent)
        {
          if (! n_parents->test(parent))
            continue;

          if (dominators.find(parent) == dominators.end())
            dominators.insert(std::make_pair(parent, 
                                             shared_bitmap(new bitmap())));
          shared_bitmap pbits = dominators[parent];

          if (intersection.size() > pbits->size())
            pbits->resize(intersection.size());

          if (pbits->size() > intersection.size())
            intersection.resize(pbits->size());

          if (first)
            {
              intersection = (*pbits);
              first = false;
            }
          else
            intersection &= (*pbits);
        }

      if (intersection.size() > bits->size())
        bits->resize(intersection.size());

      if (bits->size() > intersection.size())
        intersection.resize(bits->size());
      (*bits) |= intersection;
      if (*bits != saved)
        something_changed = true;
    }
  return something_changed;
}


static bool 
expand_ancestors(std::map<ctx, shared_bitmap> & parents,
                 std::map<ctx, shared_bitmap> & ancestors,
                 interner<ctx> & intern,
                 app_state & app)
{
  bool something_changed = false;
  std::vector<ctx> nodes;

  nodes.reserve(ancestors.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (std::map<ctx, shared_bitmap>::reverse_iterator e = ancestors.rbegin(); 
       e != ancestors.rend(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the ancestor entries we can
  for (std::vector<ctx>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
      shared_bitmap bits = ancestors[*n];
      bitmap saved(*bits);
      if (bits->size() <= *n)
        bits->resize(*n + 1);
      bits->set(*n);

      ensure_parents_loaded(*n, parents, intern, app);
      shared_bitmap n_parents = parents[*n];
      for (ctx parent = 0; parent != n_parents->size(); ++parent)
        {
          if (! n_parents->test(parent))
            continue;

          if (bits->size() <= parent)
            bits->resize(parent + 1);
          bits->set(parent);

          if (ancestors.find(parent) == ancestors.end())
            ancestors.insert(make_pair(parent, 
                                        shared_bitmap(new bitmap())));
          shared_bitmap pbits = ancestors[parent];

          if (bits->size() > pbits->size())
            pbits->resize(bits->size());

          if (pbits->size() > bits->size())
            bits->resize(pbits->size());

          (*bits) |= (*pbits);
        }
      if (*bits != saved)
        something_changed = true;
    }
  return something_changed;
}

static bool 
find_intersecting_node(bitmap & fst, 
                       bitmap & snd, 
                       interner<ctx> const & intern, 
                       revision_id & anc)
{
  
  if (fst.size() > snd.size())
    snd.resize(fst.size());
  else if (snd.size() > fst.size())
    fst.resize(snd.size());
  
  bitmap intersection = fst & snd;
  if (intersection.any())
    {
      L(F("found %d intersecting nodes\n") % intersection.count());
      for (ctx i = 0; i < intersection.size(); ++i)
        {
          if (intersection.test(i))
            {
              anc = revision_id(intern.lookup(i));
              return true;
            }
        }
    }
  return false;
}

//  static void
//  dump_bitset_map(std::string const & hdr,
//              std::map< ctx, shared_bitmap > const & mm)
//  {
//    L(F("dumping [%s] (%d entries)\n") % hdr % mm.size());
//    for (std::map< ctx, shared_bitmap >::const_iterator i = mm.begin();
//         i != mm.end(); ++i)
//      {
//        L(F("dump [%s]: %d -> %s\n") % hdr % i->first % (*(i->second)));
//      }
//  }

bool 
find_common_ancestor_for_merge(revision_id const & left,
                               revision_id const & right,
                               revision_id & anc,
                               app_state & app)
{
  // Temporary workaround until we figure out how to clean up the whole
  // ancestor selection mess:
  if (app.use_lca)
    return find_least_common_ancestor(left, right, anc, app);

  interner<ctx> intern;
  std::map< ctx, shared_bitmap > 
    parents, ancestors, dominators;
  
  ctx ln = intern.intern(left.inner()());
  ctx rn = intern.intern(right.inner()());
  
  shared_bitmap lanc = shared_bitmap(new bitmap());
  shared_bitmap ranc = shared_bitmap(new bitmap());
  shared_bitmap ldom = shared_bitmap(new bitmap());
  shared_bitmap rdom = shared_bitmap(new bitmap());

  ancestors.insert(make_pair(ln, lanc));
  ancestors.insert(make_pair(rn, ranc));
  dominators.insert(make_pair(ln, ldom));
  dominators.insert(make_pair(rn, rdom));
  
  L(F("searching for common ancestor, left=%s right=%s\n") % left % right);
  
  while (expand_ancestors(parents, ancestors, intern, app) |
         expand_dominators(parents, dominators, intern, app))
    {
      L(F("common ancestor scan [par=%d,anc=%d,dom=%d]\n") % 
        parents.size() % ancestors.size() % dominators.size());

      if (find_intersecting_node(*lanc, *rdom, intern, anc))
        {
          L(F("found node %d, ancestor of left %s and dominating right %s\n")
            % anc % left % right);
          return true;
        }
      
      else if (find_intersecting_node(*ranc, *ldom, intern, anc))
        {
          L(F("found node %d, ancestor of right %s and dominating left %s\n")
            % anc % right % left);
          return true;
        }
    }
//      dump_bitset_map("ancestors", ancestors);
//      dump_bitset_map("dominators", dominators);
//      dump_bitset_map("parents", parents);
  return false;
}


bool
find_least_common_ancestor(revision_id const & left,
                           revision_id const & right,
                           revision_id & anc,
                           app_state & app)
{
  interner<ctx> intern;
  std::map< ctx, shared_bitmap >
    parents, ancestors;

  if (left == right)
    {
      anc = left;
      return true;
    }

  ctx ln = intern.intern(left.inner()());
  ctx rn = intern.intern(right.inner()());

  shared_bitmap lanc = shared_bitmap(new bitmap());
  shared_bitmap ranc = shared_bitmap(new bitmap());

  ancestors.insert(make_pair(ln, lanc));
  ancestors.insert(make_pair(rn, ranc));

  L(F("searching for least common ancestor, left=%s right=%s\n") % left % right);

  while (expand_ancestors(parents, ancestors, intern, app))
    {
      L(F("least common ancestor scan [par=%d,anc=%d]\n") %
        parents.size() % ancestors.size());

      if (find_intersecting_node(*lanc, *ranc, intern, anc))
        {
          L(F("found node %d, ancestor of left %s and right %s\n")
            % anc % left % right);
          return true;
        }
    }
//      dump_bitset_map("ancestors", ancestors);
//      dump_bitset_map("parents", parents);
  return false;
}


// FIXME: this algorithm is incredibly inefficient; it's O(n) where n is the
// size of the entire revision graph.

static bool
is_ancestor(revision_id const & ancestor_id,
            revision_id const & descendent_id,
            std::multimap<revision_id, revision_id> const & graph)
{

  std::set<revision_id> visited;
  std::queue<revision_id> queue;

  queue.push(ancestor_id);

  while (!queue.empty())
    {
      revision_id current_id = queue.front();
      queue.pop();

      if (current_id == descendent_id)
        return true;
      else
        {
          typedef std::multimap<revision_id, revision_id>::const_iterator gi;
          std::pair<gi, gi> children = graph.equal_range(current_id);
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
  L(F("checking whether %s is an ancestor of %s\n") % ancestor_id % descendent_id);

  std::multimap<revision_id, revision_id> graph;
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
                               std::multimap<revision_id, revision_id> const & graph, 
                               std::map< ctx, shared_bitmap > & ancestors,
                               shared_bitmap & total_union)
{
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::stack<ctx> stk;

  stk.push(intern.intern(init.inner()()));

  while (! stk.empty())
    {
      ctx us = stk.top();
      revision_id rev(hexenc<id>(intern.lookup(us)));

      std::pair<gi,gi> parents = graph.equal_range(rev);
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
          std::map< ctx, shared_bitmap >::const_iterator j = ancestors.find(parent);
          I(j != ancestors.end());
          add_bitset_to_union(j->second, b);
        }

      add_bitset_to_union(b, total_union);
      ancestors.insert(std::make_pair(us, b));
      stk.pop();
    }
}

// this function actually toposorts the whole graph, and then filters by the
// passed in set.  if anyone ever needs to toposort the whole graph, then,
// this function would be a good thing to generalize...
void
toposort(std::set<revision_id> const & revisions,
         std::vector<revision_id> & sorted,
         app_state & app)
{
  sorted.clear();
  typedef std::multimap<revision_id, revision_id>::iterator gi;
  typedef std::map<revision_id, int>::iterator pi;
  std::multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  std::set<revision_id> leaves;
  app.db.get_revision_ids(leaves);
  std::map<revision_id, int> pcount;
  for (gi i = graph.begin(); i != graph.end(); ++i)
    pcount.insert(std::make_pair(i->first, 0));
  for (gi i = graph.begin(); i != graph.end(); ++i)
    ++(pcount[i->second]);
  // first find the set of graph roots
  std::list<revision_id> roots;
  for (pi i = pcount.begin(); i != pcount.end(); ++i)
    if(i->second==0)
      roots.push_back(i->first);
  while (!roots.empty())
    {
      // now stick them in our ordering (if wanted) and remove them from the
      // graph, calculating the new roots as we go
      L(F("new root: %s\n") % (roots.front()));
      if (revisions.find(roots.front()) != revisions.end())
        sorted.push_back(roots.front());
      for(gi i = graph.lower_bound(roots.front());
          i != graph.upper_bound(roots.front()); i++)
        if(--(pcount[i->second]) == 0)
          roots.push_back(i->second);
      graph.erase(roots.front());
      leaves.erase(roots.front());
      roots.pop_front();
    }
  I(graph.empty());
  for (std::set<revision_id>::const_iterator i = leaves.begin();
       i != leaves.end(); ++i)
    {
      L(F("new leaf: %s\n") % (*i));
      if (revisions.find(*i) != revisions.end())
        sorted.push_back(*i);
    }
}

// This function looks at a set of revisions, and for every pair A, B in that
// set such that A is an ancestor of B, it erases A.

void
erase_ancestors(std::set<revision_id> & revisions, app_state & app)
{
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;

  app.db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(std::make_pair(i->second, i->first));

  interner<ctx> intern;
  std::map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {      
      calculate_ancestors_from_graph(intern, *i, inverse_graph, ancestors, u);
    }

  std::set<revision_id> tmp;
  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {
      ctx id = intern.intern(i->inner()());
      bool has_ancestor_in_set = id < u->size() && u->test(id);
      if (!has_ancestor_in_set)
        tmp.insert(*i);
    }
  
  revisions = tmp;
}

// This function takes a revision A and a set of revision Bs, calculates the
// ancestry of each, and returns the set of revisions that are in A's ancestry
// but not in the ancestry of any of the Bs.  It tells you 'what's new' in A
// that's not in the Bs.  If the output set if non-empty, then A will
// certainly be in it; but the output set might be empty.
void
ancestry_difference(revision_id const & a, std::set<revision_id> const & bs,
                    std::set<revision_id> & new_stuff,
                    app_state & app)
{
  new_stuff.clear();
  typedef std::multimap<revision_id, revision_id>::const_iterator gi;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;

  app.db.get_revision_ancestry(graph);
  for (gi i = graph.begin(); i != graph.end(); ++i)
    inverse_graph.insert(std::make_pair(i->second, i->first));

  interner<ctx> intern;
  std::map< ctx, shared_bitmap > ancestors;

  shared_bitmap u = shared_bitmap(new bitmap());

  for (std::set<revision_id>::const_iterator i = bs.begin();
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

  au->resize(std::max(au->size(), u->size()));
  u->resize(std::max(au->size(), u->size()));
  
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

// Stuff related to rebuilding the revision graph. Unfortunately this is a
// real enough error case that we need support code for it.

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

  std::map<u64,manifest_id> node_to_old_man;
  std::map<manifest_id,u64> old_man_to_node;

  std::map<u64,revision_id> node_to_old_rev;
  std::map<revision_id,u64> old_rev_to_node;

  std::map<u64,revision_id> node_to_new_rev;
  std::map<revision_id,u64> new_rev_to_node;

  std::multimap<u64, std::pair<cert_name, cert_value> > certs;
  std::multimap<u64, u64> ancestry;
  std::set<std::string> branches;
  
  void add_node_ancestry(u64 child, u64 parent);  
  void write_certs();
  void kluge_for_3_ancestor_nodes();
  void rebuild_ancestry();
  void get_node_manifest(u64 node, manifest_id & man);
  u64 add_node_for_old_manifest(manifest_id const & man);
  u64 add_node_for_oldstyle_revision(revision_id const & rev);                     
  void construct_revisions_from_ancestry();
};


void anc_graph::add_node_ancestry(u64 child, u64 parent)
{
  L(F("noting ancestry from child %d -> parent %d\n") % child % parent);
  ancestry.insert(std::make_pair(child, parent));
}

void anc_graph::get_node_manifest(u64 node, manifest_id & man)
{
  std::map<u64,manifest_id>::const_iterator i = node_to_old_man.find(node);
  I(i != node_to_old_man.end());
  man = i->second;
}

void anc_graph::write_certs()
{
  std::set<cert_name> cnames;
  cnames.insert(cert_name(branch_cert_name));
  cnames.insert(cert_name(date_cert_name));
  cnames.insert(cert_name(author_cert_name));
  cnames.insert(cert_name(tag_cert_name));
  cnames.insert(cert_name(changelog_cert_name));
  cnames.insert(cert_name(comment_cert_name));
  cnames.insert(cert_name(testresult_cert_name));


  {
    // regenerate epochs on all branches to random states
    
    for (std::set<std::string>::const_iterator i = branches.begin(); i != branches.end(); ++i)
      {
        char buf[constants::epochlen_bytes];
        Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf), constants::epochlen_bytes);
        hexenc<data> hexdata;
        encode_hexenc(data(std::string(buf, buf + constants::epochlen_bytes)), hexdata);
        epoch_data new_epoch(hexdata);
        L(F("setting epoch for %s to %s\n") % *i % new_epoch);
        app.db.set_epoch(cert_value(*i), new_epoch);
      }
  }


  typedef std::multimap<u64, std::pair<cert_name, cert_value> >::const_iterator ci;
    
  for (std::map<u64,revision_id>::const_iterator i = node_to_new_rev.begin();
       i != node_to_new_rev.end(); ++i)
    {
      revision_id rev(i->second);

      std::pair<ci,ci> range = certs.equal_range(i->first);
        
      for (ci j = range.first; j != range.second; ++j)
        {
          cert_name name(j->second.first);
          cert_value val(j->second.second);

          if (cnames.find(name) == cnames.end())
            continue;

          cert new_cert;
          make_simple_cert(rev.inner(), name, val, app, new_cert);
          revision<cert> rcert(new_cert);
          if (! app.db.revision_cert_exists(rcert))
            {
              ++n_certs_out;
              app.db.put_revision_cert(rcert);
            }
        }        
    }    
}

void
anc_graph::kluge_for_3_ancestor_nodes()
{
  // This method is, as the name suggests, a kluge.  It exists because in the
  // 0.17 timeframe, monotone's ancestry graph has several nodes with 3
  // parents.  This isn't, in principle, necessarily a bad thing; having 3
  // parents is reasonably well defined, I don't know of much code that is
  // dependent on revisions having only 2 parents, etc.  But it is a very
  // weird thing, that we would never under any circumstances create today,
  // and it only exists as a side-effect of the pre-changeset days.  In the
  // future we may decide to allow 3+-parent revisions; we may decide to
  // disallow it.  Right now, I'd rather keep options open.
  // We remove only edges that are "redundant" (i.e., already weird...).
  // These are also something that we currently refuse to produce -- when a
  // node has more than one parent, and one parent is an ancestor of another.
  // These edges, again, only exist because of weirdnesses in the
  // pre-changeset days, and are not particularly meaningful.  Again, we may
  // decide in the future to use them for some things, but...
  // FIXME: remove this method eventually, since it is (mildly) destructive on
  // history, and isn't really doing anything that necessarily needs to happen
  // anyway.
  P(F("scanning for nodes with 3+ parents\n"));
  std::set<u64> manyparents;
  for (std::multimap<u64, u64>::const_iterator i = ancestry.begin();
       i != ancestry.end(); ++i)
    {
      if (ancestry.count(i->first) > 2)
        manyparents.insert(i->first);
    }
  for (std::set<u64>::const_iterator i = manyparents.begin();
       i != manyparents.end(); ++i)
    {
      std::set<u64> indirect_ancestors;
      std::set<u64> parents;
      std::vector<u64> to_examine;
      for (std::multimap<u64, u64>::const_iterator j = ancestry.lower_bound(*i);
           j != ancestry.upper_bound(*i); ++j)
        {
          to_examine.push_back(j->second);
          parents.insert(j->second);
        }
      I(!to_examine.empty());
      while (!to_examine.empty())
        {
          u64 current = to_examine.back();
          to_examine.pop_back();
          for (std::multimap<u64, u64>::const_iterator j = ancestry.lower_bound(current);
               j != ancestry.upper_bound(current); ++j)
            {
              if (indirect_ancestors.find(j->second) == indirect_ancestors.end())
                {
                  to_examine.push_back(j->second);
                  indirect_ancestors.insert(j->second);
                }
            }
        }
      size_t killed = 0;
      for (std::set<u64>::const_iterator p = parents.begin();
           p != parents.end(); ++p)
        {
          if (indirect_ancestors.find(*p) != indirect_ancestors.end())
            {
              P(F("optimizing out redundant edge %i -> %i\n") % (*p) % (*i));
              // sometimes I hate STL.  or at least my incompetence at using it.
              size_t old_size = ancestry.size();
              for (std::multimap<u64, u64>::iterator e = ancestry.lower_bound(*i);
                   e != ancestry.upper_bound(*i); ++e)
                {
                  I(e->first == *i);
                  if (e->second == *p)
                    {
                      ancestry.erase(e);
                      break;
                    }
                }
              I(old_size - 1 == ancestry.size());
              ++killed;
            }
        }
      I(killed < parents.size());
      I(ancestry.find(*i) != ancestry.end());
    }
}

void
anc_graph::rebuild_ancestry()
{
  kluge_for_3_ancestor_nodes();

  P(F("rebuilding %d nodes\n") % max_node);
  {
    transaction_guard guard(app.db);
    if (existing_graph)
      app.db.delete_existing_revs_and_certs();    
    construct_revisions_from_ancestry();
    write_certs();
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
      L(F("node %d = manifest %s\n") % node % man);
      old_man_to_node.insert(std::make_pair(man, node));
      node_to_old_man.insert(std::make_pair(node, man));
      
      // load certs
      std::vector< manifest<cert> > mcerts;
      app.db.get_manifest_certs(man, mcerts);
      erase_bogus_certs(mcerts, app);      
      for(std::vector< manifest<cert> >::const_iterator i = mcerts.begin();
          i != mcerts.end(); ++i)
        {
          L(F("loaded '%s' manifest cert for node %s\n") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(std::make_pair(node, 
                                      std::make_pair(i->inner().name, tv)));
        }
    }
  else
    {
      node = old_man_to_node[man];
    }
  return node;
}

static void 
get_manifest_for_rev(app_state & app,
                     revision_id const & ident,
                     manifest_id & mid);

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
      get_manifest_for_rev(app, rev, man);
      
      L(F("node %d = revision %s = manifest %s\n") % node % rev % man);
      old_rev_to_node.insert(std::make_pair(rev, node));
      node_to_old_rev.insert(std::make_pair(node, rev));
      node_to_old_man.insert(std::make_pair(node, man));

      // load certs
      std::vector< revision<cert> > rcerts;
      app.db.get_revision_certs(rev, rcerts);
      erase_bogus_certs(rcerts, app);      
      for(std::vector< revision<cert> >::const_iterator i = rcerts.begin();
          i != rcerts.end(); ++i)
        {
          L(F("loaded '%s' revision cert for node %s\n") % i->inner().name % node);
          cert_value tv;
          decode_base64(i->inner().value, tv);
          ++n_certs_in;
          certs.insert(std::make_pair(node, 
                                      std::make_pair(i->inner().name, tv)));

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

typedef std::map<u64, 
                 std::pair<boost::shared_ptr<roster_t>, 
                           boost::shared_ptr<marking_map>
                 > > 
parent_roster_map;

void
dump(parent_roster_map const & prm, std::string & out)
{
  std::ostringstream oss;
  for (parent_roster_map::const_iterator i = prm.begin(); i != prm.end(); ++i)
    {
      oss << "roster: " << i->first << "\n";
      std::string roster_str, indented_roster_str;
      dump(*i->second.first, roster_str);
      prefix_lines_with("    ", roster_str, indented_roster_str);
      oss << indented_roster_str;
      oss << "\nroster's marking:\n";
      std::string marking_str, indented_marking_str;
      dump(*i->second.second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str;
      oss << "\n\n";
    }
  out = oss.str();
}

static bool
viable_replacement(std::map<node_id, u64> const & birth_revs, 
                   parent_roster_map const & parent_rosters,
                   std::multimap<u64, u64> const & child_to_parents)
{
  // This is a somewhat crazy function. Conceptually, we have a set NS of
  // node_t values (representing all the elements of a path) and a set RS
  // of rosters. We want to know whether there is an N in NS and an R in RS
  // satisfying:
  //
  //       N is not present in R 
  // *and*
  //       N's birth_revision is an ancestor of R
  //
  // If we find such a pair, then the set is considered "non viable" for
  // replacement.
    
  //   L(F("beginning viability comparison for %d path nodes, %d parents\n") 
  //     % birth_revs.size() % parent_rosters.size());

  for (std::map<node_id, u64>::const_iterator n = birth_revs.begin();
       n != birth_revs.end(); ++n)
    {
      // L(F("testing viability of node %d, born in rev %d\n") % n->first % n->second);
      for (parent_roster_map::const_iterator r = parent_rosters.begin();
           r != parent_rosters.end(); ++r)
        {
          boost::shared_ptr<roster_t> parent = r->second.first;
          // L(F("node %d %s in parent roster %d\n") 
          //             % n->first
          //             % (parent->has_node(n->first) ? "exists" : "does not exist" ) 
          //             % r->first);

          if (!parent->has_node(n->first))
            {
              u64 birth_rev = n->second;
              std::deque<u64> work;
              std::set<u64> seen;
              work.push_back(r->first);
              while (!work.empty())
                {
                  u64 curr = work.front();
                  work.pop_front();
                  // L(F("examining ancestor %d of parent roster %d, looking for anc=%d\n")
                  //                     % curr % r->first % birth_rev);

                  if (seen.find(curr) != seen.end())
                    continue;
                  seen.insert(curr);

                  if (curr == birth_rev)
                    {
                      // L(F("determined non-viability, returning\n"));
                      return false;
                    }
                  typedef std::multimap<u64, u64>::const_iterator ci;
                  std::pair<ci,ci> range = child_to_parents.equal_range(curr);
                  for (ci i = range.first; i != range.second; ++i)
                    {
                      if (i->first != curr)
                        continue;
                      work.push_back(i->second);
                    }
                }
            }
        }  
    }
  // L(F("exhausted possibilities for non-viability, returning\n"));
  return true;
}


static void 
insert_into_roster_reusing_parent_entries(file_path const & pth,
                                          file_id const & fid,
                                          parent_roster_map const & parent_rosters,
                                          temp_node_id_source & nis,
                                          roster_t & child_roster,
                                          std::map<revision_id, u64> const & new_rev_to_node,
                                          std::multimap<u64, u64> const & child_to_parents)
{

  split_path sp, dirname;
  path_component basename;
  pth.split(sp);

  E(!child_roster.has_node(sp),
    F("Path %s added to child roster multiple times\n") % pth);

  dirname_basename(sp, dirname, basename);

  E(!dirname.empty(),
    F("Empty path encountered during reconstruction\n"));

  // First, we make sure the entire dir containing the file in question has
  // been inserted into the child roster.
  {
    split_path tmp_pth;
    for (split_path::const_iterator i = dirname.begin(); i != dirname.end();
         ++i)
      {
        tmp_pth.push_back(*i);
        if (child_roster.has_node(tmp_pth))
          {
            E(is_dir_t(child_roster.get_node(tmp_pth)),
              F("Directory for path %s cannot be added, as there is a file in the way\n") % pth);
          }
        else
          child_roster.attach_node(child_roster.create_dir_node(nis), tmp_pth);
      }
  }

  // Then add a node for the file and attach it
  node_id nid = child_roster.create_file_node(fid, nis);
  child_roster.attach_node(nid, sp);

  // Finally, try to find a file in one of the parents which has the same name;
  // if such a file is found, replace all the temporary node IDs we've assigned
  // with the node IDs found in the parent.

  for (parent_roster_map::const_iterator j = parent_rosters.begin();
       j != parent_rosters.end(); ++j)
    {
      // We use a stupid heuristic: first parent who has
      // a file with the same name gets the node
      // identity copied forward. 
      boost::shared_ptr<roster_t> parent_roster = j->second.first;
      boost::shared_ptr<marking_map> parent_marking = j->second.second;

      if (parent_roster->has_node(sp))
        {
          node_t other_node = parent_roster->get_node(sp);
          if (is_file_t(other_node))
            {
              // Here we've found an existing node for our file in a parent
              // roster; For example, we have foo1=p/foo2=q/foo3=r in our
              // child roster, and we've found foo1=x/foo2=y/foo3=z in a
              // parent roster. We want to perform the following
              // operations:
              //
              // child_roster.replace_node_id(r,z)
              // child_roster.replace_node_id(q,y)
              // child_roster.replace_node_id(p,x)
              //
              // where "foo1" is actually "", the root dir.

              // Before we perform this, however, we want to ensure that
              // none of the existing target nodes (x,y,z) are killed in
              // any of the parent rosters. If any is killed -- defined
              // by saying that its birth revision fails to dominate one
              // of the parent rosters -- then we want to *avoid* making
              // the replacements, and leave this node as its own child.

              bool replace_this_node = true;
              if (parent_rosters.size() > 1)
                {
                  std::map<node_id, u64> birth_revs;
                  
                  for (node_id other_id = other_node->self; 
                       ! null_node(other_id); 
                       other_id = parent_roster->get_node(other_id)->parent)
                    {
                      revision_id birth_rev = safe_get(*parent_marking, 
                                                       other_id).birth_revision;
                      birth_revs.insert(std::make_pair(other_id, 
                                                       safe_get(new_rev_to_node,
                                                                birth_rev)));
                    }
                  replace_this_node = 
                    viable_replacement(birth_revs, parent_rosters, 
                                       child_to_parents);
                }
                  
              if (replace_this_node)
                {
                  for (node_id other_id = other_node->self; 
                       ! null_node(other_id); 
                       other_id = parent_roster->get_node(other_id)->parent)
                    {
                      node_id next_nid = child_roster.get_node(nid)->parent;
                      child_roster.replace_node_id(nid, other_id);
                      nid = next_nid;
                    }
                  I(null_node(nid));
                  break;
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

void
dump(current_rev_debugger const & d, std::string & out)
{
  typedef std::multimap<u64, std::pair<cert_name, cert_value> >::const_iterator ci;
  std::pair<ci,ci> range = d.agraph.certs.equal_range(d.node);
  for(ci i = range.first; i != range.second; ++i)
    {
      if (i->first == d.node)
        {
          out += "cert '" + i->second.first() + "'";
          out += "= '" + i->second.second() + "'\n";
        }
    }
}


typedef std::map<file_path, std::map<std::string, std::string> > oldstyle_attr_map;

static void 
read_oldstyle_dot_mt_attrs(data const & dat, oldstyle_attr_map & attr)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, ".mt-attrs");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  std::string file, name, value;

  attr.clear();

  while (parser.symp("file"))
    {
      parser.sym();
      parser.str(file);
      file_path fp = file_path_internal(file);

      while (parser.symp() && 
             !parser.symp("file"))
        {
          parser.sym(name);
          parser.str(value);
          attr[fp][name] = value;
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

  typedef std::multimap<u64,u64>::const_iterator ci;
  std::multimap<u64,u64> parent_to_child_map;
  std::deque<u64> work;
  std::set<u64> done;

  {
    // Set up the parent->child mapping and prime the work queue

    std::set<u64> parents, children;
    for (std::multimap<u64, u64>::const_iterator i = ancestry.begin();
         i != ancestry.end(); ++i)
      {
        parent_to_child_map.insert(std::make_pair(i->second, i->first));
        children.insert(i->first);
        parents.insert(i->second);
      }
    
    set_difference(parents.begin(), parents.end(),
                   children.begin(), children.end(),
                   std::back_inserter(work));
  }

  while (!work.empty())
    {
      
      u64 child = work.front();

      current_rev_debugger dbg(child, *this);
      MM(dbg);

      work.pop_front();
      std::pair<ci,ci> parent_range = ancestry.equal_range(child);
      std::set<u64> parents;
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
          L(F("processing node %d\n") % child);

          manifest_id old_child_mid;
          manifest_map old_child_man;

          get_node_manifest(child, old_child_mid);
          app.db.get_manifest(old_child_mid, old_child_man);

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
                  boost::shared_ptr<roster_t> ros = boost::shared_ptr<roster_t>(new roster_t());
                  boost::shared_ptr<marking_map> mm = boost::shared_ptr<marking_map>(new marking_map());
                  app.db.get_roster(safe_get(node_to_new_rev, parent), *ros, *mm);
                  safe_insert(parent_rosters, std::make_pair(parent, std::make_pair(ros, mm)));
                }
            }

          file_path attr_path = file_path_internal(".mt-attrs");

          roster_t child_roster;
          MM(child_roster);
          temp_node_id_source nis;
          for (manifest_map::const_iterator i = old_child_man.begin();
               i != old_child_man.end(); ++i)
            {
              if (!(i->first == attr_path))
                insert_into_roster_reusing_parent_entries(i->first, i->second,
                                                          parent_rosters,
                                                          nis, child_roster,
                                                          new_rev_to_node,
                                                          ancestry);
            }
          
          // migrate attributes out of .mt-attrs
          {
            manifest_map::const_iterator i = old_child_man.find(attr_path);
            if (i != old_child_man.end())
              {
                file_data dat;
                app.db.get_file_version(i->second, dat);
                oldstyle_attr_map attrs;
                read_oldstyle_dot_mt_attrs(dat.inner(), attrs);
                for (oldstyle_attr_map::const_iterator j = attrs.begin();
                     j != attrs.end(); ++j)
                  {
                    split_path sp;
                    j->first.split(sp);
                    if (child_roster.has_node(sp))
                      {
                        std::map<std::string, std::string> const &
                          fattrs = j->second;
                        for (std::map<std::string, std::string>::const_iterator
                               k = fattrs.begin();
                             k != fattrs.end(); ++k)
                          child_roster.set_attr(sp,
                                                attr_key(k->first),
                                                attr_value(k->second));
                      }
                  }
              }
          }
                    

          revision_set rev;
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
              boost::shared_ptr<roster_t> parent_roster = i->second.first;
              boost::shared_ptr<cset> cs = boost::shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs); 
              manifest_id parent_manifest_id;
              calculate_ident(*parent_roster, parent_manifest_id);
              safe_insert(rev.edges, std::make_pair(parent_rid, 
                                                    std::make_pair(parent_manifest_id, cs)));
                                                                      
            }

          // It is possible that we're at a "root" node here -- a node
          // which had no parent in the old rev graph -- in which case we
          // synthesize an edge from the empty revision to the current,
          // containing a cset which adds all the files in the child.

          if (rev.edges.empty())
            {
              revision_id parent_rid;
              boost::shared_ptr<roster_t> parent_roster = boost::shared_ptr<roster_t>(new roster_t());
              boost::shared_ptr<cset> cs = boost::shared_ptr<cset>(new cset());
              MM(*cs);
              make_cset(*parent_roster, child_roster, *cs); 
              manifest_id parent_manifest_id;
              safe_insert (rev.edges, std::make_pair (parent_rid, 
                                                      std::make_pair (parent_manifest_id, cs)));
              
            }

          // Finally, put all this excitement into the database and save
          // the new_rid for use in the cert-writing pass.

          revision_id new_rid;
          calculate_ident(rev, new_rid);
          node_to_new_rev.insert(std::make_pair(child, new_rid));
          new_rev_to_node.insert(std::make_pair(new_rid, child));

          /*
          P(F("------------------------------------------------\n"));
          P(F("made revision %s with %d edges, manifest id = %s\n")
            % new_rid % rev.edges.size() % rev.new_manifest);

          {
            std::string rtmp;
            data dtmp;
            dump(dbg, rtmp);
            write_revision_set(rev, dtmp);
            P(F("%s\n") % rtmp);
            P(F("%s\n") % dtmp);
          }
          P(F("------------------------------------------------\n"));
          */

          if (!app.db.revision_exists (new_rid))
            {
              L(F("mapped node %d to revision %s\n") % child % new_rid);
              app.db.put_revision(new_rid, rev);
              ++n_revs_out;
            }
          else
            {
              L(F("skipping already existing revision %s\n") % new_rid);
            }
          
          // Mark this child as done, hooray!
          safe_insert(done, child);

          // Extend the work queue with all the children of this child
          std::pair<ci,ci> grandchild_range = parent_to_child_map.equal_range(child);
          for (ci i = grandchild_range.first; 
               i != grandchild_range.second; ++i)
            {
              if (i->first != child)
                continue;
              work.push_back(i->second);
            }
        }
    }
}

void 
build_roster_style_revs_from_manifest_style_revs(app_state & app)
{
  global_sanity.set_relaxed(true);
  anc_graph graph(true, app);

  P(F("converting existing revision graph to new roster-style revisions\n"));
  std::multimap<revision_id, revision_id> existing_graph;

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    get_user_key(key,app);
    require_password(key, app);
  }

  app.db.get_revision_ancestry(existing_graph);
  for (std::multimap<revision_id, revision_id>::const_iterator i = existing_graph.begin();
       i != existing_graph.end(); ++i)
    {
      if (!null_id(i->first))
        {
          u64 parent_node = graph.add_node_for_oldstyle_revision(i->first);
          u64 child_node = graph.add_node_for_oldstyle_revision(i->second);
          graph.add_node_ancestry(child_node, parent_node);
        }
    }

  global_sanity.set_relaxed(false);
  graph.rebuild_ancestry();
}


void 
build_changesets_from_manifest_ancestry(app_state & app)
{
  anc_graph graph(false, app);

  P(F("rebuilding revision graph from manifest certs\n"));

  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    get_user_key(key,app);
    require_password(key, app);
  }

  std::vector< manifest<cert> > tmp;
  app.db.get_manifest_certs(cert_name("ancestor"), tmp);
  erase_bogus_certs(tmp, app);

  for (std::vector< manifest<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id child, parent;
      child = i->inner().ident;
      parent = hexenc<id>(tv());

      u64 parent_node = graph.add_node_for_old_manifest(parent);
      u64 child_node = graph.add_node_for_old_manifest(child);
      graph.add_node_ancestry(child_node, parent_node);
    }
  
  graph.rebuild_ancestry();
}

// i/o stuff

namespace 
{
  namespace syms
  {
    std::string const old_revision("old_revision");
    std::string const new_manifest("new_manifest");
    std::string const old_manifest("old_manifest");
  }
}


// HACK: this is a special reader which picks out the new_manifest field in
// a revision; it ignores all other symbols. This is because, in the
// pre-roster database, we have revisions holding change_sets, not
// csets. If we apply the cset reader to them, they fault. We need to
// *partially* read them, however, in order to get the manifest IDs out of
// the old revisions (before we delete the revs and rebuild them)

static void 
get_manifest_for_rev(app_state & app,
                     revision_id const & ident,
                     manifest_id & mid)
{
  revision_data dat;
  app.db.get_revision(ident,dat);
  std::istringstream iss(dat.inner()());
  basic_io::input_source src(iss, "revision");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  while (pars.symp())
    {
      if (pars.symp(syms::new_manifest))
        {
          std::string tmp;
          pars.sym();
          pars.hex(tmp);
          mid = manifest_id(tmp);
          return;
        }
      else
        pars.sym();
    }
  I(false);
}


void 
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{       
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, edge_old_revision(e).inner()());
  st.push_hex_pair(syms::old_manifest, edge_old_manifest(e).inner()());
  printer.print_stanza(st);
  print_cset(printer, edge_changes(e)); 
}


void 
print_revision(basic_io::printer & printer,
               revision_set const & rev)
{
  rev.check_sane();
  basic_io::stanza st; 
  st.push_hex_pair(syms::new_manifest, rev.new_manifest.inner()());
  printer.print_stanza(st);
  for (edge_map::const_iterator edge = rev.edges.begin();
       edge != rev.edges.end(); ++edge)
    print_edge(printer, *edge);
}


void 
parse_edge(basic_io::parser & parser,
           edge_map & es)
{
  boost::shared_ptr<cset> cs(new cset());
  manifest_id old_man;
  revision_id old_rev;
  std::string tmp;
  
  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = revision_id(tmp);
  
  parser.esym(syms::old_manifest);
  parser.hex(tmp);
  old_man = manifest_id(tmp);
  
  parse_cset(parser, *cs);

  es.insert(std::make_pair(old_rev, std::make_pair(old_man, cs)));
}


void 
parse_revision(basic_io::parser & parser,
               revision_set & rev)
{
  rev.edges.clear();
  std::string tmp;
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
  rev.new_manifest = manifest_id(tmp);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, rev.edges);
  rev.check_sane();
}

void 
read_revision_set(data const & dat,
                  revision_set & rev)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "revision");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_revision(pars, rev);
  I(src.lookahead == EOF);
  rev.check_sane();
}

void 
read_revision_set(revision_data const & dat,
                  revision_set & rev)
{
  read_revision_set(dat.inner(), rev);
  rev.check_sane();
}

static void write_insane_revision_set(revision_set const & rev,
                                      data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_revision(pr, rev);
  dat = data(oss.str());
}

void
dump(revision_set const & rev, std::string & out)
{
  data dat;
  write_insane_revision_set(rev, dat);
  out = dat();
}

void
write_revision_set(revision_set const & rev,
                   data & dat)
{
  rev.check_sane();
  write_insane_revision_set(rev, dat);
}

void
write_revision_set(revision_set const & rev,
                   revision_data & dat)
{
  data d;
  write_revision_set(rev, d);
  dat = revision_data(d);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"

static void 
revision_test()
{
}

void 
add_revision_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&revision_test));
}


#endif // BUILD_UNIT_TESTS
