// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>
#include <vector>

#include "restrictions.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "transforms.hh"

using std::make_pair;
using std::set;

// TODO: add support for --depth (replace recursive boolean with depth value)
// depth really only makes sense when one directory is specified however. it would
// be much better to support foo/... style recursive paths

// TODO: add check for relevant rosters to be used by log
//
// i.e.  as log goes back through older and older rosters it may hit one that
// pre-dates any of the nodes in the restriction. the nodes that the restriction
// includes or excludes may not have been born in a sufficiently old roster. at
// this point log should stop because no earlier roster will include these nodes.

static void
make_path_set(vector<utf8> const & args, path_set & paths)
{
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      split_path sp;
      file_path_external(*i).split(sp);
      paths.insert(sp);
    }
}

// FIXME: should we really be doing implicit includes of parents?
static void
get_parent_paths(path_set const & paths, path_set & parents) 
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      split_path sp(*i);
      sp.pop_back();
      while (!sp.empty() && parents.find(sp) == parents.end())
        {
          parents.insert(sp);
          sp.pop_back();
        }
    }
}

static void
get_nodes(path_set const & paths, roster_t const & roster, 
          set<node_id> & nodes, 
          path_set & known_paths)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (roster.has_node(*i)) 
        {
          known_paths.insert(*i);
          node_id nid = roster.get_node(*i)->self;
          nodes.insert(nid);
        }
    }  
}

// FIXME: should we really be doing implicit includes of parents?
static void
get_parent_nodes(set<node_id> const & nodes, 
                 roster_t const & roster, 
                 set<node_id> & parents)
{
  for (set<node_id>::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      I(roster.has_node(*i));
      node_id nid = roster.get_node(*i)->parent;
      while (!null_node(nid))
        {
          parents.insert(nid);
          nid = roster.get_node(nid)->parent;
        }
    }
}

static void 
merge_states(path_state const & old_state, 
             path_state const & new_state, 
             path_state & merged_state, 
             split_path const & sp)
{
  if (old_state == new_state)
    {
      merged_state = old_state;
    }
  else if (old_state == explicit_include && new_state == implicit_include ||
           old_state == implicit_include && new_state == explicit_include)
    {
      merged_state = explicit_include;
    }
  else if (old_state == explicit_exclude && new_state == implicit_include ||
           old_state == implicit_include && new_state == explicit_exclude)
    {
      // FIXME: should we really be doing implicit includes of parents?

      // allowing a mix of explicit excludes and implicit includes on a path is
      // rather questionable but is required by things like exclude x include
      // x/y. (i.e. includes within excludes) these are also somewhat
      // questionable themselves since they are equivalent to simply include
      // x/y. 
      //
      // this is also required more sensible things like include x exclude x/y
      // include x/y/z.
      merged_state = implicit_include;
    }
  else
    {
      L(FL("path '%s' %d %d") % sp % old_state % new_state);
      N(false, F("conflicting include/exclude on path '%s'") % sp);
    }
}

static void
add_paths(map<split_path, path_state> & path_map, 
          path_set const & paths, 
          path_state const state)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      map<split_path, path_state>::iterator p = path_map.find(*i);
      if (p != path_map.end())
        {
          path_state merged;
          merge_states(p->second, state, merged, *i);
          p->second = merged;
        }
      else
        {
          path_map.insert(make_pair(*i, state));
        }
    }
}

