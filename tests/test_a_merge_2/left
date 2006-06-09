// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <boost/lexical_cast.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/shared_ptr.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "constants.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"


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
       p != imm_parents.end(); ++p)
    {
      if (null_id(*p))
        imm_parents.erase(p);
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
  for (std::map<ctx, shared_bitmap>::const_iterator e = dominators.begin(); 
       e != dominators.end(); ++e)
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

          if (bits->size() > pbits->size())
            pbits->resize(bits->size());

          if (pbits->size() > bits->size())
            bits->resize(pbits->size());

          if (first)
            {
              intersection = (*pbits);
              first = false;
            }
          else
            intersection &= (*pbits);
        }

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
  for (std::map<ctx, shared_bitmap>::const_iterator e = ancestors.begin(); 
       e != ancestors.end(); ++e)
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
//  dump_bitset_map(string const & hdr,
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
find_common_ancestor(revision_id const & left,
                     revision_id const & right,
                     revision_id & anc,
                     app_state & app)
{
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
  
  while (expand_ancestors(parents, ancestors, intern, app) ||
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


// 
// The idea with this algorithm is to walk from child up to ancestor,
// recursively, accumulating all the change_sets associated with
// intermediate nodes into *one big change_set*.
//
// clever readers will realize this is an overlapping-subproblem type
// situation and thus needs to keep a dynamic programming map to keep
// itself in linear complexity.
//
// in fact, we keep two: one which maps to computed results (partial_csets)
// and one which just keeps a set of all nodes we traversed
// (visited_nodes). in theory it could be one map with an extra bool stuck
// on each entry, but I think that would make it even less readable. it's
// already quite ugly.
//

static bool 
calculate_change_sets_recursive(revision_id const & ancestor,
                                revision_id const & child,
                                app_state & app,
                                change_set & cumulative_cset,
                                std::map<revision_id, boost::shared_ptr<change_set> > & partial_csets,
                                std::set<revision_id> & visited_nodes)
{

  if (ancestor == child)
    return true;

  visited_nodes.insert(child);

  bool relevant_child = false;

  revision_set rev;
  app.db.get_revision(child, rev);

  L(F("exploring changesets from parents of %s, seeking towards %s\n") 
    % child % ancestor);

  for(edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      bool relevant_parent = false;
      revision_id curr_parent = edge_old_revision(i);

      if (curr_parent.inner()().empty())
        continue;

      change_set cset_to_curr_parent;

      L(F("considering parent %s of %s\n") % curr_parent % child);

      std::map<revision_id, boost::shared_ptr<change_set> >::const_iterator j = 
        partial_csets.find(curr_parent);
      if (j != partial_csets.end()) 
        {
          // a recursive call has traversed this parent before and built an
          // existing cset. just reuse that rather than re-traversing
          cset_to_curr_parent = *(j->second);
          relevant_parent = true;
        }
      else if (visited_nodes.find(curr_parent) != visited_nodes.end())
        {
          // a recursive call has traversed this parent, but there was no
          // path from it to the root, so the parent is irrelevant. skip.
          relevant_parent = false;
        }
      else
        relevant_parent = calculate_change_sets_recursive(ancestor, curr_parent, app, 
                                                          cset_to_curr_parent, 
                                                          partial_csets,
                                                          visited_nodes);

      if (relevant_parent)
        {
          L(F("revision %s is relevant, composing with edge to %s\n") 
            % curr_parent % child);
          concatenate_change_sets(cset_to_curr_parent, edge_changes(i), cumulative_cset);
          relevant_child = true;
          break;
        }
      else
        L(F("parent %s of %s is not relevant\n") % curr_parent % child);
    }

  // store the partial edge from ancestor -> child, so that if anyone
  // re-traverses this edge they'll just fetch from the partial_edges
  // cache.
  if (relevant_child)
    partial_csets.insert(std::make_pair(child, 
                                        boost::shared_ptr<change_set>
                                        (new change_set(cumulative_cset))));
  
  return relevant_child;
}

void 
calculate_composite_change_set(revision_id const & ancestor,
                               revision_id const & child,
                               app_state & app,
                               change_set & composed)
{
  L(F("calculating composite changeset between %s and %s\n")
    % ancestor % child);
  std::set<revision_id> visited;
  std::map<revision_id, boost::shared_ptr<change_set> > partial;
  calculate_change_sets_recursive(ancestor, child, app, composed, partial, visited);
}

// migration stuff
//
// FIXME: these are temporary functions, once we've done the migration to
// revisions / changesets, we can remove them.

static void 
analyze_manifest_changes(app_state & app,
                         manifest_id const & parent, 
                         manifest_id const & child, 
                         change_set & cs)
{
  manifest_map m_parent, m_child;
  app.db.get_manifest(parent, m_parent);
  app.db.get_manifest(child, m_child);

  for (manifest_map::const_iterator i = m_parent.begin(); 
       i != m_parent.end(); ++i)
    {
      manifest_map::const_iterator j = m_child.find(manifest_entry_path(i));
      if (j == m_child.end())
        cs.delete_file(manifest_entry_path(i));
      else if (! (manifest_entry_id(i) == manifest_entry_id(j)))
        {
          cs.apply_delta(manifest_entry_path(i),
                         manifest_entry_id(i), 
                         manifest_entry_id(j));
        }       
    }
  for (manifest_map::const_iterator i = m_child.begin(); 
       i != m_child.end(); ++i)
    {
      manifest_map::const_iterator j = m_parent.find(manifest_entry_path(i));
      if (j == m_parent.end())
        cs.add_file(manifest_entry_path(i),
                    manifest_entry_id(i));
    }
}

static revision_id
construct_revisions(app_state & app,
                    manifest_id const & child,
                    std::multimap< manifest_id, manifest_id > const & ancestry,
                    std::map<manifest_id, revision_id> & mapped)
{
  revision_set rev;
  typedef std::multimap< manifest_id, manifest_id >::const_iterator ci;
  std::pair<ci,ci> range = ancestry.equal_range(child);
  for (ci i = range.first; i != range.second; ++i)
    {
      manifest_id parent(i->second);
      revision_id parent_rid;
      std::map<manifest_id, revision_id>::const_iterator j = mapped.find(parent);

      if (j != mapped.end())
        parent_rid = j->second;
      else
        {
          parent_rid = construct_revisions(app, parent, ancestry, mapped);
          P(F("inserting mapping %d : %s -> %s\n") % mapped.size() % parent % parent_rid);;
          mapped.insert(std::make_pair(parent, parent_rid));
        }
      
      change_set cs;
      analyze_manifest_changes(app, parent, child, cs);
      rev.edges.insert(std::make_pair(parent_rid,
                                      std::make_pair(parent, cs)));
    } 

  revision_id rid;
  if (rev.edges.empty())
    {
      P(F("ignoring empty revision for manifest %s\n") % child);  
      return rid;
    }

  rev.new_manifest = child;
  calculate_ident(rev, rid);

  if (!app.db.revision_exists (rid))
    {
      P(F("mapping manifest %s to revision %s\n") % child % rid);
      app.db.put_revision(rid, rev);
    }
  else
    {
      P(F("skipping additional path to revision %s\n") % rid);
    }

  // now hoist all the interesting certs up to the revision
  std::set<cert_name> cnames;
  cnames.insert(cert_name(branch_cert_name));
  cnames.insert(cert_name(date_cert_name));
  cnames.insert(cert_name(author_cert_name));
  cnames.insert(cert_name(tag_cert_name));
  cnames.insert(cert_name(changelog_cert_name));
  cnames.insert(cert_name(comment_cert_name));
  cnames.insert(cert_name(testresult_cert_name));

  std::vector< manifest<cert> > tmp;
  app.db.get_manifest_certs(child, tmp);
  erase_bogus_certs(tmp, app);      
  for (std::vector< manifest<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      if (cnames.find(i->inner().name) == cnames.end())
        continue;
      cert new_cert;
      cert_value tv;
      decode_base64(i->inner().value, tv);
      make_simple_cert(rid.inner(), i->inner().name, tv, app, new_cert);
      if (! app.db.revision_cert_exists(revision<cert>(new_cert)))
        app.db.put_revision_cert(revision<cert>(new_cert));
    }
  return rid;  
}


void 
build_changesets(app_state & app)
{
  std::vector< manifest<cert> > tmp;
  app.db.get_manifest_certs(cert_name("ancestor"), tmp);
  erase_bogus_certs(tmp, app);

  std::multimap< manifest_id, manifest_id > ancestry;
  std::set<manifest_id> heads;
  std::set<manifest_id> total;
  
  for (std::vector< manifest<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id child, parent;
      child = i->inner().ident;
      parent = hexenc<id>(tv());
      heads.insert(child);
      heads.erase(parent);
      total.insert(child);
      total.insert(parent);
      ancestry.insert(std::make_pair(child, parent));
    }
  
  P(F("found a total of %d manifests\n") % total.size());

  transaction_guard guard(app.db);
  std::map<manifest_id, revision_id> mapped;
  for (std::set<manifest_id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    {
      construct_revisions(app, *i, ancestry, mapped);
    }
  guard.commit();
}


// i/o stuff

std::string revision_file_name("revision");

namespace 
{
  namespace syms
  {
    std::string const old_revision("old_revision");
    std::string const new_manifest("new_manifest");
    std::string const old_manifest("old_manifest");
  }
}


void 
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{       
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, edge_old_revision(e).inner()());
  st.push_hex_pair(syms::old_manifest, edge_old_manifest(e).inner()());
  printer.print_stanza(st);
  print_change_set(printer, edge_changes(e)); 
}


void 
print_revision(basic_io::printer & printer,
               revision_set const & rev)
{
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
  change_set cs;
  manifest_id old_man;
  revision_id old_rev;
  std::string tmp;
  
  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = revision_id(tmp);
  
  parser.esym(syms::old_manifest);
  parser.hex(tmp);
  old_man = manifest_id(tmp);
  
  parse_change_set(parser, cs);

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
}

void 
read_revision_set(revision_data const & dat,
                  revision_set & rev)
{
  data unpacked;
  unpack(dat.inner(), unpacked);
  read_revision_set(unpacked, rev);
}

void
write_revision_set(revision_set const & rev,
                   data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_revision(pr, rev);
  dat = data(oss.str());
}

void
write_revision_set(revision_set const & rev,
                   revision_data & dat)
{
  data d;
  write_revision_set(rev, d);
  base64< gzip<data> > packed;
  pack(d, packed);
  dat = revision_data(packed);
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
