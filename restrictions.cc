// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "safe_map.hh"
#include "vector.hh"
#include "restrictions.hh"
#include "file_io.hh"
#include "roster.hh"
#include "database.hh" // for parent_roster

#ifdef BUILD_UNIT_TESTS
# include "unit_tests.hh"
# include "constants.hh"
#else
# include "work.hh"
#endif

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
map_nodes(map<node_id, restricted_path::status> & node_map,
          roster_t const & roster,
          set<file_path> const & paths,
          set<file_path> & known_paths,
          restricted_path::status const status)
{
  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (roster.has_node(*i))
        {
          known_paths.insert(*i);
          node_id nid = roster.get_node(*i)->self;

          map<node_id, restricted_path::status>::iterator n
            = node_map.find(nid);
          if (n != node_map.end())
            N(n->second == status,
              F("conflicting include/exclude on path '%s'") % *i);
          else
            node_map.insert(make_pair(nid, status));
        }
    }
}

static void
map_nodes(map<node_id, restricted_path::status> & node_map,
          roster_t const & roster,
          set<file_path> const & included_paths,
          set<file_path> const & excluded_paths,
          set<file_path> & known_paths)
{
  map_nodes(node_map, roster, included_paths, known_paths,
            restricted_path::included);
  map_nodes(node_map, roster, excluded_paths, known_paths,
            restricted_path::excluded);
}

static void
map_paths(map<file_path, restricted_path::status> & path_map,
          set<file_path> const & paths,
          restricted_path::status const status)
{
  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      map<file_path, restricted_path::status>::iterator p = path_map.find(*i);
      if (p != path_map.end())
        N(p->second == status,
          F("conflicting include/exclude on path '%s'") % *i);
      else
        path_map.insert(make_pair(*i, status));
    }
}

namespace
{
  struct unknown_p
  {
    virtual bool operator()(file_path const &) const = 0;
  protected:
    unknown_p() {}
    virtual ~unknown_p() {}
  };

  struct unknown_node : public unknown_p
  {
    explicit unknown_node(set<file_path> const & known_paths)
      : known_paths(known_paths)
    {}
    virtual bool operator()(file_path const & p) const
    {
      return known_paths.find(p) == known_paths.end();
    }

  private:
    set<file_path> const & known_paths;
  };

  struct unknown_path : public unknown_p
  {
    virtual bool operator()(file_path const & p) const
    {
      return !path_exists(p);
    }
  };

#ifndef BUILD_UNIT_TESTS
  // ignored paths are allowed into the restriction but are not considered
  // invalid if they are found in none of the restriction's rosters
  // this is only relevant to the main program, not the unit tests
  struct unknown_unignored_node : public unknown_p
  {
    explicit unknown_unignored_node(set<file_path> const & known_paths,
                                    workspace & work)
      : known_paths(known_paths), work(work)
    {}
    virtual bool operator()(file_path const & p) const
    {
      return (known_paths.find(p) == known_paths.end()
              && !work.ignore_file(p));
    }

  private:
    set<file_path> const & known_paths;
    workspace & work;
  };

  struct unknown_unignored_path : public unknown_p
  {
    explicit unknown_unignored_path(workspace & work)
      : work(work)
    {}
    virtual bool operator()(file_path const & p) const
    {
      return !path_exists(p) && !work.ignore_file(p);
    }
  private:
    workspace & work;
  };
#endif
}

static void
validate_paths(set<file_path> const & included_paths,
               set<file_path> const & excluded_paths,
               unknown_p const & is_unknown)
{
  int bad = 0;

  for (set<file_path>::const_iterator i = included_paths.begin();
       i != included_paths.end(); ++i)
    if (is_unknown(*i))
      {
        bad++;
        W(F("restriction includes unknown path '%s'") % *i);
      }

  for (set<file_path>::const_iterator i = excluded_paths.begin();
       i != excluded_paths.end(); ++i)
    if (is_unknown(*i))
      {
        bad++;
        W(F("restriction excludes unknown path '%s'") % *i);
      }

  N(bad == 0, FP("%d unknown path", "%d unknown paths", bad) % bad);
}

