// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>
#include <vector>

#include "manifest.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "transforms.hh"

restriction::restriction(vector<utf8> const & args,
                         roster_t const & roster)
{
  set_paths(args);
  add_nodes(roster);
  check_paths();
}

restriction::restriction(vector<utf8> const & args,
                         roster_t const & roster1,
                         roster_t const & roster2)
{
  set_paths(args);
  add_nodes(roster1);
  add_nodes(roster2);
  check_paths();
}

void
restriction::set_paths(vector<utf8> const & args)
{
  L(F("setting paths with %d args") % args.size());

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      split_path sp;
      file_path_external(*i).split(sp);
      paths.insert(sp);
      L(F("added path '%s'") % *i);
    }
}

void
restriction::add_nodes(roster_t const & roster)
{
  L(F("adding nodes\n"));

  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      // TODO: (future) handle some sort of peg revision path syntax here.
      // note that the idea of a --peg option doesn't work because each
      // path may be relative to a different revision.

      if (roster.has_node(*i)) 
        {
          node_t node = roster.get_node(*i);
          bool recursive = is_dir_t(node);
          node_id nid = node->self;

          valid_paths.insert(*i);

          // TODO: proper wildcard paths like foo/... 
          // for now we always add directories recursively and files exactly

          // TODO: possibly fail with nice error if path is already explicitly
          // in the map?

          L(F("adding nid %d '%s'\n") % nid % file_path(*i));
          insert(nid, recursive);

          // currently we need to insert the parents of included nodes so that
          // the included nodes are not orphaned in a restricted roster.  this
          // happens in cases like add "a" + add "a/b" when only "a/b" is
          // included. i.e. "a" must be included for "a/b" to be valid. this
          // isn't entirely sensible and should probably be revisited.

          node_id parent = node->parent;
          while (!null_node(parent))
            {
              split_path sp;
              roster.get_name(parent, sp);
              L(F("adding parent %d '%s'\n") % parent % file_path(sp));
              insert(parent, false);
              node = roster.get_node(parent);
              parent = node->parent;
            }

          // TODO: consider keeping a list of valid paths here that we can use
          // for doing a set-difference against with the full list of paths to
          // find those that are not legal in any roster of this restriction
        }
      else
        {
          L(F("missed path '%s'") % *i);
        }
    }

}

bool
restriction::includes(roster_t const & roster, node_id nid) const
{
  // empty restriction includes everything
  if (restricted_node_map.empty()) 
    return true;

  node_id current = nid;

  MM(roster);

  I(roster.has_node(nid));

  while (!null_node(current)) 
    {
      split_path sp;
      roster.get_name(current, sp);
      L(F("checking nid %d '%s'\n") % current % file_path(sp));

      restriction_map::const_iterator r = restricted_node_map.find(current);

      if (r != restricted_node_map.end()) 
        {
          // found exact node or a recursive parent
          if (r->second || current == nid) 
            {
              L(F("included nid %d '%s'\n") % current % file_path(sp));
              return true;
            }
        }

      node_t node = roster.get_node(current);
      current = node->parent;
    }

  split_path sp;
  roster.get_name(nid, sp);
  L(F("excluded nid %d '%s'\n") % nid % file_path(sp));
  
  return false;
}

void
restriction::insert(node_id nid, bool recursive)
{
  // we (mistakenly) allow multiple settings of the recursive include flag on a
  // nid. this needs to be fixed but we need to track more than a simple boolean
  // to do it. nids can be added non-recursively as parents of included nids, or
  // explicitly. we should probably prevent explicit inclusion of foo/bar and
  // foo/bar/... though.

  restricted_node_map[nid] |= recursive;
}

void
restriction::check_paths()
{
  int bad = 0;
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (valid_paths.find(*i) == valid_paths.end())
        {
          bad++;
          W(F("unknown path %s") % *i);
        }
    }

  E(bad == 0, F("%d unknown paths") % bad);
}
