#include "base.hh"
#include <map>
#include <utility>
#include <list>
#include <boost/shared_ptr.hpp>

#include "sanity.hh"
#include "graph.hh"
#include "safe_map.hh"
#include "numeric_vocab.hh"
#include "hash_map.hh"
#include "vocab_hash.hh"
#include "rev_height.hh"
#include "transforms.hh"

using boost::shared_ptr;
using std::string;
using std::vector;
using std::set;
using std::pair;
using std::map;
using std::multimap;
using std::make_pair;
using std::list;

using hashmap::hash_set;

void
get_reconstruction_path(id const & start,
                        reconstruction_graph const & graph,
                        reconstruction_path & path)
{
  // This function does a breadth-first search from a starting point, until it
  // finds some node that matches an arbitrary condition.  The intended usage
  // is for finding reconstruction paths in a database of deltas -- we start
  // from the node we want to reconstruct, and follow existing deltas outward
  // until we reach a full-text base.  We return the shortest path from
  // 'start' to a base version.
  //
  // The algorithm involves keeping a set of parallel linear paths, starting
  // from 'start', that move forward through the DAG until we hit a base.
  //
  // On each iteration, we extend every active path by one step. If our
  // extension involves a fork, we duplicate the path. If any path
  // contains a cycle, we fault.
  //
  // If, by extending a path C, we enter a node which another path
  // D has already seen, we kill path C. This avoids the possibility of
  // exponential growth in the number of paths due to extensive forking
  // and merging.

  // Long ago, we used to do this with the boost graph library, but it
  // involved loading too much of the storage graph into memory at any
  // moment. this imperative version only loads the descendents of the
  // reconstruction node, so it much cheaper in terms of memory.

  vector< shared_ptr<reconstruction_path> > live_paths;

  {
    shared_ptr<reconstruction_path> pth0 = shared_ptr<reconstruction_path>(new reconstruction_path());
    pth0->push_back(start);
    live_paths.push_back(pth0);
  }

  shared_ptr<reconstruction_path> selected_path;
  set<id> seen_nodes;

  while (!selected_path)
    {
      vector< shared_ptr<reconstruction_path> > next_paths;

      I(!live_paths.empty());
      for (vector<shared_ptr<reconstruction_path> >::const_iterator i = live_paths.begin();
           i != live_paths.end(); ++i)
        {
          shared_ptr<reconstruction_path> pth = *i;
          id tip = pth->back();

          if (graph.is_base(tip))
            {
              selected_path = pth;
              break;
            }
          else
            {
              // This tip is not a root, so extend the path.
              set<id> next;
              graph.get_next(tip, next);
              I(!next.empty());

              // Replicate the path if there's a fork.
              bool first = true;
              for (set<id>::const_iterator j = next.begin();
                    j != next.end(); ++j)
                {
                  if (global_sanity.debug_p())
                    L(FL("considering %s -> %s") % tip % *j);
                  if (seen_nodes.find(*j) == seen_nodes.end())
                    {
                      shared_ptr<reconstruction_path> pthN;
                      if (first)
                        {
                          pthN = pth;
                          first = false;
                        }
                      else
                        {
                          // NOTE: this is not the first iteration of the loop, and
                          // the first iteration appended one item to pth.  So, we
                          // want to remove one before we use it.  (Why not just
                          // copy every time?  Because that makes this into an
                          // O(n^2) algorithm, in the common case where there is
                          // only one direction to go at each stop.)
                          pthN = shared_ptr<reconstruction_path>(new reconstruction_path(*pth));
                          I(!pthN->empty());
                          pthN->pop_back();
                        }
                      // check for a cycle... not that anything would break if
                      // there were one, but it's nice to let us know we have a bug
                      for (reconstruction_path::const_iterator k = pthN->begin(); k != pthN->end(); ++k)
                        I(*k != *j);
                      pthN->push_back(*j);
                      next_paths.push_back(pthN);
                      seen_nodes.insert(*j);
                    }
                }
            }
        }

      I(selected_path || !next_paths.empty());
      live_paths = next_paths;
    }

  path = *selected_path;
}


#ifdef BUILD_UNIT_TESTS

#include <map>
#include "unit_tests.hh"
#include "randomizer.hh"

#include "transforms.hh"
#include "lexical_cast.hh"

using boost::lexical_cast;
using std::pair;