restriction::restriction(std::vector<file_path> const & includes,
                         std::vector<file_path> const & excludes,
                         long depth)
  : included_paths(includes.begin(), includes.end()),
    excluded_paths(excludes.begin(), excludes.end()),
    depth(depth)
{}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster, included_paths, excluded_paths, known_paths);
  validate_paths(included_paths, excluded_paths, unknown_node(known_paths));
}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster1,
                                   roster_t const & roster2) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster1, included_paths, excluded_paths, known_paths);
  map_nodes(node_map, roster2, included_paths, excluded_paths, known_paths);
  validate_paths(included_paths, excluded_paths, unknown_node(known_paths));
}

node_restriction::node_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   parent_map const & rosters1,
                                   roster_t const & roster2) :
  restriction(includes, excludes, depth)
{
  for (parent_map::const_iterator i = rosters1.begin();
       i != rosters1.end(); i++)
    map_nodes(node_map, parent_roster(i),
              included_paths, excluded_paths, known_paths);
  map_nodes(node_map, roster2, included_paths, excluded_paths, known_paths);
  validate_paths(included_paths, excluded_paths, unknown_node(known_paths));
}


path_restriction::path_restriction(std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   validity_check vc) :
  restriction(includes, excludes, depth)
{
  map_paths(path_map, included_paths, restricted_path::included);
  map_paths(path_map, excluded_paths, restricted_path::excluded);

  if (vc == check_paths)
    validate_paths(included_paths, excluded_paths, unknown_path());
}

// The constructor variants that take a workspace argument are only used in
// the main program, not the unit tests.  Conditional compilation lets us
// leave work.o out of the unit_tester binary.
#ifndef BUILD_UNIT_TESTS
node_restriction::node_restriction(workspace & work,
                                   std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster, included_paths, excluded_paths, known_paths);
  validate_paths(included_paths, excluded_paths,
                 unknown_unignored_node(known_paths, work));
}

node_restriction::node_restriction(workspace & work,
                                   std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   roster_t const & roster1,
                                   roster_t const & roster2) :
  restriction(includes, excludes, depth)
{
  map_nodes(node_map, roster1, included_paths, excluded_paths, known_paths);
  map_nodes(node_map, roster2, included_paths, excluded_paths, known_paths);

  validate_paths(included_paths, excluded_paths,
                 unknown_unignored_node(known_paths, work));
}

node_restriction::node_restriction(workspace & work,
                                   std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   parent_map const & rosters1,
                                   roster_t const & roster2) :
  restriction(includes, excludes, depth)
{
  for (parent_map::const_iterator i = rosters1.begin();
       i != rosters1.end(); i++)
    map_nodes(node_map, parent_roster(i),
              included_paths, excluded_paths, known_paths);
  map_nodes(node_map, roster2, included_paths, excluded_paths, known_paths);
  validate_paths(included_paths, excluded_paths,
                 unknown_unignored_node(known_paths, work));
}


path_restriction::path_restriction(workspace & work,
                                   std::vector<file_path> const & includes,
                                   std::vector<file_path> const & excludes,
                                   long depth,
                                   validity_check vc) :
  restriction(includes, excludes, depth)
{
  map_paths(path_map, included_paths, restricted_path::included);
  map_paths(path_map, excluded_paths, restricted_path::excluded);

  if (vc == check_paths)
    validate_paths(included_paths, excluded_paths,
                   unknown_unignored_path(work));
}
#endif


