// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <string>
#include <vector>

#include "restrictions.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "transforms.hh"
#include "app_state.hh"

using std::make_pair;
using std::map;
using std::set;
using std::vector;

// TODO: add check for relevant rosters to be used by log
//
// i.e.  as log goes back through older and older rosters it may hit one
// that pre-dates any of the nodes in the restriction. the nodes that the
// restriction includes or excludes may not have been born in a sufficiently
// old roster. at this point log should stop because no earlier roster will
// include these nodes.

static void
make_path_set(vector<file_path> const & paths, path_set & split_paths)
{
  for (vector<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      split_path sp;
      i->split(sp);
      split_paths.insert(sp);
    }
}

static void
map_nodes(map<node_id, restricted_path::status> & node_map,
          roster_t const & roster,
          path_set const & paths,
          path_set & known_paths,
          restricted_path::status const status)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (roster.has_node(*i))
        {
          known_paths.insert(*i);
          node_id nid = roster.get_node(*i)->self;

          map<node_id, restricted_path::status>::iterator n = node_map.find(nid);
          if (n != node_map.end())
            N(n->second == status,
              F("conflicting include/exclude on path '%s'") % *i);
          else
            node_map.insert(make_pair(nid, status));
        }
    }
}

static void
map_paths(map<split_path, restricted_path::status> & path_map,
          path_set const & paths,
          restricted_path::status const status)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      map<split_path, restricted_path::status>::iterator p = path_map.find(*i);
      if (p != path_map.end())
        N(p->second == status,
          F("conflicting include/exclude on path '%s'") % *i);
      else
        path_map.insert(make_pair(*i, status));
    }
}

static void
validate_roster_paths(path_set const & included_paths, 
                      path_set const & excluded_paths, 
                      path_set const & known_paths,
                      app_state & app)
{
  int bad = 0;

  for (path_set::const_iterator i = included_paths.begin();
       i != included_paths.end(); ++i)
    {
      // ignored paths are allowed into the restriction but are not
      // considered invalid if they are found in none of the restriction's
      // rosters
      if (known_paths.find(*i) == known_paths.end())
        {
          file_path fp(*i);
          if (!app.lua.hook_ignore_file(fp))
            {
              bad++;
              W(F("restriction includes unknown path '%s'") % *i);
            }
        }
    }

  for (path_set::const_iterator i = excluded_paths.begin();
       i != excluded_paths.end(); ++i)
    {
      if (known_paths.find(*i) == known_paths.end())
        {
          bad++;
          W(F("restriction excludes unknown path '%s'") % *i);
        }
    }

  N(bad == 0, FP("%d unknown path", "%d unknown paths", bad) % bad);
}

void
validate_workspace_paths(path_set const & included_paths, 
                         path_set const & excluded_paths,
                         app_state & app)
{
  int bad = 0;

  for (path_set::const_iterator i = included_paths.begin();
       i != included_paths.end(); ++i)
    {
      if (workspace_root(*i)) 
        continue;

      // ignored paths are allowed into the restriction but are not
      // considered invalid if they are found in none of the restriction's
      // rosters
      file_path fp(*i);
      if (!path_exists(fp) && !app.lua.hook_ignore_file(fp))
        {
          bad++;
          W(F("restriction includes unknown path '%s'") % *i);
        }
    }

  for (path_set::const_iterator i = excluded_paths.begin();
       i != excluded_paths.end(); ++i)
    {
      if (workspace_root(*i)) 
        continue;

      file_path fp(*i);
      if (!path_exists(fp))
        {
          bad++;
          W(F("restriction excludes unknown path '%s'") % *i);
        }
    }

  N(bad == 0, FP("%d unknown path", "%d unknown paths", bad) % bad);
}