static void
add_nodes(map<node_id, path_state> & node_map, 
          roster_t const & roster,
          set<node_id> const & nodes,
          path_state const state)
{
  for (set<node_id>::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      I(roster.has_node(*i));

      map<node_id, path_state>::iterator n = node_map.find(*i);
      if (n != node_map.end())
        {
          path_state merged;
          split_path sp;
          roster.get_name(*i, sp);
          merge_states(n->second, state, merged, sp);
          n->second = merged;
        }
      else
        {
          node_map.insert(make_pair(*i, state));
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// construction helpers
////////////////////////////////////////////////////////////////////////////////

void
restriction::map_paths(vector<utf8> const & include_args,
                       vector<utf8> const & exclude_args)
{
  make_path_set(include_args, included_paths);
  make_path_set(exclude_args, excluded_paths);

  path_set parent_paths;
  get_parent_paths(included_paths, parent_paths);

  add_paths(path_map, included_paths, explicit_include);
  add_paths(path_map, excluded_paths, explicit_exclude);
  add_paths(path_map, parent_paths,  implicit_include);

  L(FL("restriction paths: %d included; %d excluded; %d parents") 
    % included_paths.size()
    % excluded_paths.size()
    % parent_paths.size());
}

void
restriction::map_nodes(roster_t const & roster)
{
  set<node_id> included_nodes, excluded_nodes, parent_nodes;
  get_nodes(included_paths, roster, included_nodes, known_paths);
  get_nodes(excluded_paths, roster, excluded_nodes, known_paths);
  get_parent_nodes(included_nodes, roster, parent_nodes);

  add_nodes(node_map, roster, included_nodes, explicit_include);
  add_nodes(node_map, roster, excluded_nodes, explicit_exclude);
  add_nodes(node_map, roster, parent_nodes,   implicit_include);

  L(FL("restriction nodes: %d included; %d excluded; %d parents") 
    % included_nodes.size()
    % excluded_nodes.size()
    % parent_nodes.size());
}

void
restriction::validate()
{
  int bad = 0;

  for (path_set::const_iterator i = included_paths.begin(); 
       i != included_paths.end(); ++i)
    {
      // ignored paths are allowed into the restriction but are not considered
      // invalid if they are found in none of the restriction's rosters
      if (known_paths.find(*i) == known_paths.end())
        {
          file_path fp(*i);
          if (!app.lua.hook_ignore_file(fp))
            {
              bad++;
              W(F("unknown path included %s") % *i);
            }
        }
    }

  for (path_set::const_iterator i = excluded_paths.begin(); 
       i != excluded_paths.end(); ++i)
    {
      if (known_paths.find(*i) == known_paths.end())
        {
          bad++;
          W(F("unknown path excluded %s") % *i);
        }
    }
  
  N(bad == 0, F("%d unknown paths") % bad);
}

////////////////////////////////////////////////////////////////////////////////
// public api
////////////////////////////////////////////////////////////////////////////////

bool
restriction::includes(roster_t const & roster, node_id nid) const
{
  MM(roster);
  I(roster.has_node(nid));

  split_path sp;
  roster.get_name(nid, sp);
  
  // empty restriction includes everything
  if (empty()) 
    {
      L(FL("empty include of nid %d path '%s'") % nid % file_path(sp));
      return true;
    }

  node_id current = nid;
  int depth = 0;

  // use app.depth+1 here because the old semantics of depth=0 were something
  // like "the current directory and its immediate children". it seems somewhat
  // more reasonable here to use depth=0 to mean "exactly this directory" and 
  // depth=1 to mean "this directory and its immediate children"
  while (!null_node(current) && (app.depth == -1 || depth <= app.depth + 1)) 
    {
      map<node_id, path_state>::const_iterator r = node_map.find(current);

      if (r != node_map.end()) 
        {
          switch (r->second) 
            {
            case explicit_include:
              L(FL("explicit include of nid %d path '%s'") % current % file_path(sp));
              return true;

            case explicit_exclude:
              L(FL("explicit exclude of nid %d path '%s'") % current % file_path(sp));
              return false;

            case implicit_include:
              // this is non-recursive and requires an exact match
              if (depth == 0)
                {
                  L(FL("implicit include of nid %d path '%s'") % current % file_path(sp));
                  return true;
                }
            }
        }

      node_t node = roster.get_node(current);
      current = node->parent;
      depth++;
    }

  if (included_paths.empty())
    {
      L(FL("default include of nid %d path '%s'\n") % nid % file_path(sp));
      return true;
    }
  else
    {
      L(FL("default exclude of nid %d path '%s'\n") % nid % file_path(sp));
      return false;
    }
}

bool
restriction::includes(split_path const & sp) const
{
  // empty restriction includes everything
  if (empty()) 
    {
      L(FL("empty include of path '%s'") % file_path(sp));
      return true;
    }

  split_path current(sp);
  int depth = 0;

  // use app.depth+1 here because the old semantics of depth=0 were something
  // like "the current directory and its immediate children". it seems somewhat
  // more reasonable here to use depth=0 to mean "exactly this directory" and 
  // depth=1 to mean "this directory and its immediate children"
  while (!current.empty() && (app.depth == -1 || depth <= app.depth + 1))
    {
      map<split_path, path_state>::const_iterator r = path_map.find(current);

      if (r != path_map.end())
        {
          switch (r->second) 
            {
            case explicit_include:
              L(FL("explicit include of path '%s'") % file_path(sp));
              return true;

            case explicit_exclude:
              L(FL("explicit exclude of path '%s'") % file_path(sp));
              return false;

            case implicit_include:
              // this is non-recursive and requires an exact match
              if (depth == 0)
                {
                  L(FL("implicit include of path '%s'") % file_path(sp));
                  return true;
                }
            }
        }

      current.pop_back();
      depth++;
    }

  if (included_paths.empty())
    {
      L(FL("default include of path '%s'\n") % file_path(sp));
      return true;
    }
  else
    {
      L(FL("default exclude of path '%s'\n") % file_path(sp));
      return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
////////////////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "app_state.hh"
#include "unit_tests.hh"
#include "roster.hh"
#include "sanity.hh"

using std::string;

// f's and g's are files
// x's and y's are directories
// and this is rather painful

split_path sp_root;
split_path sp_f;
split_path sp_g;

split_path sp_x;
split_path sp_xf;
split_path sp_xg;
split_path sp_xx;
split_path sp_xxf;
split_path sp_xxg;
split_path sp_xy;
split_path sp_xyf;
split_path sp_xyg;

split_path sp_y;
split_path sp_yf;
split_path sp_yg;
split_path sp_yx;
split_path sp_yxf;
split_path sp_yxg;
split_path sp_yy;
split_path sp_yyf;
split_path sp_yyg;

node_id nid_root;
node_id nid_f;
node_id nid_g;

node_id nid_x;
node_id nid_xf;
node_id nid_xg;
node_id nid_xx;
node_id nid_xxf;
node_id nid_xxg;
node_id nid_xy;
node_id nid_xyf;
node_id nid_xyg;

node_id nid_y;
node_id nid_yf;
node_id nid_yg;
node_id nid_yx;
node_id nid_yxf;
node_id nid_yxg;
node_id nid_yy;
node_id nid_yyf;
node_id nid_yyg;

file_id fid_f(string("1000000000000000000000000000000000000000"));
file_id fid_g(string("2000000000000000000000000000000000000000"));

file_id fid_xf(string("3000000000000000000000000000000000000000"));
file_id fid_xg(string("4000000000000000000000000000000000000000"));
file_id fid_xxf(string("5000000000000000000000000000000000000000"));
file_id fid_xxg(string("6000000000000000000000000000000000000000"));
file_id fid_xyf(string("7000000000000000000000000000000000000000"));
file_id fid_xyg(string("8000000000000000000000000000000000000000"));

file_id fid_yf(string("9000000000000000000000000000000000000000"));
file_id fid_yg(string("a000000000000000000000000000000000000000"));
file_id fid_yxf(string("b000000000000000000000000000000000000000"));
file_id fid_yxg(string("c000000000000000000000000000000000000000"));
file_id fid_yyf(string("d000000000000000000000000000000000000000"));
file_id fid_yyg(string("e000000000000000000000000000000000000000"));

static void setup(roster_t & roster) 
{
  temp_node_id_source nis;

  file_path_internal("").split(sp_root);
  file_path_internal("f").split(sp_f);
  file_path_internal("g").split(sp_g);

  file_path_internal("x").split(sp_x);
  file_path_internal("x/f").split(sp_xf);
  file_path_internal("x/g").split(sp_xg);
  file_path_internal("x/x").split(sp_xx);
  file_path_internal("x/x/f").split(sp_xxf);
  file_path_internal("x/x/g").split(sp_xxg);
  file_path_internal("x/y").split(sp_xy);
  file_path_internal("x/y/f").split(sp_xyf);
  file_path_internal("x/y/g").split(sp_xyg);

  file_path_internal("y").split(sp_y);
  file_path_internal("y/f").split(sp_yf);
  file_path_internal("y/g").split(sp_yg);
  file_path_internal("y/x").split(sp_yx);
  file_path_internal("y/x/f").split(sp_yxf);
  file_path_internal("y/x/g").split(sp_yxg);
  file_path_internal("y/y").split(sp_yy);
  file_path_internal("y/y/f").split(sp_yyf);
  file_path_internal("y/y/g").split(sp_yyg);

  nid_root = roster.create_dir_node(nis);
  nid_f    = roster.create_file_node(fid_f, nis);
  nid_g    = roster.create_file_node(fid_g, nis);

  nid_x   = roster.create_dir_node(nis);
  nid_xf  = roster.create_file_node(fid_xf, nis);
  nid_xg  = roster.create_file_node(fid_xg, nis);
  nid_xx  = roster.create_dir_node(nis);
  nid_xxf = roster.create_file_node(fid_xxf, nis);
  nid_xxg = roster.create_file_node(fid_xxg, nis);
  nid_xy  = roster.create_dir_node(nis);
  nid_xyf = roster.create_file_node(fid_xxf, nis);
  nid_xyg = roster.create_file_node(fid_xxg, nis);

  nid_y   = roster.create_dir_node(nis);
  nid_yf  = roster.create_file_node(fid_yf, nis);
  nid_yg  = roster.create_file_node(fid_yg, nis);
  nid_yx  = roster.create_dir_node(nis);
  nid_yxf = roster.create_file_node(fid_yxf, nis);
  nid_yxg = roster.create_file_node(fid_yxg, nis);
  nid_yy  = roster.create_dir_node(nis);
  nid_yyf = roster.create_file_node(fid_yxf, nis);
  nid_yyg = roster.create_file_node(fid_yxg, nis);

  roster.attach_node(nid_root, sp_root);
  roster.attach_node(nid_f, sp_f);
  roster.attach_node(nid_g, sp_g);

  roster.attach_node(nid_x,   sp_x);
  roster.attach_node(nid_xf,  sp_xf);
  roster.attach_node(nid_xg,  sp_xg);
  roster.attach_node(nid_xx,  sp_xx);
  roster.attach_node(nid_xxf, sp_xxf);
  roster.attach_node(nid_xxg, sp_xxg);
  roster.attach_node(nid_xy,  sp_xy);
  roster.attach_node(nid_xyf, sp_xyf);
  roster.attach_node(nid_xyg, sp_xyg);

  roster.attach_node(nid_y,   sp_y);
  roster.attach_node(nid_yf,  sp_yf);
  roster.attach_node(nid_yg,  sp_yg);
  roster.attach_node(nid_yx,  sp_yx);
  roster.attach_node(nid_yxf, sp_yxf);
  roster.attach_node(nid_yxg, sp_yxg);
  roster.attach_node(nid_yy,  sp_yy);
  roster.attach_node(nid_yyf, sp_yyf);
  roster.attach_node(nid_yyg, sp_yyg);
}

static void 
test_empty_restriction()
{
  roster_t roster;
  setup(roster);

  app_state app;
  restriction mask(app);
  
  BOOST_CHECK(mask.empty());

  // check restricted nodes
  BOOST_CHECK(mask.includes(roster, nid_root));
  BOOST_CHECK(mask.includes(roster, nid_f));
  BOOST_CHECK(mask.includes(roster, nid_g));

  BOOST_CHECK(mask.includes(roster, nid_x));
  BOOST_CHECK(mask.includes(roster, nid_xf));
  BOOST_CHECK(mask.includes(roster, nid_xg));
  BOOST_CHECK(mask.includes(roster, nid_xx));
  BOOST_CHECK(mask.includes(roster, nid_xxf));
  BOOST_CHECK(mask.includes(roster, nid_xxg));
  BOOST_CHECK(mask.includes(roster, nid_xy));
  BOOST_CHECK(mask.includes(roster, nid_xyf));
  BOOST_CHECK(mask.includes(roster, nid_xyg));

  BOOST_CHECK(mask.includes(roster, nid_y));
  BOOST_CHECK(mask.includes(roster, nid_yf));
  BOOST_CHECK(mask.includes(roster, nid_yg));
  BOOST_CHECK(mask.includes(roster, nid_yx));
  BOOST_CHECK(mask.includes(roster, nid_yxf));
  BOOST_CHECK(mask.includes(roster, nid_yxg));
  BOOST_CHECK(mask.includes(roster, nid_yy));
  BOOST_CHECK(mask.includes(roster, nid_yyf));
  BOOST_CHECK(mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK(mask.includes(sp_root));
  BOOST_CHECK(mask.includes(sp_f));
  BOOST_CHECK(mask.includes(sp_g));

  BOOST_CHECK(mask.includes(sp_x));
  BOOST_CHECK(mask.includes(sp_xf));
  BOOST_CHECK(mask.includes(sp_xg));
  BOOST_CHECK(mask.includes(sp_xx));
  BOOST_CHECK(mask.includes(sp_xxf));
  BOOST_CHECK(mask.includes(sp_xxg));
  BOOST_CHECK(mask.includes(sp_xy));
  BOOST_CHECK(mask.includes(sp_xyf));
  BOOST_CHECK(mask.includes(sp_xyg));

  BOOST_CHECK(mask.includes(sp_y));
  BOOST_CHECK(mask.includes(sp_yf));
  BOOST_CHECK(mask.includes(sp_yg));
  BOOST_CHECK(mask.includes(sp_yx));
  BOOST_CHECK(mask.includes(sp_yxf));
  BOOST_CHECK(mask.includes(sp_yxg));
  BOOST_CHECK(mask.includes(sp_yy));
  BOOST_CHECK(mask.includes(sp_yyf));
  BOOST_CHECK(mask.includes(sp_yyg));
}

static void 
test_simple_include()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  includes.push_back(utf8(string("x/x")));
  includes.push_back(utf8(string("y/y")));

  app_state app;
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK(!mask.includes(roster, nid_f));
  BOOST_CHECK(!mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK(!mask.includes(roster, nid_xf));
  BOOST_CHECK(!mask.includes(roster, nid_xg));
  BOOST_CHECK( mask.includes(roster, nid_xx));
  BOOST_CHECK( mask.includes(roster, nid_xxf));
  BOOST_CHECK( mask.includes(roster, nid_xxg));
  BOOST_CHECK(!mask.includes(roster, nid_xy));
  BOOST_CHECK(!mask.includes(roster, nid_xyf));
  BOOST_CHECK(!mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK(!mask.includes(roster, nid_yf));
  BOOST_CHECK(!mask.includes(roster, nid_yg));
  BOOST_CHECK(!mask.includes(roster, nid_yx));
  BOOST_CHECK(!mask.includes(roster, nid_yxf));
  BOOST_CHECK(!mask.includes(roster, nid_yxg));
  BOOST_CHECK( mask.includes(roster, nid_yy));
  BOOST_CHECK( mask.includes(roster, nid_yyf));
  BOOST_CHECK( mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK(!mask.includes(sp_f));
  BOOST_CHECK(!mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK(!mask.includes(sp_xf));
  BOOST_CHECK(!mask.includes(sp_xg));
  BOOST_CHECK( mask.includes(sp_xx));
  BOOST_CHECK( mask.includes(sp_xxf));
  BOOST_CHECK( mask.includes(sp_xxg));
  BOOST_CHECK(!mask.includes(sp_xy));
  BOOST_CHECK(!mask.includes(sp_xyf));
  BOOST_CHECK(!mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK(!mask.includes(sp_yf));
  BOOST_CHECK(!mask.includes(sp_yg));
  BOOST_CHECK(!mask.includes(sp_yx));
  BOOST_CHECK(!mask.includes(sp_yxf));
  BOOST_CHECK(!mask.includes(sp_yxg));
  BOOST_CHECK( mask.includes(sp_yy));
  BOOST_CHECK( mask.includes(sp_yyf));
  BOOST_CHECK( mask.includes(sp_yyg));
}

static void 
test_simple_exclude()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  excludes.push_back(utf8(string("x/x")));
  excludes.push_back(utf8(string("y/y")));

  app_state app;
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK( mask.includes(roster, nid_f));
  BOOST_CHECK( mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK( mask.includes(roster, nid_xf));
  BOOST_CHECK( mask.includes(roster, nid_xg));
  BOOST_CHECK(!mask.includes(roster, nid_xx));
  BOOST_CHECK(!mask.includes(roster, nid_xxf));
  BOOST_CHECK(!mask.includes(roster, nid_xxg));
  BOOST_CHECK( mask.includes(roster, nid_xy));
  BOOST_CHECK( mask.includes(roster, nid_xyf));
  BOOST_CHECK( mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK( mask.includes(roster, nid_yf));
  BOOST_CHECK( mask.includes(roster, nid_yg));
  BOOST_CHECK( mask.includes(roster, nid_yx));
  BOOST_CHECK( mask.includes(roster, nid_yxf));
  BOOST_CHECK( mask.includes(roster, nid_yxg));
  BOOST_CHECK(!mask.includes(roster, nid_yy));
  BOOST_CHECK(!mask.includes(roster, nid_yyf));
  BOOST_CHECK(!mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK( mask.includes(sp_f));
  BOOST_CHECK( mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK( mask.includes(sp_xf));
  BOOST_CHECK( mask.includes(sp_xg));
  BOOST_CHECK(!mask.includes(sp_xx));
  BOOST_CHECK(!mask.includes(sp_xxf));
  BOOST_CHECK(!mask.includes(sp_xxg));
  BOOST_CHECK( mask.includes(sp_xy));
  BOOST_CHECK( mask.includes(sp_xyf));
  BOOST_CHECK( mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK( mask.includes(sp_yf));
  BOOST_CHECK( mask.includes(sp_yg));
  BOOST_CHECK( mask.includes(sp_yx));
  BOOST_CHECK( mask.includes(sp_yxf));
  BOOST_CHECK( mask.includes(sp_yxg));
  BOOST_CHECK(!mask.includes(sp_yy));
  BOOST_CHECK(!mask.includes(sp_yyf));
  BOOST_CHECK(!mask.includes(sp_yyg));
}

static void 
test_include_exclude()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  includes.push_back(utf8(string("x")));
  includes.push_back(utf8(string("y")));
  excludes.push_back(utf8(string("x/x")));
  excludes.push_back(utf8(string("y/y")));

  app_state app;
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK(!mask.includes(roster, nid_f));
  BOOST_CHECK(!mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK( mask.includes(roster, nid_xf));
  BOOST_CHECK( mask.includes(roster, nid_xg));
  BOOST_CHECK(!mask.includes(roster, nid_xx));
  BOOST_CHECK(!mask.includes(roster, nid_xxf));
  BOOST_CHECK(!mask.includes(roster, nid_xxg));
  BOOST_CHECK( mask.includes(roster, nid_xy));
  BOOST_CHECK( mask.includes(roster, nid_xyf));
  BOOST_CHECK( mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK( mask.includes(roster, nid_yf));
  BOOST_CHECK( mask.includes(roster, nid_yg));
  BOOST_CHECK( mask.includes(roster, nid_yx));
  BOOST_CHECK( mask.includes(roster, nid_yxf));
  BOOST_CHECK( mask.includes(roster, nid_yxg));
  BOOST_CHECK(!mask.includes(roster, nid_yy));
  BOOST_CHECK(!mask.includes(roster, nid_yyf));
  BOOST_CHECK(!mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK(!mask.includes(sp_f));
  BOOST_CHECK(!mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK( mask.includes(sp_xf));
  BOOST_CHECK( mask.includes(sp_xg));
  BOOST_CHECK(!mask.includes(sp_xx));
  BOOST_CHECK(!mask.includes(sp_xxf));
  BOOST_CHECK(!mask.includes(sp_xxg));
  BOOST_CHECK( mask.includes(sp_xy));
  BOOST_CHECK( mask.includes(sp_xyf));
  BOOST_CHECK( mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK( mask.includes(sp_yf));
  BOOST_CHECK( mask.includes(sp_yg));
  BOOST_CHECK( mask.includes(sp_yx));
  BOOST_CHECK( mask.includes(sp_yxf));
  BOOST_CHECK( mask.includes(sp_yxg));
  BOOST_CHECK(!mask.includes(sp_yy));
  BOOST_CHECK(!mask.includes(sp_yyf));
  BOOST_CHECK(!mask.includes(sp_yyg));
}

static void 
test_exclude_include()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  excludes.push_back(utf8(string("x")));
  excludes.push_back(utf8(string("y")));
  includes.push_back(utf8(string("x/x")));
  includes.push_back(utf8(string("y/y")));

  app_state app;
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK(!mask.includes(roster, nid_f));
  BOOST_CHECK(!mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK(!mask.includes(roster, nid_xf));
  BOOST_CHECK(!mask.includes(roster, nid_xg));
  BOOST_CHECK( mask.includes(roster, nid_xx));
  BOOST_CHECK( mask.includes(roster, nid_xxf));
  BOOST_CHECK( mask.includes(roster, nid_xxg));
  BOOST_CHECK(!mask.includes(roster, nid_xy));
  BOOST_CHECK(!mask.includes(roster, nid_xyf));
  BOOST_CHECK(!mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK(!mask.includes(roster, nid_yf));
  BOOST_CHECK(!mask.includes(roster, nid_yg));
  BOOST_CHECK(!mask.includes(roster, nid_yx));
  BOOST_CHECK(!mask.includes(roster, nid_yxf));
  BOOST_CHECK(!mask.includes(roster, nid_yxg));
  BOOST_CHECK( mask.includes(roster, nid_yy));
  BOOST_CHECK( mask.includes(roster, nid_yyf));
  BOOST_CHECK( mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK(!mask.includes(sp_f));
  BOOST_CHECK(!mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK(!mask.includes(sp_xf));
  BOOST_CHECK(!mask.includes(sp_xg));
  BOOST_CHECK( mask.includes(sp_xx));
  BOOST_CHECK( mask.includes(sp_xxf));
  BOOST_CHECK( mask.includes(sp_xxg));
  BOOST_CHECK(!mask.includes(sp_xy));
  BOOST_CHECK(!mask.includes(sp_xyf));
  BOOST_CHECK(!mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK(!mask.includes(sp_yf));
  BOOST_CHECK(!mask.includes(sp_yg));
  BOOST_CHECK(!mask.includes(sp_yx));
  BOOST_CHECK(!mask.includes(sp_yxf));
  BOOST_CHECK(!mask.includes(sp_yxg));
  BOOST_CHECK( mask.includes(sp_yy));
  BOOST_CHECK( mask.includes(sp_yyf));
  BOOST_CHECK( mask.includes(sp_yyg));
}

static void 
test_invalid_paths()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  includes.push_back(utf8(string("foo")));
  excludes.push_back(utf8(string("bar")));

  app_state app;
  BOOST_CHECK_THROW(restriction(includes, excludes, roster, app), informative_failure);
}

static void
test_get_parent_paths()
{
  path_set paths, parents;
  split_path a, ab, abc;

  file_path_internal("a").split(a);
  file_path_internal("a/b").split(ab);
  file_path_internal("a/b/c").split(abc);

  paths.insert(abc);
  get_parent_paths(paths, parents);

  BOOST_CHECK(parents.find(a)  != parents.end());
  BOOST_CHECK(parents.find(ab) != parents.end());
  BOOST_CHECK(parents.find(abc) == parents.end());
}

static void
test_include_depth_0()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  includes.push_back(utf8(string("x")));
  includes.push_back(utf8(string("y")));

  app_state app;
  app.set_depth(0);
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK(!mask.includes(roster, nid_f));
  BOOST_CHECK(!mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK(!mask.includes(roster, nid_xf));
  BOOST_CHECK(!mask.includes(roster, nid_xg));
  BOOST_CHECK(!mask.includes(roster, nid_xx));
  BOOST_CHECK(!mask.includes(roster, nid_xxf));
  BOOST_CHECK(!mask.includes(roster, nid_xxg));
  BOOST_CHECK(!mask.includes(roster, nid_xy));
  BOOST_CHECK(!mask.includes(roster, nid_xyf));
  BOOST_CHECK(!mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK(!mask.includes(roster, nid_yf));
  BOOST_CHECK(!mask.includes(roster, nid_yg));
  BOOST_CHECK(!mask.includes(roster, nid_yx));
  BOOST_CHECK(!mask.includes(roster, nid_yxf));
  BOOST_CHECK(!mask.includes(roster, nid_yxg));
  BOOST_CHECK(!mask.includes(roster, nid_yy));
  BOOST_CHECK(!mask.includes(roster, nid_yyf));
  BOOST_CHECK(!mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK(!mask.includes(sp_f));
  BOOST_CHECK(!mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK(!mask.includes(sp_xf));
  BOOST_CHECK(!mask.includes(sp_xg));
  BOOST_CHECK(!mask.includes(sp_xx));
  BOOST_CHECK(!mask.includes(sp_xxf));
  BOOST_CHECK(!mask.includes(sp_xxg));
  BOOST_CHECK(!mask.includes(sp_xy));
  BOOST_CHECK(!mask.includes(sp_xyf));
  BOOST_CHECK(!mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK(!mask.includes(sp_yf));
  BOOST_CHECK(!mask.includes(sp_yg));
  BOOST_CHECK(!mask.includes(sp_yx));
  BOOST_CHECK(!mask.includes(sp_yxf));
  BOOST_CHECK(!mask.includes(sp_yxg));
  BOOST_CHECK(!mask.includes(sp_yy));
  BOOST_CHECK(!mask.includes(sp_yyf));
  BOOST_CHECK(!mask.includes(sp_yyg));
}

static void
test_include_depth_1()
{
  roster_t roster;
  setup(roster);

  vector<utf8> includes, excludes;
  includes.push_back(utf8(string("x")));
  includes.push_back(utf8(string("y")));

  app_state app;
  app.set_depth(1);
  restriction mask(includes, excludes, roster, app);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK( mask.includes(roster, nid_root));
  BOOST_CHECK(!mask.includes(roster, nid_f));
  BOOST_CHECK(!mask.includes(roster, nid_g));

  BOOST_CHECK( mask.includes(roster, nid_x));
  BOOST_CHECK( mask.includes(roster, nid_xf));
  BOOST_CHECK( mask.includes(roster, nid_xg));
  BOOST_CHECK( mask.includes(roster, nid_xx));
  BOOST_CHECK(!mask.includes(roster, nid_xxf));
  BOOST_CHECK(!mask.includes(roster, nid_xxg));
  BOOST_CHECK( mask.includes(roster, nid_xy));
  BOOST_CHECK(!mask.includes(roster, nid_xyf));
  BOOST_CHECK(!mask.includes(roster, nid_xyg));

  BOOST_CHECK( mask.includes(roster, nid_y));
  BOOST_CHECK( mask.includes(roster, nid_yf));
  BOOST_CHECK( mask.includes(roster, nid_yg));
  BOOST_CHECK( mask.includes(roster, nid_yx));
  BOOST_CHECK(!mask.includes(roster, nid_yxf));
  BOOST_CHECK(!mask.includes(roster, nid_yxg));
  BOOST_CHECK( mask.includes(roster, nid_yy));
  BOOST_CHECK(!mask.includes(roster, nid_yyf));
  BOOST_CHECK(!mask.includes(roster, nid_yyg));

  // check restricted paths
  BOOST_CHECK( mask.includes(sp_root));
  BOOST_CHECK(!mask.includes(sp_f));
  BOOST_CHECK(!mask.includes(sp_g));

  BOOST_CHECK( mask.includes(sp_x));
  BOOST_CHECK( mask.includes(sp_xf));
  BOOST_CHECK( mask.includes(sp_xg));
  BOOST_CHECK( mask.includes(sp_xx));
  BOOST_CHECK(!mask.includes(sp_xxf));
  BOOST_CHECK(!mask.includes(sp_xxg));
  BOOST_CHECK( mask.includes(sp_xy));
  BOOST_CHECK(!mask.includes(sp_xyf));
  BOOST_CHECK(!mask.includes(sp_xyg));

  BOOST_CHECK( mask.includes(sp_y));
  BOOST_CHECK( mask.includes(sp_yf));
  BOOST_CHECK( mask.includes(sp_yg));
  BOOST_CHECK( mask.includes(sp_yx));
  BOOST_CHECK(!mask.includes(sp_yxf));
  BOOST_CHECK(!mask.includes(sp_yxg));
  BOOST_CHECK( mask.includes(sp_yy));
  BOOST_CHECK(!mask.includes(sp_yyf));
  BOOST_CHECK(!mask.includes(sp_yyg));
}

void
add_restrictions_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_empty_restriction));
  suite->add(BOOST_TEST_CASE(&test_simple_include));
  suite->add(BOOST_TEST_CASE(&test_simple_exclude));
  suite->add(BOOST_TEST_CASE(&test_include_exclude));
  suite->add(BOOST_TEST_CASE(&test_exclude_include));
  suite->add(BOOST_TEST_CASE(&test_invalid_paths));
  suite->add(BOOST_TEST_CASE(&test_get_parent_paths));
  suite->add(BOOST_TEST_CASE(&test_include_depth_0));
  suite->add(BOOST_TEST_CASE(&test_include_depth_1));

}
#endif // BUILD_UNIT_TESTS