bool
node_restriction::includes(roster_t const & roster, node_id nid) const
{
  MM(roster);
  I(roster.has_node(nid));

  file_path fp;
  roster.get_name(nid, fp);

  if (empty())
    {
      if (depth != -1)
        {
          int path_depth = fp.depth();
          if (path_depth <= depth)
            {
              L(FL("depth includes nid %d path '%s'") % nid % fp);
              return true;
            }
          else
            {
              L(FL("depth excludes nid %d path '%s'") % nid % fp);
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

  while (!null_node(current) && (depth == -1 || path_depth <= depth))
    {
      map<node_id, restricted_path::status>::const_iterator
        r = node_map.find(current);

      if (r != node_map.end())
        {
          switch (r->second)
            {
            case restricted_path::included:
              L(FL("explicit include of nid %d path '%s'")
                % current % fp);
              return true;

            case restricted_path::excluded:
              L(FL("explicit exclude of nid %d path '%s'")
                % current % fp);
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
        % nid % fp);
      return true;
    }
  else
    {
      L(FL("(debug) default exclude of nid %d path '%s'")
        % nid % fp);
      return false;
    }
}

bool
path_restriction::includes(file_path const & pth) const
{
  if (empty())
    {
      if (depth != -1)
        {
          int path_depth = pth.depth();
          if (path_depth <= depth)
            {
              L(FL("depth includes path '%s'") % pth);
              return true;
            }
          else
            {
              L(FL("depth excludes path '%s'") % pth);
              return false;
            }
        }
      else
        {
          L(FL("empty include of path '%s'") % pth);
          return true;
        }
    }

  int path_depth = 0;
  file_path fp = pth;
  while (depth == -1 || path_depth <= depth)
    {
      map<file_path, restricted_path::status>::const_iterator
        r = path_map.find(fp);

      if (r != path_map.end())
        {
          switch (r->second)
            {
            case restricted_path::included:
              L(FL("explicit include of path '%s'") % pth);
              return true;

            case restricted_path::excluded:
              L(FL("explicit exclude of path '%s'") % pth);
              return false;
            }
        }

      if (fp.empty())
        break;
      fp = fp.dirname();
      path_depth++;
    }

  if (included_paths.empty())
    {
      L(FL("default include of path '%s'") % pth);
      return true;
    }
  else
    {
      L(FL("default exclude of path '%s'") % pth);
      return false;
    }
}

///////////////////////////////////////////////////////////////////////
// tests
///////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS

using std::string;

// f's and g's are files
// x's and y's are directories
// and this is rather painful

#define fp_root file_path_internal("")
#define fp_f file_path_internal("f")
#define fp_g file_path_internal("g")

#define fp_x file_path_internal("x")
#define fp_xf file_path_internal("x/f")
#define fp_xg file_path_internal("x/g")
#define fp_xx file_path_internal("x/x")
#define fp_xxf file_path_internal("x/x/f")
#define fp_xxg file_path_internal("x/x/g")
#define fp_xy file_path_internal("x/y")
#define fp_xyf file_path_internal("x/y/f")
#define fp_xyg file_path_internal("x/y/g")

#define fp_y file_path_internal("y")
#define fp_yf file_path_internal("y/f")
#define fp_yg file_path_internal("y/g")
#define fp_yx file_path_internal("y/x")
#define fp_yxf file_path_internal("y/x/f")
#define fp_yxg file_path_internal("y/x/g")
#define fp_yy file_path_internal("y/y")
#define fp_yyf file_path_internal("y/y/f")
#define fp_yyg file_path_internal("y/y/g")

namespace
{
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

  file_id fid_f  (string(constants::idlen_bytes, '\x11'));
  file_id fid_g  (string(constants::idlen_bytes, '\x22'));

  file_id fid_xf (string(constants::idlen_bytes, '\x33'));
  file_id fid_xg (string(constants::idlen_bytes, '\x44'));
  file_id fid_xxf(string(constants::idlen_bytes, '\x55'));
  file_id fid_xxg(string(constants::idlen_bytes, '\x66'));
  file_id fid_xyf(string(constants::idlen_bytes, '\x77'));
  file_id fid_xyg(string(constants::idlen_bytes, '\x88'));

  file_id fid_yf (string(constants::idlen_bytes, '\x99'));
  file_id fid_yg (string(constants::idlen_bytes, '\xaa'));
  file_id fid_yxf(string(constants::idlen_bytes, '\xbb'));
  file_id fid_yxg(string(constants::idlen_bytes, '\xcc'));
  file_id fid_yyf(string(constants::idlen_bytes, '\xdd'));
  file_id fid_yyg(string(constants::idlen_bytes, '\xee'));
}

static void setup(roster_t & roster)
{
  temp_node_id_source nis;

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

  roster.attach_node(nid_root, fp_root);
  roster.attach_node(nid_f, fp_f);
  roster.attach_node(nid_g, fp_g);

  roster.attach_node(nid_x,   fp_x);
  roster.attach_node(nid_xf,  fp_xf);
  roster.attach_node(nid_xg,  fp_xg);
  roster.attach_node(nid_xx,  fp_xx);
  roster.attach_node(nid_xxf, fp_xxf);
  roster.attach_node(nid_xxg, fp_xxg);
  roster.attach_node(nid_xy,  fp_xy);
  roster.attach_node(nid_xyf, fp_xyf);
  roster.attach_node(nid_xyg, fp_xyg);

  roster.attach_node(nid_y,   fp_y);
  roster.attach_node(nid_yf,  fp_yf);
  roster.attach_node(nid_yg,  fp_yg);
  roster.attach_node(nid_yx,  fp_yx);
  roster.attach_node(nid_yxf, fp_yxf);
  roster.attach_node(nid_yxg, fp_yxg);
  roster.attach_node(nid_yy,  fp_yy);
  roster.attach_node(nid_yyf, fp_yyf);
  roster.attach_node(nid_yyg, fp_yyg);

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

  UNIT_TEST_CHECK(pmask.includes(fp_root));
  UNIT_TEST_CHECK(pmask.includes(fp_f));
  UNIT_TEST_CHECK(pmask.includes(fp_g));

  UNIT_TEST_CHECK(pmask.includes(fp_x));
  UNIT_TEST_CHECK(pmask.includes(fp_xf));
  UNIT_TEST_CHECK(pmask.includes(fp_xg));
  UNIT_TEST_CHECK(pmask.includes(fp_xx));
  UNIT_TEST_CHECK(pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(pmask.includes(fp_xy));
  UNIT_TEST_CHECK(pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(pmask.includes(fp_xyg));

  UNIT_TEST_CHECK(pmask.includes(fp_y));
  UNIT_TEST_CHECK(pmask.includes(fp_yf));
  UNIT_TEST_CHECK(pmask.includes(fp_yg));
  UNIT_TEST_CHECK(pmask.includes(fp_yx));
  UNIT_TEST_CHECK(pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(pmask.includes(fp_yy));
  UNIT_TEST_CHECK(pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, simple_include)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x/x"));
  includes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

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

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK(!pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK(!pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, simple_exclude)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  excludes.push_back(file_path_internal("x/x"));
  excludes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

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

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK( pmask.includes(fp_f));
  UNIT_TEST_CHECK( pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
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

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

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

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
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

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

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

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK(!pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK(!pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, invalid_roster_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  UNIT_TEST_CHECK_THROW(node_restriction(includes, excludes, -1, roster),
                        informative_failure);
}

UNIT_TEST(restrictions, invalid_workspace_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  UNIT_TEST_CHECK_THROW(path_restriction(includes, excludes, -1),
                    informative_failure);
}

UNIT_TEST(restrictions, ignored_invalid_workspace_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  path_restriction pmask(includes, excludes, -1,
                         path_restriction::skip_check);

  UNIT_TEST_CHECK( pmask.includes(file_path_internal("foo")));
  UNIT_TEST_CHECK(!pmask.includes(file_path_internal("bar")));
}

UNIT_TEST(restrictions, include_depth_0)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 0;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK(!nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

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

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, include_depth_1)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 1;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

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

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, include_depth_1_empty_restriction)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;

  long depth = 1;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

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

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK( pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK( pmask.includes(fp_f));
  UNIT_TEST_CHECK( pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(restrictions, include_depth_2)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 2;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

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

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK(!pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
