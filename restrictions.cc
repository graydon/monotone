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
    }
}

void
restriction::add_nodes(roster_t const & roster)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      // TODO: (future) handle some sort of peg revision path syntax here.
      // note that the idea of a --peg option doesn't work because each
      // path may be relative to a different revision.

      if (roster.has_node(*i)) 
        {
          // TODO: proper recursive wildcard paths like foo/...  
          // for now explicit paths are recursive
          // and implicit parents are non-recursive

          // TODO: possibly fail with nice error if path is already explicitly
          // in the map?

          // currently we need to insert the parents of included nodes so that
          // the included nodes are not orphaned in a restricted roster.  this
          // happens in cases like add "a" + add "a/b" when only "a/b" is
          // included. i.e. "a" must be included for "a/b" to be valid. this
          // isn't entirely sensible and should probably be revisited. it does
          // match the current (old restrictions) semantics though.

          valid_paths.insert(*i);

          bool recursive = true;
          node_id nid = roster.get_node(*i)->self;

          while (!null_node(nid))
            {
              split_path sp;
              roster.get_name(nid, sp);
              L(F("adding nid %d path '%s' recursive %s") % nid % file_path(sp) % recursive);

              restricted_node_map[nid] |= recursive;
              restricted_path_map[sp] |= recursive;

              recursive = false;

              nid = roster.get_node(nid)->parent;
            }
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
  MM(roster);
  I(roster.has_node(nid));

  // empty restriction includes everything
  if (restricted_node_map.empty()) 
    {
      split_path sp;
      roster.get_name(nid, sp);
      L(F("included nid %d path '%s'") % nid % file_path(sp));
      return true;
    }

  node_id current = nid;

  while (!null_node(current)) 
    {
      split_path sp;
      roster.get_name(current, sp);

      map<node_id, bool>::const_iterator r = restricted_node_map.find(current);

      if (r != restricted_node_map.end()) 
        {
          // found exact node or a explicit/recusrive parent
          if (r->second || current == nid) 
            {
              L(F("included nid %d path '%s'") % current % file_path(sp));
              return true;
            }
        }

      node_t node = roster.get_node(current);
      current = node->parent;
    }

  split_path sp;
  roster.get_name(nid, sp);
  L(F("excluded nid %d path '%s'\n") % nid % file_path(sp));
  
  return false;
}