restriction::restriction(std::vector<file_path> const & includes,
                         std::vector<file_path> const & excludes,
                         long depth)
  : depth(depth)
{
  make_path_set(includes, included_paths);
  make_path_set(excludes, excluded_paths);
}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster,
                                   app_state & a) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster, included_paths, known_paths, 
            restricted_path::included);
  map_nodes(node_map, roster, excluded_paths, known_paths, 
            restricted_path::excluded);

  validate_roster_paths(included_paths, excluded_paths, known_paths, a);
}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster1,
                                   roster_t const & roster2,
                                   app_state & a) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster1, included_paths, known_paths, 
            restricted_path::included);
  map_nodes(node_map, roster1, excluded_paths, known_paths, 
            restricted_path::excluded);

  map_nodes(node_map, roster2, included_paths, known_paths, 
            restricted_path::included);
  map_nodes(node_map, roster2, excluded_paths, known_paths, 
            restricted_path::excluded);

  validate_roster_paths(included_paths, excluded_paths, known_paths, a);
}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   parent_map const & rosters1,
                                   roster_t const & roster2,
                                   app_state & a) :
  restriction(includes, excludes, depth)
{
  for (parent_map::const_iterator i = rosters1.begin();
       i != rosters1.end();
       i++)
    {
      map_nodes(node_map, parent_roster(i), included_paths, known_paths, 
                restricted_path::included);
      map_nodes(node_map, parent_roster(i), excluded_paths, known_paths, 
                restricted_path::excluded);
    }

  map_nodes(node_map, roster2, included_paths, known_paths, 
            restricted_path::included);
  map_nodes(node_map, roster2, excluded_paths, known_paths, 
            restricted_path::excluded);

  validate_roster_paths(included_paths, excluded_paths, known_paths, a);
}


path_restriction::path_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   app_state & a) :
  restriction(includes, excludes, depth)
{
  map_paths(path_map, included_paths, restricted_path::included);
  map_paths(path_map, excluded_paths, restricted_path::excluded);

  validate_workspace_paths(included_paths, excluded_paths, a);
}

bool
node_restriction::includes(roster_t const & roster, node_id nid) const
{
  MM(roster);
  I(roster.has_node(nid));

  split_path sp;
  roster.get_name(nid, sp);

  if (empty())
    {
      if (depth != -1)
        {
          int path_depth = sp.size() - 1; // -1 to not count root path_component
          if (path_depth <= depth + 1)
            {
              L(FL("depth includes nid %d path '%s'") % nid % file_path(sp));
              return true;
            }
          else
            {
              L(FL("depth excludes nid %d path '%s'") % nid % file_path(sp));
              return false;
            }
        }
      else
        {
          // don't log this, we end up using rather a bit of cpu time just
          // in the logging code, for totally unrestricted operations.
          return true;
        }
    }

  node_id current = nid;
  int path_depth = 0;

  // FIXME: this uses depth+1 because the old semantics of depth=0 were
  // something like "the current directory and its immediate children". it
  // seems somewhat more reasonable here to use depth=0 to mean "exactly
  // this directory" and depth=1 to mean "this directory and its immediate
  // children"

  while (!null_node(current) && (depth == -1 || path_depth <= depth + 1))
    {
      map<node_id, restricted_path::status>::const_iterator 
        r = node_map.find(current);

      if (r != node_map.end())
        {
          switch (r->second)
            {
            case restricted_path::included:
              L(FL("explicit include of nid %d path '%s'") 
                % current % file_path(sp));
              return true;

            case restricted_path::excluded:
              L(FL("explicit exclude of nid %d path '%s'") 
                % current % file_path(sp));
              return false;
            }
        }

      node_t node = roster.get_node(current);
      current = node->parent;
      path_depth++;
    }

  if (included_paths.empty())
    {
      L(FL("default include of nid %d path '%s'") 
        % nid % file_path(sp));
      return true;
    }
  else
    {
      if (global_sanity.debug)
      {
        // printing this slows down "log <file>".
        L(FL("(debug) default exclude of nid %d path '%s'") 
          % nid % file_path(sp));
      }
      return false;
    }
}