typedef std::multimap<id, id> rg_map;
struct mock_reconstruction_graph : public reconstruction_graph
{
  rg_map ancestry;
  set<id> bases;
  mock_reconstruction_graph(rg_map const & ancestry, set<id> const & bases)
    : ancestry(ancestry), bases(bases)
  {}
  virtual bool is_base(id const & node) const
  {
    return bases.find(node) != bases.end();
  }
  virtual void get_next(id const & from, set<id> & next) const
  {
    typedef rg_map::const_iterator ci;
    pair<ci, ci> range = ancestry.equal_range(from);
    for (ci i = range.first; i != range.second; ++i)
      next.insert(i->second);
  }
};

static void
make_random_reconstruction_graph(size_t num_nodes, size_t num_random_edges,
                                 size_t num_random_bases,
                                 vector<id> & all_nodes, rg_map & ancestry,
                                 set<id> & bases,
                                 randomizer & rng)
{
  for (size_t i = 0; i != num_nodes; ++i)
    {
      id hash;
      string s(lexical_cast<string>(i));
      calculate_ident(data(s), hash);
      all_nodes.push_back(hash);
    }
  // We put a single long chain of edges in, to make sure that everything is
  // reconstructable somehow.
  for (size_t i = 1; i != num_nodes; ++i)
    ancestry.insert(make_pair(idx(all_nodes, i - 1), idx(all_nodes, i)));
  bases.insert(all_nodes.back());
  // Then we insert a bunch of random edges too.  These edges always go
  // forwards, to avoid creating cycles (which make get_reconstruction_path
  // unhappy).
  for (size_t i = 0; i != num_random_edges; ++i)
    {
      size_t from_idx = rng.uniform(all_nodes.size() - 1);
      size_t to_idx = from_idx + 1 + rng.uniform(all_nodes.size() - 1 - from_idx);
      ancestry.insert(make_pair(idx(all_nodes, from_idx),
                                idx(all_nodes, to_idx)));
    }
  // And a bunch of random bases.
  for (size_t i = 0; i != num_random_bases; ++i)
    bases.insert(idx(all_nodes, rng.uniform(all_nodes.size())));
}

static void
check_reconstruction_path(id const & start, reconstruction_graph const & graph,
                          reconstruction_path const & path)
{
  I(!path.empty());
  I(*path.begin() == start);
  reconstruction_path::const_iterator last = path.end();
  --last;
  I(graph.is_base(*last));
  for (reconstruction_path::const_iterator i = path.begin(); i != last; ++i)
    {
      set<id> children;
      graph.get_next(*i, children);
      reconstruction_path::const_iterator next = i;
      ++next;
      I(children.find(*next) != children.end());
    }
}

static void
run_get_reconstruction_path_tests_on_random_graph(size_t num_nodes,
                                                  size_t num_random_edges,
                                                  size_t num_random_bases,
                                                  randomizer & rng)
{
  vector<id> all_nodes;
  rg_map ancestry;
  set<id> bases;
  make_random_reconstruction_graph(num_nodes, num_random_edges, num_random_bases,
                                   all_nodes, ancestry, bases,
                                   rng);
  mock_reconstruction_graph graph(ancestry, bases);
  for (vector<id>::const_iterator i = all_nodes.begin();
       i != all_nodes.end(); ++i)
    {
      reconstruction_path path;
      get_reconstruction_path(*i, graph, path);
      check_reconstruction_path(*i, graph, path);
    }
}

UNIT_TEST(graph, random_get_reconstruction_path)
{
  randomizer rng;
  // Some arbitrary numbers.
  run_get_reconstruction_path_tests_on_random_graph(100, 100, 10, rng);
  run_get_reconstruction_path_tests_on_random_graph(100, 200, 5, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 1000, 50, rng);
  run_get_reconstruction_path_tests_on_random_graph(1000, 2000, 100, rng);
}

#endif // BUILD_UNIT_TESTS


// graph is a parent->child map
void toposort_rev_ancestry(rev_ancestry_map const & graph,
                           vector<revision_id> & revisions)
{
  typedef multimap<revision_id, revision_id>::const_iterator gi;
  typedef map<revision_id, int>::iterator pi;
  
  revisions.clear();
  // determine the number of parents for each rev
  map<revision_id, int> pcount;
  for (gi i = graph.begin(); i != graph.end(); ++i)
    pcount.insert(make_pair(i->first, 0));
  for (gi i = graph.begin(); i != graph.end(); ++i)
    ++(pcount[i->second]);

  // find the set of graph roots
  list<revision_id> roots;
  for (pi i = pcount.begin(); i != pcount.end(); ++i)
    if(i->second==0)
      roots.push_back(i->first);

  while (!roots.empty())
    {
      revision_id cur = roots.front();
      roots.pop_front();
      if (!null_id(cur))
        revisions.push_back(cur);
      
      for(gi i = graph.lower_bound(cur);
          i != graph.upper_bound(cur); i++)
        if(--(pcount[i->second]) == 0)
          roots.push_back(i->second);
    }
}


