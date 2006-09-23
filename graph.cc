#include <boost/shared_ptr.hpp>

#include "sanity.hh"
#include "graph.hh"

using boost::shared_ptr;
using std::string;
using std::vector;
using std::set;

void
get_reconstruction_path(std::string const & start,
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
  set<string> seen_nodes;

  while (!selected_path)
    {
      vector< shared_ptr<reconstruction_path> > next_paths;

      I(!live_paths.empty());
      for (vector<shared_ptr<reconstruction_path> >::const_iterator i = live_paths.begin();
           i != live_paths.end(); ++i)
        {
          shared_ptr<reconstruction_path> pth = *i;
          string tip = pth->back();

          if (graph.is_base(tip))
            {
              selected_path = pth;
              break;
            }
          else
            {
              // This tip is not a root, so extend the path.
              set<string> next;
              graph.get_next(tip, next);
              I(!next.empty());

              // Replicate the path if there's a fork.
              bool first = true;
              for (set<string>::const_iterator j = next.begin(); j != next.end(); ++j)
                {
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

#include <boost/lexical_cast.hpp>

using boost::lexical_cast;
using std::pair;

typedef std::multimap<string, string> rg_map;
struct mock_reconstruction_graph : public reconstruction_graph
{
  rg_map ancestry;
  set<string> bases;
  mock_reconstruction_graph(rg_map const & ancestry, set<string> const & bases)
    : ancestry(ancestry), bases(bases)
  {}
  virtual bool is_base(string const & node) const
  {
    return bases.find(node) != bases.end();
  }
  virtual void get_next(string const & from, set<string> & next) const
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
                                 vector<string> & all_nodes, rg_map & ancestry,
                                 set<string> & bases,
                                 randomizer & rng)
{
  for (size_t i = 0; i != num_nodes; ++i)
    all_nodes.push_back(lexical_cast<string>(i));
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
check_reconstruction_path(string const & start, reconstruction_graph const & graph,
                          reconstruction_path const & path)
{
  I(!path.empty());
  I(*path.begin() == start);
  reconstruction_path::const_iterator last = path.end();
  --last;
  I(graph.is_base(*last));
  for (reconstruction_path::const_iterator i = path.begin(); i != last; ++i)
    {
      set<string> children;
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
  vector<string> all_nodes;
  rg_map ancestry;
  set<string> bases;
  make_random_reconstruction_graph(num_nodes, num_random_edges, num_random_bases,
                                   all_nodes, ancestry, bases,
                                   rng);
  mock_reconstruction_graph graph(ancestry, bases);
  for (vector<string>::const_iterator i = all_nodes.begin();
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
