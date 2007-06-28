// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
// Copyright (C) 2007 Lapo Luchini <lapo@lapo.it>
// Copyright (C) 2007 Gabriele Dini Ciacci <dark.schneider@iol.it>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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

#include "base.hh"
#include <algorithm>
#include <iostream>
#include <iterator>

#include "asciik.hh"
#include "simplestring_xform.hh"
#include "cmd.hh"
#include "app_state.hh"

using std::insert_iterator;
using std::max;
using std::min;
using std::ostream;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::find;
using std::reverse;
using std::distance;

static revision_id ghost; // valid but empty revision_id to be used as ghost value

asciik::asciik(ostream & os, size_t min_width)
  : width(min_width), output(os)
{
}

void
asciik::links_cross(set<pair<size_t, size_t> > const & links,
                    set<size_t> & crosses) const
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

void
asciik::draw(size_t const curr_items,
             size_t const next_items,
             size_t const curr_loc,
             set<pair<size_t, size_t> > const & links,
             set<size_t> const & curr_ghosts,
             string const & annotation) const
{
  size_t line_len = max(width, max(curr_items, next_items) * 2);
  string line(line_len, ' ');      // actual len: curr_items * 2 - 1
  string interline(line_len, ' '); // actual len: max(curr_items, next_items) * 2 - 1
  string interline2(line_len, ' ');

  // first draw the flow-through bars in the line
  for (size_t i = 0; i < curr_items; ++i)
    line[i * 2] = '|';

  // but then erase it for ghosts
  for (set<size_t>::const_iterator i = curr_ghosts.begin();
       i != curr_ghosts.end(); ++i)
    line[(*i) * 2] = ' ';

  // then the links
  set<size_t> dots;
  for (set<pair<size_t, size_t> >::const_iterator link = links.begin();
       link != links.end(); ++link)
    {
      size_t i = link->first;
      size_t j = link->second;
      if (i == j)
        interline[2 * i] = '|';
      else
        {
          size_t start, end, dot;
          if (j < i)
            {
              // | .---o
              // |/| | |
              // 0 1 2 3
              // j     i
              // 0123456
              //    s  e
              start = 2 * j + 3;
              end = 2 * i;
              dot = start - 1;
              interline[dot - 1] = '/';
            }
          else // j > i
            {
              // o---.
              // | | |\|
              // 0 1 2 3
              // i     j
              // 0123456
              //  s  e
              start = 2 * i + 1;
              end = 2 * j - 2;
              dot = end;
              interline[dot + 1] = '\\';
            }
          if (end > start)
            {
              dots.insert(dot);
              for (size_t l = start; l < end; ++l)
                line[l] = '-';
            }
        }
      // prepare the proper continuation line
      interline2[j * 2] = '|';
    }
  // add any dots (must do this in a second pass, so that things still work if
  // there are cases like:
  //   | .-----.-o
  //   |/| | |/|
  // where we want to make sure that the second dot overwrites the first -.
  for (set<size_t>::const_iterator dot = dots.begin();
       dot != dots.end(); ++dot)
    line[*dot] = '.';
  // and add the main attraction (may overwrite a '.').
  line[curr_loc * 2] = 'o';

  // split a multi-line annotation
  vector<string> lines;
  split_into_lines(annotation, lines);
  int num_lines = lines.size();
  if (num_lines < 1)
    lines.push_back(string(""));
  if (num_lines < 2)
    lines.push_back(string(""));
  // ignore empty lines at the end
  while ((num_lines > 2) && (lines[num_lines - 1].size() == 0))
    --num_lines;

  // prints it out
  //TODO convert line/interline/interline2 from ASCII to system charset
  output << line << "  " << lines[0] << '\n';
  output << interline << "  " << lines[1] << '\n';
  for (int i = 2; i < num_lines; ++i)
    output << interline2 << "  " << lines[i] << '\n';
}