// get_uncommon_ancestors
typedef std::pair<rev_height, revision_id> height_rev_pair;

static void
advance_frontier(set<height_rev_pair> & frontier,
                 hash_set<revision_id> & seen,
                 rev_graph const & rg)
{
  const height_rev_pair h_node = *frontier.rbegin();
  const revision_id & node(h_node.second);
  frontier.erase(h_node);
  set<revision_id> parents;
  rg.get_parents(node, parents);
  for (set<revision_id>::const_iterator r = parents.begin();
        r != parents.end(); r++)
  {
    if (seen.find(*r) == seen.end())
    {
      rev_height h;
      rg.get_height(*r, h);
      frontier.insert(make_pair(h, *r));
      seen.insert(*r);
    }
  }
}

void
get_uncommon_ancestors(revision_id const & a,
                       revision_id const & b,
                       rev_graph const & rg,
                       set<revision_id> & a_uncommon_ancs,
                       set<revision_id> & b_uncommon_ancs)
{
  a_uncommon_ancs.clear();
  b_uncommon_ancs.clear();

  // We extend a frontier from each revision until it reaches
  // a revision that has been seen by the other frontier. By
  // traversing in ascending height order we can ensure that
  // any common ancestor will have been 'seen' by both sides
  // before it is traversed.

  set<height_rev_pair> a_frontier, b_frontier, common_frontier;
  {
    rev_height h;
    rg.get_height(a, h);
    a_frontier.insert(make_pair(h, a));
    rg.get_height(b, h);
    b_frontier.insert(make_pair(h, b));
  }
  
  hash_set<revision_id> a_seen, b_seen, common_seen;
  a_seen.insert(a);
  b_seen.insert(b);
  
  while (!a_frontier.empty() || !b_frontier.empty())
  {
    // We take the leaf-most (ie highest) height entry from any frontier.
    // Note: the default height is the lowest possible.
    rev_height a_height, b_height, common_height;
    if (!a_frontier.empty())
      a_height = a_frontier.rbegin()->first;
    if (!b_frontier.empty())
      b_height = b_frontier.rbegin()->first;
    if (!common_frontier.empty())
      common_height = common_frontier.rbegin()->first;

    if (a_height > b_height && a_height > common_height)
      {
        a_uncommon_ancs.insert(a_frontier.rbegin()->second);
        advance_frontier(a_frontier, a_seen, rg);
      }
    else if (b_height > a_height && b_height > common_height)
      {
        b_uncommon_ancs.insert(b_frontier.rbegin()->second);
        advance_frontier(b_frontier, b_seen, rg);
      }
    else if (common_height > a_height && common_height > b_height)
      {
        advance_frontier(common_frontier, common_seen, rg);
      }
    else if (a_height == b_height) // may or may not also == common_height
      {
        // if both frontiers are the same, then we can safely say that
        // we've found all uncommon ancestors. This stopping condition
        // can result in traversing more nodes than required, but is simple.
        if (a_frontier == b_frontier)
          break;

        common_frontier.insert(*a_frontier.rbegin());
        a_frontier.erase(*a_frontier.rbegin());
        b_frontier.erase(*b_frontier.rbegin());
      }
    else if (a_height == common_height)
      {
        a_frontier.erase(*a_frontier.rbegin());
      }
    else if (b_height == common_height)
      {
        b_frontier.erase(*b_frontier.rbegin());
      }
    else
      I(false);
  }  
}

#ifdef BUILD_UNIT_TESTS

#include <map>
#include "unit_tests.hh"
#include "randomizer.hh"
#include "roster.hh"


static void
get_all_ancestors(revision_id const & start, rev_ancestry_map const & child_to_parent_map,
                  set<revision_id> & ancestors)
{
  ancestors.clear();
  vector<revision_id> frontier;
  frontier.push_back(start);
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if (ancestors.find(rid) != ancestors.end())
        continue;
      safe_insert(ancestors, rid);
      typedef rev_ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rid);
      for (ci i = range.first; i != range.second; ++i)
        frontier.push_back(i->second);
    }
}