bool
path_restriction::includes(split_path const & sp) const
{
  if (empty())
    {
      if (depth != -1)
        {
          int path_depth = sp.size() - 1; // -1 to not count root path_component
          if (path_depth <= depth + 1)
            {
              L(FL("depth includes path '%s'") % file_path(sp));
              return true;
            }
          else
            {
              L(FL("depth excludes path '%s'") % file_path(sp));
              return false;
            }
        }
      else
        {
          L(FL("empty include of path '%s'") % file_path(sp));
          return true;
        }
    }

  split_path current(sp);
  int path_depth = 0;

  // FIXME: this uses depth+1 because the old semantics of depth=0 were
  // something like "the current directory and its immediate children". it
  // seems somewhat more reasonable here to use depth=0 to mean "exactly
  // this directory" and depth=1 to mean "this directory and its immediate
  // children"

  while (!current.empty() && (depth == -1 || path_depth <= depth + 1))
    {
      map<split_path, restricted_path::status>::const_iterator 
        r = path_map.find(current);

      if (r != path_map.end())
        {
          switch (r->second)
            {
            case restricted_path::included:
              L(FL("explicit include of path '%s'") 
                % file_path(sp));
              return true;

            case restricted_path::excluded:
              L(FL("explicit exclude of path '%s'") 
                % file_path(sp));
              return false;
            }
        }

      current.pop_back();
      path_depth++;
    }

  if (included_paths.empty())
    {
      L(FL("default include of path '%s'") 
        % file_path(sp));
      return true;
    }
  else
    {
      L(FL("default exclude of path '%s'") 
        % file_path(sp));
      return false;
    }
}

///////////////////////////////////////////////////////////////////////
// tests
///////////////////////////////////////////////////////////////////////

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

  // these directories must exist for the path_restrictions to be valid.  it
  // is a bit lame to be creating directories arbitrarily like this. perhaps
  // unit_tests should run in a unit_tests.dir or something.

  mkdir_p(file_path_internal("x/x"));
  mkdir_p(file_path_internal("x/y"));
  mkdir_p(file_path_internal("y/x"));
  mkdir_p(file_path_internal("y/y"));

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

