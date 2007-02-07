// Copyright (C) 2007 Lapo Luchini <lapo@lapo.it>
// Copyright (C) 2007 Gabriele Dini Ciacci <dark.schneider@iol.it>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>
#include <vector>
#include <boost/tuple/tuple.hpp>

#include "cmd.hh"
#include "revision.hh"

using std::cout;
using std::insert_iterator;
using std::max;
using std::min;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::vector;

/*
BUGS:

1)

| | | | | | |\ \ \ \
| | | | | | o | | | |    145c71fb56cff358dd711773586ae6b5219b0cfc
| | | | | | |\ \ \ \ \

should be

| | | | | | |\ \ \ \
| | | | | | o \ \ \ \    145c71fb56cff358dd711773586ae6b5219b0cfc
| | | | | | |\ \ \ \ \

need some sort "inertia", if we moved sideways before and are moving
sideways now...

2)

It actually is possible to remove a ghost on the same line as a long
rightwards edge -- and it even looks better than not doing it, at least in
some cases.  Consider:

Current:

| | | o | | | | |    0f07da5974be8bf91495a34093efc635dcf1f01c
| | | |\ \ \ \ \ \
| | | | .-o | | | |    20037e09def77cc217679bf2f72baf5fa0415649
| | | |/|   | | | |
| | | o---. | | | |    de74b6e2bd612ba40f1afc3eba3f9a3d8f419135
| | | | |  \| | | |
| | | o |   | | | |    3fd16665caab9d942e6c5254b477587ff7d3722d
| | | | |  / / / /
| | | o | | | | |    38f3561cc4bf294b99436f98cd97fc707b422bfa
| | | | | | | | |
| | | | .-o | | |    59017bc836986a59fd1ac6fba4f78fe1045c67e9
| | | |/| | | | |
| | | o | | | | |    97e8f96bb34774003de428292edbdd562ca6d867
| | | | | | | | |

Desired:

| | | o | | | | |    0f07da5974be8bf91495a34093efc635dcf1f01c
| | | |\ \ \ \ \ \
| | | | .-o | | | |    20037e09def77cc217679bf2f72baf5fa0415649
| | | |/|   | | | |
| | | o-.   | | | |    de74b6e2bd612ba40f1afc3eba3f9a3d8f419135
| | | | |\ / / / /
| | | o | | | | |    3fd16665caab9d942e6c5254b477587ff7d3722d
| | | | | | | | |
| | | o | | | | |    38f3561cc4bf294b99436f98cd97fc707b422bfa
| | | | | | | | |
| | | | .-o | | |    59017bc836986a59fd1ac6fba4f78fe1045c67e9
| | | |/| | | | |
| | | o | | | | |    97e8f96bb34774003de428292edbdd562ca6d867
| | | | | | | | |

Possibly the no-shift-while-drawing-long-edges code could even be removed,
deferring to the no-edge-crossings code.




How this works:
  This is completely iterative; we have no lookahead whatsoever.  We output
    each line before even looking at the next.  (This means the layout is
    much less clever than it could be, because there is no global
    optimization; but it also means we can calculate these things in zero
    time, incrementally while running log.)

  Output comes in two-line chunks -- a "line", which contains exactly one
  node, and then an "interline", which contains edges that will link us to
  the next line.

  A design goal of the system is that you can always trivially increase the
  space between two "lines", by adding another "| | | |"-type interline
  after the real interline.  This allows us to put arbitrarily long
  annotations in the space to the right of the graph, for each revision --
  we can just stretch the graph portion to give us more space.

Loop:
  We start knowing, for each logical column, what thing has to go there
    (because this was determined last time)
  We use this to first determine what thing has to go in each column next
    time (though we will not draw them yet).
  This is somewhat tricky, because we do want to squish things towards the
  left when possible.  However, we have very limited drawing options -- we
  can slide several things 1 space to the left or right and do no other long
  sideways edges; or, we can draw 1 or 2 long sideways edges, but then
  everything else must go straight.  So, we try a few different layouts.
  The options are, remove a "ghost" if one exists, don't remove a ghost, and
  insert a ghost.  (A "ghost" is a blank space left by a line that has
  terminated or merged back into another line, but we haven't shifted things
  over sideways yet to fill in the space.)

  Having found a layout that works, we draw lines connecting things!  Yay.
*/

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
//*  for (vector<revision_id>::const_reverse_iterator rev = sorted.rbegin();
//*       rev != sorted.rend(); ++rev)
  reverse(sorted.begin(), sorted.end()); //TODO: faster to use a reverse_iterator I guess, but that seems to give some problems
  for (vector<revision_id>::const_iterator rev = sorted.begin();
       rev != sorted.end(); ++rev)
    {
      // print row
      std::cerr << "asciik: foreach sorted\n";

//p    if curr_rev not in curr_row:
//p        curr_row.append(curr_rev)
      if (find(curr_row.begin(), curr_row.end(), *rev) == curr_row.end())
	curr_row.push_back(*rev);

//p    curr_loc = curr_row.index(curr_rev)
      //iterator_traits<vector<revision_id>::iterator>::difference_type
      size_t curr_loc = distance(curr_row.begin(),
	find(curr_row.begin(), curr_row.end(), *rev));
      //assert(curr_loc < size()); as it is surely found

      std::cerr << "asciik: parents\n";
      set<revision_id> parents;
      app.db.get_revision_parents(*rev, parents);
//p    new_revs = []
//p    for p in parents:
//p        if p not in curr_row:
//p            new_revs.append(p)
      std::cerr << "asciik: foreach parent\n";
      set<revision_id> new_revs;
      for (set<revision_id>::const_iterator parent = parents.begin();
	   parent != parents.end(); ++parent)
	if (find(curr_row.begin(), curr_row.end(), *parent) == curr_row.end())
	  new_revs.insert(*parent);

//p    next_row = list(curr_row)
//p    next_row[curr_loc:curr_loc + 1] = new_revs
      std::cerr << "asciik: next row\n";
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

//p def links_cross(links):
//p     crosses = set()
//p     for i, j in links:
//p         if i != j:
//p             for coord in xrange(2 * min(i, j) + 1, 2 * max(i, j)):
//p                 crosses.add(coord)
//p     return crosses
void links_cross(const set<pair<size_t, size_t> > & links, set<size_t> & crosses)
{
  for (set<pair<size_t, size_t> >::const_iterator link = links.begin();
       link != links.end(); ++link)
    {
      size_t i = link->first, j = link->second;
      if (i != j)
	for (size_t coord = 2 * min(i, j) + 1, end = 2 * max(i, j);
	     coord < end; ++coord)
	  crosses.insert(coord);
    }
}

void draw(const size_t curr_items, const size_t next_items,
  const size_t curr_loc, const set<pair<size_t, size_t> > & links,
  const set<size_t> & curr_ghosts, const string & annotation)
{
//p    line = [" "] * (curr_items * 2 - 1)
//p    interline = [" "] * (max(curr_items, next_items) * 2 - 1)
  string line(curr_items * 2 - 1, ' ');
  string interline(max(curr_items, next_items) * 2 - 1, ' ');

//p    # first draw the flow-through bars in the line
//p    for i in xrange(curr_items):
//p        line[i * 2] = "|"
  for (size_t i = 0; i < curr_items; ++i)
    line[i * 2] = '|';

//p    # but then erase it for ghosts
//p    for i in curr_ghosts:
//p        line[i * 2] = " "
  for (set<size_t>::const_iterator i = curr_ghosts.begin();
       i != curr_ghosts.end(); ++i)
    line[(*i) * 2] = ' ';

//p    # then the links
//p    dots = set()
  set<size_t> dots;
//p    for i, j in links:
  for (set<pair<size_t, size_t> >::const_iterator link = links.begin();
       link != links.end(); ++link)
    {
      size_t i = link->first, j = link->second, start, end, dot;
//p        if i == j:
//p            interline[2 * i] = "|"
      if (i == j)
	interline[2 * i] = '|';
//p        else:
      else if (j < i) {
//p            if j < i:
//p                # | .---o
//p                # |/| | |
//p                # 0 1 2 3
//p                # j     i
//p                # 0123456
//p                #    s  e
//p                start = 2*j + 3
//p                end = 2*i
//p                dot = start - 1
//p                interline[dot - 1] = "/"
	start = 2 * j + 3;
	end = 2 * i;
	dot = start - 1;
	interline[dot - 1] = '/';
      } else {
//p            else: # i < j
//p                # o---.
//p                # | | |\|
//p                # 0 1 2 3
//p                # i     j
//p                # 0123456
//p                #  s  e
//p                start = 2*i + 1
//p                end = 2*j - 2
//p                dot = end
//p                interline[dot + 1] = "\\"
	start = 2 * i + 1;
	end = 2 * j - 2;
	dot = end;
	interline[dot + 1] = '\\';
      }
//p            if end - start >= 1:
//p                dots.add(dot)
//p            line[start:end] = "-" * (end - start)
      if ((end - start) > 0)
	dots.insert(dot);
      for (size_t l = start; l < end; ++l)
	line[l] = '-';
    }
//p    # add any dots (must do this in a second pass, so that if there are
//p    # cases like:
//p    #   | .-----.-o
//p    #   |/| | |/|
//p    # where we want to make sure the second dot overwrites the first --.
//p    for dot in dots:
//p        line[dot] = "."
  for (set<size_t>::const_iterator dot = dots.begin();
       dot != dots.end(); ++dot)
    line[*dot] = '·';
//p    # and add the main attraction (may overwrite a ".").
//p    line[curr_loc * 2] = "o"
  line[curr_loc * 2] = 'o';

//p    print "".join(line) + "    " + annotation
//p    print "".join(interline)
  cout << line << "    " << annotation << '\n';
  cout << interline << '\n';
}

bool try_draw(const vector<revision_id> & curr_row,
  const vector<revision_id> & next_row, const size_t curr_loc,
  const set<revision_id> & parents)
{
//p    curr_items = len(curr_row)
//p    next_items = len(next_row)
  size_t curr_items = curr_row.size();
  size_t next_items = next_row.size();

//p    curr_ghosts = []
//p    for i in xrange(curr_items):
//p        if curr_row[i] is None:
//p            curr_ghosts.append(i)
  set<size_t> curr_ghosts;
  for (size_t i = 0; i < curr_items; ++i)
    if (idx(curr_row, i) == ghost)
      curr_ghosts.insert(i);

//p    preservation_links = []
//p    have_shift = False
//p    for rev in curr_row:
//p        if rev is not None and rev in next_row:
//p            i = curr_row.index(rev)
//p            j = next_row.index(rev)
//p            if i != j:
//p                have_shift = True
//p            if abs(i - j) > 1:
//p                return False
//p            preservation_links.append((i, j))
  set<pair<size_t, size_t> > preservation_links;
  bool have_shift = false;
  for (size_t i = 0; i < curr_items; ++i) {
    if (idx(curr_row, i) != ghost) {
      vector<revision_id>::const_iterator found =
	find(next_row.begin(), next_row.end(), idx(curr_row, i));
      if (found != next_row.end()) {
	size_t j = distance(next_row.begin(), found);
	size_t d = abs(i - j);
	if (d > 1)
	  return false;
	if (d != 0)
	  have_shift = true;
	preservation_links.insert(pair<size_t, size_t>(i, j));
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
  set<pair<size_t, size_t> > parent_links;
  for (set<revision_id>::const_iterator p = parents.begin();
       p != parents.end(); )
    {
      size_t i = curr_loc;
      size_t j = distance(next_row.begin(),
	find(next_row.begin(), next_row.end(), *p));
      size_t d = abs(i - j);
      if ((d > 1) && have_shift)
	return false;
      parent_links.insert(pair<size_t, size_t>(i, j));
    }

//p    preservation_crosses = links_cross(preservation_links)
//p    parent_crosses = links_cross(parent_links)
//p    if preservation_crosses.intersection(parent_crosses):
//p        return False
  set<size_t> preservation_crosses, parent_crosses, intersection_crosses;
  links_cross(preservation_links, preservation_crosses);
  links_cross(parent_links, parent_crosses);
  set_intersection(
    preservation_crosses.begin(), preservation_crosses.end(),
    parent_crosses.begin(), parent_crosses.end(),
    insert_iterator<set<size_t> >(intersection_crosses, intersection_crosses.begin()));
  if (intersection_crosses.size() > 0)
    return false;

//p    links = preservation_links + parent_links
//p    draw(curr_items, next_items, curr_loc, links, curr_ghosts, curr_row[curr_loc])
  set<pair<size_t, size_t> > links(preservation_links);
  copy(parent_links.begin(), parent_links.end(),
    insert_iterator<set<pair<size_t, size_t> > >(links, links.begin()));
  draw(curr_items, next_items, curr_loc, links, curr_ghosts,
    /*annotation*/ idx(curr_row, curr_loc).inner()());

//p    return True
  return true;
}
