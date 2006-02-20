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

// TODO: add support for --depth (replace recursive boolean with depth value)
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
      L(F("path '%s' %d %d") % sp % old_state % new_state);
      N(false, F("conflicting include/exclude on path '%s'") % sp);
    }
}

static void
update_path_map(map<split_path, path_entry> & path_map, split_path const & sp, path_state const state)
{
  map<split_path, path_entry>::iterator p = path_map.find(sp);
  if (p != path_map.end())
    {
      path_state merged;
      merge_states(p->second.state, state, merged, sp);
      p->second.state = merged;
    }
  else
    {
      path_map.insert(make_pair(sp, path_entry(state)));
    }
}

static void
add_included_paths(map<split_path, path_entry> & path_map, path_set const & includes)
{
  for (path_set::const_iterator i = includes.begin(); i != includes.end(); ++i)
    {
      split_path sp(*i);
      path_state state = explicit_include;

      while (!sp.empty())
        {
          update_path_map(path_map, sp, state);
          sp.pop_back();
          state = implicit_include;
        }

    }
}

static void
add_excluded_paths(map<split_path, path_entry> & path_map, path_set const & excludes)
{
  for (path_set::const_iterator i = excludes.begin(); i != excludes.end(); ++i)
    {
      update_path_map(path_map, *i, explicit_exclude);
    }
}

static void
update_node_map(map<node_id, path_state> & node_map, node_id const nid, split_path const & sp, path_state const state)
{
  map<node_id, path_state>::iterator n = node_map.find(nid);
  if (n != node_map.end())
    {
      path_state merged;
      merge_states(n->second, state, merged, sp);
      n->second = merged;
    }
  else
    {
      node_map.insert(make_pair(nid, state));
    }
}

static void
add_included_nodes(map<node_id, path_state> & node_map, 
                   map<split_path, path_entry> & path_map,
                   path_set const & includes, 
                   roster_t const & roster)
{
  for (path_set::const_iterator i = includes.begin(); i != includes.end(); ++i)
    {

      // TODO: (future) handle some sort of peg revision path syntax here.
      // note that the idea of a --peg option doesn't work because each
      // path may be relative to a different revision.

      if (roster.has_node(*i)) 
        {

          // FIXME: this may be better as a list of invalid paths
          map<split_path, path_entry>::iterator p = path_map.find(*i);
          I(p != path_map.end());

          p->second.roster_count++;

          // TODO: proper recursive wildcard paths like foo/...  
          // for now explicit paths are recursive
          // and implicit parents are non-recursive

          // currently we need to insert the parents of included nodes so that
          // the included nodes are not orphaned in a restricted roster.  this
          // happens in cases like add "a" + add "a/b" when only "a/b" is
          // included. i.e. "a" must be included for "a/b" to be valid. this
          // isn't entirely sensible and should probably be revisited. it does
          // match the current (old restrictions) semantics though.

          node_id nid = roster.get_node(*i)->self;

          path_state state = explicit_include;

          while (!null_node(nid))
            {
              split_path sp;
              roster.get_name(nid, sp);

              update_node_map(node_map, nid, sp, state);

              nid = roster.get_node(nid)->parent;
              state = implicit_include;
            }
        }
    }
}

static void
add_excluded_nodes(map<node_id, path_state> & node_map, 
                   map<split_path, path_entry> & path_map,
                   path_set const & excludes, 
                   roster_t const & roster)
{
  for (path_set::const_iterator i = excludes.begin(); i != excludes.end(); ++i)
    {

      // TODO: (future) handle some sort of peg revision path syntax here.
      // note that the idea of a --peg option doesn't work because each
      // path may be relative to a different revision.

      if (roster.has_node(*i)) 
        {
          // FIXME: this may be better as a list of invalid paths
          map<split_path, path_entry>::iterator p = path_map.find(*i);
          I(p != path_map.end());

          p->second.roster_count++;

          // TODO: proper recursive wildcard paths like foo/...  
          // for now explicit paths are recursive
          // and implicit parents are non-recursive

          node_id nid = roster.get_node(*i)->self;

          split_path sp;
          roster.get_name(nid, sp);
          update_node_map(node_map, nid, sp, explicit_exclude);
        }
    }

}