struct mock_rev_graph : rev_graph
{
  mock_rev_graph(rev_ancestry_map const & child_to_parent_map)
    : child_to_parent_map(child_to_parent_map)
  {
    // assign sensible heights
    height_map.clear();
    
    // toposort expects parent->child
    rev_ancestry_map parent_to_child;
    for (rev_ancestry_map::const_iterator i = child_to_parent_map.begin();
      i != child_to_parent_map.end(); i++)
    {
      parent_to_child.insert(make_pair(i->second, i->first));
    }
    vector<revision_id> topo_revs;
    toposort_rev_ancestry(parent_to_child, topo_revs);
    
    // this is ugly but works. just give each one a sequential number.
    rev_height top = rev_height::root_height();
    u32 num = 1;
    for (vector<revision_id>::const_iterator r = topo_revs.begin();
      r != topo_revs.end(); r++, num++)
    {
      height_map.insert(make_pair(*r, top.child_height(num)));
    }
  }
  
  virtual void get_parents(revision_id const & node, set<revision_id> & parents) const
  {
    parents.clear();
    for (rev_ancestry_map::const_iterator i = child_to_parent_map.lower_bound(node);
      i != child_to_parent_map.upper_bound(node); i++)
    {
      if (!null_id(i->second))
        safe_insert(parents, i->second);
    }
  }
  
  virtual void get_children(revision_id const & node, set<revision_id> & parents) const
  {
    // not required
    I(false);
  }
  
  virtual void get_height(revision_id const & rev, rev_height & h) const
  {
    MM(rev);
    h = safe_get(height_map, rev);
  }
  
  
  rev_ancestry_map const & child_to_parent_map;
  map<revision_id, rev_height> height_map;
};


static void
run_a_get_uncommon_ancestors_test(rev_ancestry_map const & child_to_parent_map,
                                  revision_id const & left, revision_id const & right)
{
  set<revision_id> true_left_ancestors, true_right_ancestors;
  get_all_ancestors(left, child_to_parent_map, true_left_ancestors);
  get_all_ancestors(right, child_to_parent_map, true_right_ancestors);
  set<revision_id> true_left_uncommon_ancestors, true_right_uncommon_ancestors;
  MM(true_left_uncommon_ancestors);
  MM(true_right_uncommon_ancestors);
  set_difference(true_left_ancestors.begin(), true_left_ancestors.end(),
                 true_right_ancestors.begin(), true_right_ancestors.end(),
                 inserter(true_left_uncommon_ancestors, true_left_uncommon_ancestors.begin()));
  set_difference(true_right_ancestors.begin(), true_right_ancestors.end(),
                 true_left_ancestors.begin(), true_left_ancestors.end(),
                 inserter(true_right_uncommon_ancestors, true_right_uncommon_ancestors.begin()));
      
  set<revision_id> calculated_left_uncommon_ancestors, calculated_right_uncommon_ancestors;
  MM(calculated_left_uncommon_ancestors);
  MM(calculated_right_uncommon_ancestors);
  mock_rev_graph rg(child_to_parent_map);
  get_uncommon_ancestors(left, right, rg,
                         calculated_left_uncommon_ancestors,
                         calculated_right_uncommon_ancestors);
  I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
  I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
  get_uncommon_ancestors(right, left, rg,
                         calculated_right_uncommon_ancestors,
                         calculated_left_uncommon_ancestors);
  I(calculated_left_uncommon_ancestors == true_left_uncommon_ancestors);
  I(calculated_right_uncommon_ancestors == true_right_uncommon_ancestors);
}