bool
asciik::try_draw(vector<revision_id> const & next_row,
                 size_t const curr_loc,
                 set<revision_id> const & parents,
                 string const & annotation) const
{
  size_t curr_items = curr_row.size();
  size_t next_items = next_row.size();
  I(curr_loc < curr_items);

  set<size_t> curr_ghosts;
  for (size_t i = 0; i < curr_items; ++i)
    if (idx(curr_row, i) == ghost)
      curr_ghosts.insert(i);

  set<pair<size_t, size_t> > preservation_links;
  bool have_shift = false;
  for (size_t i = 0; i < curr_items; ++i) 
    {
      if (idx(curr_row, i) != ghost) 
        {
          vector<revision_id>::const_iterator found =
            find(next_row.begin(), next_row.end(), idx(curr_row, i));
          if (found != next_row.end()) 
            {
              size_t j = distance(next_row.begin(), found);
              size_t d = i>j ? i-j : j-i;
              if (d > 1)
                return false;
              if (d != 0)
                have_shift = true;
              preservation_links.insert(pair<size_t, size_t>(i, j));
            }
        }
    }

  set<pair<size_t, size_t> > parent_links;
  for (set<revision_id>::const_iterator p = parents.begin();
       p != parents.end(); ++p)
    if (*p != ghost)
      {
        size_t i = curr_loc;
        size_t j = distance(next_row.begin(),
          find(next_row.begin(), next_row.end(), *p));
        I(j < next_items);
        size_t d = i>j ? i-j : j-i;
        if ((d > 1) && have_shift)
          return false;
        parent_links.insert(pair<size_t, size_t>(i, j));
      }

  set<size_t> preservation_crosses, parent_crosses, intersection_crosses;
  links_cross(preservation_links, preservation_crosses);
  links_cross(parent_links, parent_crosses);
  set_intersection(
    preservation_crosses.begin(), preservation_crosses.end(),
    parent_crosses.begin(), parent_crosses.end(),
    insert_iterator<set<size_t> >(intersection_crosses, intersection_crosses.begin()));
  if (intersection_crosses.size() > 0)
    return false;

  set<pair<size_t, size_t> > links(preservation_links);
  copy(parent_links.begin(), parent_links.end(),
    insert_iterator<set<pair<size_t, size_t> > >(links, links.begin()));
  draw(curr_items, next_items, curr_loc, links, curr_ghosts, annotation);
  return true;
}

void
asciik::print(revision_id const & rev,
              set<revision_id> const & parents,
              string const & annotation)
{
  if (find(curr_row.begin(), curr_row.end(), rev) == curr_row.end())
    curr_row.push_back(rev);
  size_t curr_loc = distance(curr_row.begin(),
    find(curr_row.begin(), curr_row.end(), rev));
  // it must be found as either it was there already or we just added it
  I(curr_loc < curr_row.size());

  set<revision_id> new_revs;
  for (set<revision_id>::const_iterator parent = parents.begin();
       parent != parents.end(); ++parent)
    if (find(curr_row.begin(), curr_row.end(), *parent) == curr_row.end())
      new_revs.insert(*parent);

  vector<revision_id> next_row(curr_row);
  I(curr_loc < next_row.size());
  next_row.insert(
    next_row.erase(next_row.begin() + curr_loc),
    new_revs.begin(), new_revs.end());

  // now next_row contains exactly the revisions it needs to, except that no
  // ghost handling has been done.
  vector<revision_id> no_ghost(next_row);
  vector<revision_id>::iterator first_ghost = find(no_ghost.begin(),
    no_ghost.end(), ghost);
  if (first_ghost != no_ghost.end())
    no_ghost.erase(first_ghost);

  if (try_draw(no_ghost, curr_loc, parents, annotation))
    curr_row = no_ghost;
  else if (try_draw(next_row, curr_loc, parents, annotation))
    curr_row = next_row;
  else if (new_revs.size() == 0) // this line has disappeared
    {
      vector<revision_id> extra_ghost(next_row);
      I(curr_loc < extra_ghost.size());
      extra_ghost.insert(extra_ghost.begin() + curr_loc, ghost);
      I(try_draw(extra_ghost, curr_loc, parents, annotation));
      curr_row = extra_ghost;
    }
}

CMD(asciik, "asciik", "", CMD_REF(debug), N_("SELECTOR"),
    N_("Prints an ASCII representation of the revisions' graph"),
    "",
    options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(args[0](), app.db));

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  //set<hexenc<id>> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app.db);

  asciik graph(std::cout, 10);
  set<revision_id> revs;
  for (set<string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    {
      revision_id rid(*i);
      revs.insert(rid);
    }
  vector<revision_id> sorted;
  toposort(revs, sorted, app.db);
  vector<revision_id> curr_row;
  reverse(sorted.begin(), sorted.end());
  for (vector<revision_id>::const_iterator rev = sorted.begin();
       rev != sorted.end(); ++rev)
    {
      set<revision_id> parents;
      app.db.get_revision_parents(*rev, parents);
      parents.erase(ghost); // remove the fake parent that root nodes have
      graph.print(*rev, parents, rev->inner()());
    }
}