static void
check_paths(map<split_path, path_entry> const & path_map)
{
  int bad = 0;

  for (map<split_path, path_entry>::const_iterator i = path_map.begin(); 
       i != path_map.end(); ++i)
    {
      L(F("%d %d %s") % i->second.state % i->second.roster_count % i->first);
 
      if (i->second.state == explicit_include && i->second.roster_count == 0)
        {
          bad++;
          W(F("unknown path included %s") % i->first);
        }
      else if (i->second.state == explicit_exclude && i->second.roster_count == 0)
        {
          bad++;
          W(F("unknown path excluded %s") % i->first);
        }
    }
  
  N(bad == 0, F("%d unknown paths") % bad);
}


////////////////////////////////////////////////////////////////////////////////
// public constructors
////////////////////////////////////////////////////////////////////////////////

// FIXME: add_paths and add_nodes should take the nodes to add and their state
// includes/explicit_include, excludes/explicit_exclude, parents/implicit_include
// then we need things to generate lists of parent paths and nodes

restriction::restriction(vector<utf8> const & include_args,
                         vector<utf8> const & exclude_args,
                         roster_t const & roster)
{
  path_set includes, excludes;
  make_path_set(include_args, includes);
  make_path_set(exclude_args, excludes);

  add_included_paths(path_map, includes);
  add_excluded_paths(path_map, excludes);

  add_included_nodes(node_map, path_map, includes, roster);
  add_excluded_nodes(node_map, path_map, excludes, roster);

  default_result = includes.empty();

  check_paths(path_map);
}

restriction::restriction(vector<utf8> const & include_args,
                         vector<utf8> const & exclude_args,
                         roster_t const & roster1,
                         roster_t const & roster2)
{
  path_set includes, excludes;
  make_path_set(include_args, includes);
  make_path_set(exclude_args, excludes);

  add_included_paths(path_map, includes);
  add_excluded_paths(path_map, excludes);

  add_included_nodes(node_map, path_map, includes, roster1);
  add_excluded_nodes(node_map, path_map, excludes, roster1);

  add_included_nodes(node_map, path_map, includes, roster2);
  add_excluded_nodes(node_map, path_map, excludes, roster2);

  default_result = includes.empty();

  check_paths(path_map);
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
  if (node_map.empty()) 
    {
      L(F("empty include of nid %d path '%s'") % nid % file_path(sp));
      return true;
    }

  node_id current = nid;

  while (!null_node(current)) 
    {
      map<node_id, path_state>::const_iterator r = node_map.find(current);

      if (r != node_map.end()) 
        {
          switch (r->second) 
            {
            case explicit_include:
              L(F("explicit include of nid %d path '%s'") % current % file_path(sp));
              return true;

            case explicit_exclude:
              L(F("explicit exclude of nid %d path '%s'") % current % file_path(sp));
              return false;

            case implicit_include:
              // this is non-recursive and requires an exact match
              if (current == nid)
                {
                  L(F("implicit include of nid %d path '%s'") % current % file_path(sp));
                  return true;
                }
            }
        }

      node_t node = roster.get_node(current);
      current = node->parent;
    }

  if (default_result)
    {
      L(F("default include of nid %d path '%s'\n") % nid % file_path(sp));
      return true;
    }
  else
    {
      L(F("default exclude of nid %d path '%s'\n") % nid % file_path(sp));
      return false;
    }
}

bool
restriction::includes(split_path const & sp) const
{
  if (path_map.empty()) 
    {
      L(F("empty include of path '%s'") % file_path(sp));
      return true;
    }

  split_path current(sp);

  while (!current.empty())
    {
      map<split_path, path_entry>::const_iterator r = path_map.find(current);

      if (r != path_map.end())
        {
          switch (r->second.state) 
            {
            case explicit_include:
              L(F("explicit include of path '%s'") % file_path(sp));
              return true;

            case explicit_exclude:
              L(F("explicit exclude of path '%s'") % file_path(sp));
              return false;

            case implicit_include:
              // this is non-recursive and requires an exact match
              if (current == sp)
                {
                  L(F("implicit include of path '%s'") % file_path(sp));
                  return true;
                }
            }
        }

      current.pop_back();
    }

  if (default_result)
    {
      L(F("default include of path '%s'\n") % file_path(sp));
      return true;
    }
  else
    {
      L(F("default exclude of path '%s'\n") % file_path(sp));
      return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
////////////////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
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

  restriction mask;
  
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

  restriction mask(includes, excludes, roster);

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

  restriction mask(includes, excludes, roster);

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

  restriction mask(includes, excludes, roster);

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

  restriction mask(includes, excludes, roster);

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

  BOOST_CHECK_THROW(restriction(includes, excludes, roster), informative_failure);
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
}
#endif // BUILD_UNIT_TESTS