UNIT_TEST(graph, get_uncommon_ancestors_nasty_convexity_case)
{
  // This tests the nasty case described in the giant comment above
  // get_uncommon_ancestors:
  // 
  //              9
  //              |\                  . Extraneous dots brought to you by the
  //              8 \                 . Committee to Shut Up the C Preprocessor
  //             /|  \                . (CSUCPP), and viewers like you and me.
  //            / |   |
  //           /  7   |
  //          |   |   |
  //          |   6   |
  //          |   |   |
  //          |   5   |
  //          |   |   |
  //          |   4   |
  //          |   |   |
  //          |   :   |  <-- insert arbitrarily many revisions at the ellipsis
  //          |   :   |
  //          |   |   |
  //          1   2   3
  //           \ / \ /
  //            L   R

  rev_ancestry_map child_to_parent_map;
  revision_id left(fake_id()), right(fake_id());
  revision_id one(fake_id()), two(fake_id()), eight(fake_id()), three(fake_id()), nine(fake_id());
  MM(left);
  MM(right);
  MM(one);
  MM(two);
  MM(three);
  MM(eight);
  MM(nine);
  child_to_parent_map.insert(make_pair(left, one));
  child_to_parent_map.insert(make_pair(one, eight));
  child_to_parent_map.insert(make_pair(eight, nine));
  child_to_parent_map.insert(make_pair(right, three));
  child_to_parent_map.insert(make_pair(three, nine));

  revision_id middle(fake_id());
  child_to_parent_map.insert(make_pair(left, two));
  child_to_parent_map.insert(make_pair(right, two));
  // We insert a _lot_ of revisions at the ellipsis, to make sure that
  // whatever sort of step-size is used on the expansion, we can't take the
  // entire middle portion in one big gulp and make the test pointless.
  for (int i = 0; i != 1000; ++i)
    {
      revision_id next(fake_id());
      child_to_parent_map.insert(make_pair(middle, next));
      middle = next;
    }
  child_to_parent_map.insert(make_pair(middle, eight));

  run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
}

double const new_root_freq = 0.05;
double const merge_node_freq = 0.2;
double const skip_up_freq = 0.5;

static revision_id
pick_node_from_set(set<revision_id> const & heads,
                   randomizer & rng)
{
  I(!heads.empty());
  size_t which_start = rng.uniform(heads.size());
  set<revision_id>::const_iterator i = heads.begin();
  for (size_t j = 0; j != which_start; ++j)
    ++i;
  return *i;
}

static revision_id
pick_node_or_ancestor(set<revision_id> const & heads,
                      rev_ancestry_map const & child_to_parent_map,
                      randomizer & rng)
{
  revision_id rev = pick_node_from_set(heads, rng);
  // now we recurse up from this starting point
  while (rng.bernoulli(skip_up_freq))
    {
      typedef rev_ancestry_map::const_iterator ci;
      pair<ci,ci> range = child_to_parent_map.equal_range(rev);
      if (range.first == range.second)
        break;
      ci second = range.first;
      ++second;
      if (second == range.second)
        // there was only one parent
        rev = range.first->second;
      else
        {
          // there are two parents, pick one randomly
          if (rng.flip())
            rev = range.first->second;
          else
            rev = second->second;
        }
    }
  return rev;
}

static void
make_random_graph(size_t num_nodes,
                  rev_ancestry_map & child_to_parent_map,
                  vector<revision_id> & nodes,
                  randomizer & rng)
{
  set<revision_id> heads;

  for (size_t i = 0; i != num_nodes; ++i)
    {
      revision_id new_rid = revision_id(fake_id());
      nodes.push_back(new_rid);
      set<revision_id> parents;
      if (heads.empty() || rng.bernoulli(new_root_freq))
        parents.insert(revision_id());
      else if (rng.bernoulli(merge_node_freq) && heads.size() > 1)
        {
          // maybe we'll pick the same node twice and end up not doing a
          // merge, oh well...
          parents.insert(pick_node_from_set(heads, rng));
          parents.insert(pick_node_from_set(heads, rng));
        }
      else
        {
          parents.insert(pick_node_or_ancestor(heads, child_to_parent_map, rng));
        }
      for (set<revision_id>::const_iterator j = parents.begin();
           j != parents.end(); ++j)
        {
          heads.erase(*j);
          child_to_parent_map.insert(std::make_pair(new_rid, *j));
        }
      safe_insert(heads, new_rid);
    }
}

static void
run_a_get_uncommon_ancestors_random_test(size_t num_nodes,
                                         size_t iterations,
                                         randomizer & rng)
{
  rev_ancestry_map child_to_parent_map;
  vector<revision_id> nodes;
  make_random_graph(num_nodes, child_to_parent_map, nodes, rng);
  for (size_t i = 0; i != iterations; ++i)
    {
      L(FL("get_uncommon_ancestors: random test %s-%s") % num_nodes % i);
      revision_id left = idx(nodes, rng.uniform(nodes.size()));
      revision_id right = idx(nodes, rng.uniform(nodes.size()));
      run_a_get_uncommon_ancestors_test(child_to_parent_map, left, right);
    }
}

UNIT_TEST(graph, get_uncommon_ancestors_randomly)
{
  randomizer rng;
  run_a_get_uncommon_ancestors_random_test(100, 100, rng);
  run_a_get_uncommon_ancestors_random_test(1000, 100, rng);
  run_a_get_uncommon_ancestors_random_test(10000, 1000, rng);
}


#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