UNIT_TEST(restrictions, empty_restriction)
{
  roster_t roster;
  setup(roster);

  // check restricted nodes

  node_restriction nmask;

  UNIT_TEST_CHECK(nmask.empty());

  UNIT_TEST_CHECK(nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK(nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK(nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask;

  UNIT_TEST_CHECK(pmask.empty());

  UNIT_TEST_CHECK(pmask.includes(sp_root));
  UNIT_TEST_CHECK(pmask.includes(sp_f));
  UNIT_TEST_CHECK(pmask.includes(sp_g));

  UNIT_TEST_CHECK(pmask.includes(sp_x));
  UNIT_TEST_CHECK(pmask.includes(sp_xf));
  UNIT_TEST_CHECK(pmask.includes(sp_xg));
  UNIT_TEST_CHECK(pmask.includes(sp_xx));
  UNIT_TEST_CHECK(pmask.includes(sp_xxf));
  UNIT_TEST_CHECK(pmask.includes(sp_xxg));
  UNIT_TEST_CHECK(pmask.includes(sp_xy));
  UNIT_TEST_CHECK(pmask.includes(sp_xyf));
  UNIT_TEST_CHECK(pmask.includes(sp_xyg));

  UNIT_TEST_CHECK(pmask.includes(sp_y));
  UNIT_TEST_CHECK(pmask.includes(sp_yf));
  UNIT_TEST_CHECK(pmask.includes(sp_yg));
  UNIT_TEST_CHECK(pmask.includes(sp_yx));
  UNIT_TEST_CHECK(pmask.includes(sp_yxf));
  UNIT_TEST_CHECK(pmask.includes(sp_yxg));
  UNIT_TEST_CHECK(pmask.includes(sp_yy));
  UNIT_TEST_CHECK(pmask.includes(sp_yyf));
  UNIT_TEST_CHECK(pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, simple_include)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x/x"));
  includes.push_back(file_path_internal("y/y"));

  app_state app;

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(sp_root));
  UNIT_TEST_CHECK(!pmask.includes(sp_f));
  UNIT_TEST_CHECK(!pmask.includes(sp_g));

  UNIT_TEST_CHECK(!pmask.includes(sp_x));
  UNIT_TEST_CHECK(!pmask.includes(sp_xf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xg));
  UNIT_TEST_CHECK( pmask.includes(sp_xx));
  UNIT_TEST_CHECK( pmask.includes(sp_xxf));
  UNIT_TEST_CHECK( pmask.includes(sp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xy));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyg));

  UNIT_TEST_CHECK(!pmask.includes(sp_y));
  UNIT_TEST_CHECK(!pmask.includes(sp_yf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yx));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxg));
  UNIT_TEST_CHECK( pmask.includes(sp_yy));
  UNIT_TEST_CHECK( pmask.includes(sp_yyf));
  UNIT_TEST_CHECK( pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, simple_exclude)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  excludes.push_back(file_path_internal("x/x"));
  excludes.push_back(file_path_internal("y/y"));

  app_state app;

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(sp_root));
  UNIT_TEST_CHECK( pmask.includes(sp_f));
  UNIT_TEST_CHECK( pmask.includes(sp_g));

  UNIT_TEST_CHECK( pmask.includes(sp_x));
  UNIT_TEST_CHECK( pmask.includes(sp_xf));
  UNIT_TEST_CHECK( pmask.includes(sp_xg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xx));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxg));
  UNIT_TEST_CHECK( pmask.includes(sp_xy));
  UNIT_TEST_CHECK( pmask.includes(sp_xyf));
  UNIT_TEST_CHECK( pmask.includes(sp_xyg));

  UNIT_TEST_CHECK( pmask.includes(sp_y));
  UNIT_TEST_CHECK( pmask.includes(sp_yf));
  UNIT_TEST_CHECK( pmask.includes(sp_yg));
  UNIT_TEST_CHECK( pmask.includes(sp_yx));
  UNIT_TEST_CHECK( pmask.includes(sp_yxf));
  UNIT_TEST_CHECK( pmask.includes(sp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yy));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, include_exclude)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));
  excludes.push_back(file_path_internal("x/x"));
  excludes.push_back(file_path_internal("y/y"));

  app_state app;

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(sp_root));
  UNIT_TEST_CHECK(!pmask.includes(sp_f));
  UNIT_TEST_CHECK(!pmask.includes(sp_g));

  UNIT_TEST_CHECK( pmask.includes(sp_x));
  UNIT_TEST_CHECK( pmask.includes(sp_xf));
  UNIT_TEST_CHECK( pmask.includes(sp_xg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xx));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxg));
  UNIT_TEST_CHECK( pmask.includes(sp_xy));
  UNIT_TEST_CHECK( pmask.includes(sp_xyf));
  UNIT_TEST_CHECK( pmask.includes(sp_xyg));

  UNIT_TEST_CHECK( pmask.includes(sp_y));
  UNIT_TEST_CHECK( pmask.includes(sp_yf));
  UNIT_TEST_CHECK( pmask.includes(sp_yg));
  UNIT_TEST_CHECK( pmask.includes(sp_yx));
  UNIT_TEST_CHECK( pmask.includes(sp_yxf));
  UNIT_TEST_CHECK( pmask.includes(sp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yy));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, exclude_include)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  // note that excludes higher up the tree than the top
  // include are rather pointless -- nothing above the
  // top include is included anyway
  excludes.push_back(file_path_internal("x"));
  excludes.push_back(file_path_internal("y"));
  includes.push_back(file_path_internal("x/x"));
  includes.push_back(file_path_internal("y/y"));

  app_state app;

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(sp_root));
  UNIT_TEST_CHECK(!pmask.includes(sp_f));
  UNIT_TEST_CHECK(!pmask.includes(sp_g));

  UNIT_TEST_CHECK(!pmask.includes(sp_x));
  UNIT_TEST_CHECK(!pmask.includes(sp_xf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xg));
  UNIT_TEST_CHECK( pmask.includes(sp_xx));
  UNIT_TEST_CHECK( pmask.includes(sp_xxf));
  UNIT_TEST_CHECK( pmask.includes(sp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xy));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyg));

  UNIT_TEST_CHECK(!pmask.includes(sp_y));
  UNIT_TEST_CHECK(!pmask.includes(sp_yf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yx));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxg));
  UNIT_TEST_CHECK( pmask.includes(sp_yy));
  UNIT_TEST_CHECK( pmask.includes(sp_yyf));
  UNIT_TEST_CHECK( pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, invalid_roster_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  app_state app;
  UNIT_TEST_CHECK_THROW(node_restriction(includes, excludes, -1, roster, app), 
                    informative_failure);
}

