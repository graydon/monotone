// Copyright (C) 2007 Lapo Luchini <lapo@lapo.it>
// Copyright (C) 2007 Gabriele Dini Ciacci <dark.schneider@iol.it>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <iterator>
#include <set>
#include <vector>
#include <boost/tuple/tuple.hpp>

#include "cmd.hh"
#include "revision.hh"

using std::cout;
using std::pair;
using std::set;
using std::vector;
using std::ostream_iterator;

//namespace asciik { }

/**
 * Prints an ASCII-k chunk using the given revisions.
 */
//void asciik::print(set<revision_id> ids) {
//  database::get_revision_parents(revision_id const & id, set<revision_id> & parents)
//  for (iterator id = ids.begin(); id != ids.end(); ++id)
//    os << "Work on: " << id << "\n";
//}

static revision_id ghost; // valid but empty revision_id to be used as ghost value

CMD(asciik, N_("tree"), N_("SELECTOR"),
    N_("prints ASCII-art tree representation"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(args[0](), app));

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  //set<hexenc<id>> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  set<revision_id> revs;
  for (set<string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    {
      revision_id rid(*i);
      revs.insert(rid);
    }
  vector<revision_id> sorted;
  toposort(revs, sorted, app);
  vector<revision_id> curr_row;
  reverse(sorted.begin(), sorted.end()); //TODO: faster to use a reverse_iterator I guess, but that seems to give some problems
  for (vector<revision_id>::const_iterator rev = sorted.begin();
       rev != sorted.end(); ++rev)
//*  for (vector<revision_id>::const_reverse_iterator rev = sorted.rbegin();
//*       rev != sorted.rend(); ++rev)
    {
      // print row

//p    if curr_rev not in curr_row:
//p        curr_row.append(curr_rev)
      if (find(curr_row.begin(), curr_row.end(), *rev) == curr_row.end())
	curr_row.push_back(*rev);

//p    curr_loc = curr_row.index(curr_rev)
      //iterator_traits<vector<revision_id>::iterator>::difference_type
      int curr_loc = distance(curr_row.begin(),
	find(curr_row.begin(), curr_row.end(), *rev));
      //assert(curr_loc < size()); as it is surely found

//p    new_revs = []
//p    for p in parents:
//p        if p not in curr_row:
//p            new_revs.append(p)
      set<revision_id> parents;
      app.db.get_revision_parents(*rev, parents);

      set<revision_id> new_revs;
      for (set<revision_id>::const_iterator parent = parents.begin();
	   parent != parents.end(); )
	if (find(curr_row.begin(), curr_row.end(), *parent) == curr_row.end())
	  new_revs.insert(*parent);
//#2      set<revision_id> new_revs;
//#2      app.db.get_revision_parents(*rev, new_revs);
//#2      for (set<revision_id>::const_iterator parent = new_revs.begin();
//#2	   parent != new_revs.end(); )
//#2	if (find(curr_row.begin(), curr_row.end(), *parent) != curr_row.end())
//#2	  new_revs.erase(parent++);
//#2	else
//#2	  ++parent;

//p    next_row = list(curr_row)
//p    next_row[curr_loc:curr_loc + 1] = new_revs
      vector<revision_id> next_row(curr_row);
      next_row.insert(
	next_row.erase(next_row.begin() + curr_loc),
	new_revs.begin(), new_revs.end());

      //TODO:remove test print
      cout << "curr_row: ";
      copy(curr_row.begin(), curr_row.end(), ostream_iterator<revision_id>(cout, " "));
      cout << "\nnext_row: ";
      copy(next_row.begin(), next_row.end(), ostream_iterator<revision_id>(cout, " "));
      cout << "\n";

//p    # now next_row contains exactly the revisions it needs to, except that no
//p    # ghost handling has been done.

//p    no_ghost = without_a_ghost(next_row)
      vector<revision_id> no_ghost(curr_row);
      vector<revision_id>::iterator i_ghost = find(no_ghost.begin(),
	no_ghost.end(), ghost);
      if (i_ghost != no_ghost.end())
	no_ghost.erase(i_ghost);
//p
//p    if try_draw(curr_row, no_ghost, curr_loc, parents):
//p        return no_ghost
//p    if try_draw(curr_row, next_row, curr_loc, parents):
//p        return next_row
//p    if not new_revs: # this line has disappeared
//p        extra_ghost = with_a_ghost_added(next_row, curr_loc)
//p        if try_draw(curr_row, extra_ghost, curr_loc, parents):
//p            return extra_ghost
//p    assert False
    }
}

bool try_draw(const vector<revision_id> curr_row,
  const vector<revision_id> next_row, int curr_loc, set<revision_id> parents)
{
//p    curr_items = len(curr_row)
//p    next_items = len(next_row)
  size_t curr_items = curr_row.size();
  size_t next_items = next_row.size();

//p    curr_ghosts = []
//p    for i in xrange(curr_items):
//p        if curr_row[i] is None:
//p            curr_ghosts.append(i)
  vector<int> curr_ghosts;
  for (size_t i = 0; i < curr_items; ++i)
    if (curr_row[i] == ghost)
      curr_ghosts.insert(i);

//p    preservation_links = []
//p    have_shift = False
  vector<pair<int, int> > preservation_links;
  bool have_shift = false;

//p    for rev in curr_row:
//p        if rev is not None and rev in next_row:
//p            i = curr_row.index(rev)
//p            j = next_row.index(rev)
//p            if i != j:
//p                have_shift = True
//p            if abs(i - j) > 1:
//p                return False
//p            preservation_links.append((i, j))
  for (size_t i = 0; i < curr_items; ++i) {
    if (idx(curr_row, i) != ghost) {
      int j = distance(next_row.begin(), find(next_row.begin(), next_row.end(), curr_row[i]));
      if (j < news_row.size()) {
	int d = abs(i - j);
	if (d > 1)
	  return false;
	if (d != 0)
	  have_shift = true;
	preservation_links.insert(pair<int, int>(i, j));
      }
    }
  }

//p    parent_links = []
//p    for p in parents:
//p        i = curr_loc
//p        j = next_row.index(p)
//p        if abs(i - j) > 1 and have_shift:
//p            return False
//p        parent_links.append((i, j))
//p
//p    preservation_crosses = links_cross(preservation_links)
//p    parent_crosses = links_cross(parent_links)
//p    if preservation_crosses.intersection(parent_crosses):
//p        return False
//p
//p    links = preservation_links + parent_links
//p    draw(curr_items, next_items, curr_loc, links, curr_ghosts, curr_row[curr_loc])
//p    return True
}