bool
restriction::includes(split_path const & sp) const
{
  if (restricted_path_map.empty()) 
    {
      L(F("included path '%s'") % file_path(sp));
      return true;
    }

  split_path current(sp);

  while (!current.empty())
    {
      L(F("checking path '%s'\n") % current);
      map<split_path, bool>::const_iterator r = restricted_path_map.find(current);

      if (r != restricted_path_map.end())
        {
          if (r->second || current == sp)
            {
              L(F("included path '%s'") % file_path(sp));
              return true;
            }
        }

      current.pop_back();
    }

  L(F("excluded path '%s'") % file_path(sp));
  return false;

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

////////////////////////////////////////////////////////////////////
//   testing
////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "roster.hh"
#include "sanity.hh"

using std::string;

file_id file1_id(string("1000000000000000000000000000000000000000"));
file_id file2_id(string("2000000000000000000000000000000000000000"));
file_id file3_id(string("3000000000000000000000000000000000000000"));

static void
test_basic_restrictions()
{
  temp_node_id_source nis;
  roster_t roster;

  node_id root_nid = roster.create_dir_node(nis);
  node_id file1_nid = roster.create_file_node(file1_id, nis);
  node_id file2_nid = roster.create_file_node(file2_id, nis);
  node_id file3_nid = roster.create_file_node(file3_id, nis);

  split_path root_path, file1_path, file2_path, file3_path;
  file_path().split(root_path);
  file_path_internal("file1").split(file1_path);
  file_path_internal("file2").split(file2_path);
  file_path_internal("file3").split(file3_path);

  roster.attach_node(root_nid, root_path);
  roster.attach_node(file1_nid, file1_path);
  roster.attach_node(file2_nid, file2_path);
  roster.attach_node(file3_nid, file3_path);

  {
    // empty restriction
    restriction mask;

    BOOST_CHECK(mask.empty());

    // check restricted nodes
    BOOST_CHECK(mask.includes(roster, root_nid));
    BOOST_CHECK(mask.includes(roster, file1_nid));
    BOOST_CHECK(mask.includes(roster, file2_nid));
    BOOST_CHECK(mask.includes(roster, file3_nid));

    // check restricted paths
    BOOST_CHECK(mask.includes(root_path));
    BOOST_CHECK(mask.includes(file1_path));
    BOOST_CHECK(mask.includes(file2_path));
    BOOST_CHECK(mask.includes(file3_path));
  }

  {
    // non-empty restriction
    vector<utf8> args;
    args.push_back(utf8(string("file1")));

    restriction mask(args, roster);

    BOOST_CHECK(!mask.empty());

    // check restricted nodes
    BOOST_CHECK(mask.includes(roster, root_nid));
    BOOST_CHECK(mask.includes(roster, file1_nid));
    BOOST_CHECK(!mask.includes(roster, file2_nid));
    BOOST_CHECK(!mask.includes(roster, file3_nid));

    // check restricted paths
    BOOST_CHECK(mask.includes(root_path));
    BOOST_CHECK(mask.includes(file1_path));
    BOOST_CHECK(!mask.includes(file2_path));
    BOOST_CHECK(!mask.includes(file3_path));
  }

  {
    // invalid paths
    // non-empty restriction
    vector<utf8> args;
    args.push_back(utf8(string("file4")));

    BOOST_CHECK_THROW(restriction(args, roster), informative_failure);
  }

}

static void
test_recursive_nonrecursive()
{
  temp_node_id_source nis;
  roster_t roster;

  node_id root_nid = roster.create_dir_node(nis);

  node_id dir1_nid = roster.create_dir_node(nis);
  node_id dir2_nid = roster.create_dir_node(nis);

  node_id file1_nid = roster.create_file_node(file1_id, nis);
  node_id file2_nid = roster.create_file_node(file2_id, nis);
  node_id file3_nid = roster.create_file_node(file3_id, nis);

  // root/file1
  // root/dir1
  // root/dir1/file2
  // root/dir1/dir2
  // root/dir1/dir2/file3

  split_path root_path, file1_path, dir1_path, file2_path, dir2_path, file3_path;

  file_path().split(root_path);
  file_path_internal("file1").split(file1_path);
  file_path_internal("dir1").split(dir1_path);
  file_path_internal("dir1/file2").split(file2_path);
  file_path_internal("dir1/dir2").split(dir2_path);
  file_path_internal("dir1/dir2/file3").split(file3_path);

  roster.attach_node(root_nid, root_path);
  roster.attach_node(file1_nid, file1_path);
  roster.attach_node(dir1_nid, dir1_path);
  roster.attach_node(file2_nid, file2_path);
  roster.attach_node(dir2_nid, dir2_path);
  roster.attach_node(file3_nid, file3_path);

  vector<utf8> args;
  args.push_back(utf8(string("dir1/dir2")));

  restriction mask(args, roster);

  BOOST_CHECK(!mask.empty());

  // check restricted nodes
  BOOST_CHECK(mask.includes(roster, root_nid));
  BOOST_CHECK(!mask.includes(roster, file1_nid));
  BOOST_CHECK(mask.includes(roster, dir1_nid));
  BOOST_CHECK(!mask.includes(roster, file2_nid));
  BOOST_CHECK(mask.includes(roster, dir2_nid));
  BOOST_CHECK(mask.includes(roster, file3_nid));

  // check restricted paths
  BOOST_CHECK(mask.includes(root_path));
  BOOST_CHECK(!mask.includes(file1_path));
  BOOST_CHECK(mask.includes(dir1_path));
  BOOST_CHECK(!mask.includes(file2_path));
  BOOST_CHECK(mask.includes(dir2_path));
  BOOST_CHECK(mask.includes(file3_path));
}

void
add_restrictions_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_basic_restrictions));
  suite->add(BOOST_TEST_CASE(&test_recursive_nonrecursive));
}
#endif // BUILD_UNIT_TESTS