UNIT_TEST(restrictions, invalid_workspace_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  app_state app;
  UNIT_TEST_CHECK_THROW(path_restriction(includes, excludes, -1, app),
                    informative_failure);
}

UNIT_TEST(restrictions, include_depth_0)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  app_state app;
  // FIXME: depth == 0 currently means directory + immediate children
  // this should be changed to mean just the named directory but for
  // compatibility with old restrictions this behaviour has been preserved
  long depth = 0;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(sp_root));
  UNIT_TEST_CHECK(!pmask.includes(sp_f));
  UNIT_TEST_CHECK(!pmask.includes(sp_g));

  UNIT_TEST_CHECK( pmask.includes(sp_x));
  UNIT_TEST_CHECK( pmask.includes(sp_xf));
  UNIT_TEST_CHECK( pmask.includes(sp_xg));
  UNIT_TEST_CHECK( pmask.includes(sp_xx));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxg));
  UNIT_TEST_CHECK( pmask.includes(sp_xy));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyg));

  UNIT_TEST_CHECK( pmask.includes(sp_y));
  UNIT_TEST_CHECK( pmask.includes(sp_yf));
  UNIT_TEST_CHECK( pmask.includes(sp_yg));
  UNIT_TEST_CHECK( pmask.includes(sp_yx));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxg));
  UNIT_TEST_CHECK( pmask.includes(sp_yy));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, include_depth_0_empty_restriction)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;

  app_state app;
  // FIXME: depth == 0 currently means directory + immediate children
  // this should be changed to mean just the named directory but for
  // compatibility with old restrictions this behaviour has been preserved
  long depth = 0;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster, app);

  UNIT_TEST_CHECK( nmask.empty());

  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth, app);

  UNIT_TEST_CHECK( pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(sp_root));
  UNIT_TEST_CHECK( pmask.includes(sp_f));
  UNIT_TEST_CHECK( pmask.includes(sp_g));

  UNIT_TEST_CHECK( pmask.includes(sp_x));
  UNIT_TEST_CHECK(!pmask.includes(sp_xf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xx));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_xy));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_xyg));

  UNIT_TEST_CHECK( pmask.includes(sp_y));
  UNIT_TEST_CHECK(!pmask.includes(sp_yf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yx));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(sp_yy));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(sp_yyg));
}

UNIT_TEST(restrictions, include_depth_1)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  app_state app;
  // FIXME: depth == 1 currently means directory + children + grand children
  // this should be changed to mean directory + immediate children but for
  // compatibility with old restrictions this behaviour has been preserved
  long depth = 1;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster, app);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth, app);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(sp_root));
  UNIT_TEST_CHECK(!pmask.includes(sp_f));
  UNIT_TEST_CHECK(!pmask.includes(sp_g));

  UNIT_TEST_CHECK( pmask.includes(sp_x));
  UNIT_TEST_CHECK( pmask.includes(sp_xf));
  UNIT_TEST_CHECK( pmask.includes(sp_xg));
  UNIT_TEST_CHECK( pmask.includes(sp_xx));
  UNIT_TEST_CHECK( pmask.includes(sp_xxf));
  UNIT_TEST_CHECK( pmask.includes(sp_xxg));
  UNIT_TEST_CHECK( pmask.includes(sp_xy));
  UNIT_TEST_CHECK( pmask.includes(sp_xyf));
  UNIT_TEST_CHECK( pmask.includes(sp_xyg));

  UNIT_TEST_CHECK( pmask.includes(sp_y));
  UNIT_TEST_CHECK( pmask.includes(sp_yf));
  UNIT_TEST_CHECK( pmask.includes(sp_yg));
  UNIT_TEST_CHECK( pmask.includes(sp_yx));
  UNIT_TEST_CHECK( pmask.includes(sp_yxf));
  UNIT_TEST_CHECK( pmask.includes(sp_yxg));
  UNIT_TEST_CHECK( pmask.includes(sp_yy));
  UNIT_TEST_CHECK( pmask.includes(sp_yyf));
  UNIT_TEST_CHECK( pmask.includes(sp_yyg));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
