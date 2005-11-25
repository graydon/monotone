// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>
#include <string>

#include "basic_io.hh"
#include "cset.hh"
#include "sanity.hh"
#include "safe_map.hh"

using std::set;
using std::map;
using std::pair;
using std::string;
using std::make_pair;

static void
check_normalized(cset const & cs)
{
  MM(cs);

  // FIXME -- normalize:
  //
  //   add_file foo@id1 + apply_delta id1->id2
  //   clear_attr foo:bar + set_attr foo:bar=baz
  //   rename foo -> foo
  //
  // possibly more?

  // no file appears in both the "added" list and the "patched" list
  {
    map<split_path, file_id>::const_iterator a = cs.files_added.begin();
    map<split_path, std::pair<file_id, file_id> >::const_iterator
      d = cs.deltas_applied.begin();
    while (a != cs.files_added.end() && d != cs.deltas_applied.end())
      {
        // SPEEDUP?: should this use lower_bound instead of ++?  it should
        // make this loop iterate about as many times as the shorter list,
        // rather the sum of the two lists...
        if (a->first < d->first)
          ++a;
        else if (d->first < a->first)
          ++d;
        else
          I(false);
      }
  }
  
  // no file+attr pair appears in both the "set" list and the "cleared" list
  {
    set<pair<split_path, attr_key> >::const_iterator c = cs.attrs_cleared.begin();
    map<pair<split_path, attr_key>, attr_value>::const_iterator
      s = cs.attrs_set.begin();
    while (c != cs.attrs_cleared.end() && s != cs.attrs_set.end())
      {
        if (*c < s->first)
          ++c;
        else if (s->first < *c)
          ++s;
        else
          I(false);
      }
  }
}

bool
cset::empty() const
{
  return 
    nodes_deleted.empty() 
    && dirs_added.empty()
    && files_added.empty()
    && nodes_renamed.empty()
    && deltas_applied.empty()
    && attrs_cleared.empty()
    && attrs_set.empty();
}

void
cset::clear()
{
  nodes_deleted.clear();
  dirs_added.clear();
  files_added.clear();
  nodes_renamed.clear();
  deltas_applied.clear();
  attrs_cleared.clear();
  attrs_set.clear();
}

struct
detach
{
  detach(split_path const & src) 
    : src_path(src), 
      reattach(false) 
  {}
  
  detach(split_path const & src, 
         split_path const & dst) 
    : src_path(src), 
      reattach(true), 
      dst_path(dst) 
  {}
  
  split_path src_path;
  bool reattach;
  split_path dst_path;

  bool operator<(struct detach const & other) const
  {
    // We sort detach operations bottom-up by src path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return src_path > other.src_path;
  }
};

struct
attach
{
  attach(node_id n, 
         split_path const & p) 
    : node(n), path(p)
  {}

  node_id node;
  split_path path;

  bool operator<(struct attach const & other) const
  {
    // We sort attach operations top-down by path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return path < other.path;
  }
};

void 
cset::apply_to(editable_tree & t) const
{
  // SPEEDUP?: use vectors and sort them once, instead of maintaining sorted
  // sets?
  set<detach> detaches;
  set<attach> attaches;
  set<node_id> drops;

  MM(*this);

  check_normalized(*this);

  // Decompose all additions into a set of pending attachments to be
  // executed top-down. We might as well do this first, to be sure we
  // can form the new nodes -- such as in a filesystem -- before we do
  // anything else potentially destructive. This should all be
  // happening in a temp directory anyways.

  // NB: it's very important we do safe_insert's here, because our comparison
  // operator for attach and detach does not distinguish all nodes!  the nodes
  // that it does not distinguish are ones where we're attaching or detaching
  // repeatedly from the same place, so they're impossible anyway, but we need
  // to error out if someone tries to add them.

  for (path_set::const_iterator i = dirs_added.begin();
       i != dirs_added.end(); ++i)
    safe_insert(attaches, attach(t.create_dir_node(), *i));

  for (map<split_path, file_id>::const_iterator i = files_added.begin();
       i != files_added.end(); ++i)
    safe_insert(attaches, attach(t.create_file_node(i->second), i->first));


  // Decompose all path deletion and the first-half of renamings on
  // existing paths into the set of pending detaches, to be executed
  // bottom-up.

  for (path_set::const_iterator i = nodes_deleted.begin();
       i != nodes_deleted.end(); ++i)    
    safe_insert(detaches, detach(*i));
  
  for (map<split_path, split_path>::const_iterator i = nodes_renamed.begin();
       i != nodes_renamed.end(); ++i)
    safe_insert(detaches, detach(i->first, i->second));


  // Execute all the detaches, rescheduling the results of each detach
  // for either attaching or dropping.

  for (set<detach>::const_iterator i = detaches.begin(); 
       i != detaches.end(); ++i)
    {
      node_id n = t.detach_node(i->src_path);
      if (i->reattach)
        safe_insert(attaches, attach(n, i->dst_path));
      else
        safe_insert(drops, n);
    }


  // Execute all the attaches.

  for (set<attach>::const_iterator i = attaches.begin(); i != attaches.end(); ++i)
    t.attach_node(i->node, i->path);


  // Execute all the drops.

  for (set<node_id>::const_iterator i = drops.begin(); i != drops.end(); ++i)
    t.drop_detached_node (*i);


  // Execute all the in-place edits
  for (map<split_path, pair<file_id, file_id> >::const_iterator i = deltas_applied.begin();
       i != deltas_applied.end(); ++i)
    t.apply_delta(i->first, i->second.first, i->second.second);

  for (set<pair<split_path, attr_key> >::const_iterator i = attrs_cleared.begin();
       i != attrs_cleared.end(); ++i)
    t.clear_attr(i->first, i->second);

  for (map<pair<split_path, attr_key>, attr_value>::const_iterator i = attrs_set.begin();
       i != attrs_set.end(); ++i)
    t.set_attr(i->first.first, i->first.second, i->second);
}

////////////////////////////////////////////////////////////////////
//   I/O routines
////////////////////////////////////////////////////////////////////

namespace
{
  namespace syms
  {
    // cset symbols
    string const delete_node("delete");
    string const rename_node("rename");
    string const content("content");
    string const add_file("add_file");
    string const add_dir("add_dir");
    string const patch("patch");
    string const from("from");
    string const to("to");
    string const clear("clear");
    string const set("set");
    string const attr("attr");
    string const value("value");
  }
}

void 
print_cset(basic_io::printer & printer,
           cset const & cs)
{
  for (path_set::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, file_path(*i));
      printer.print_stanza(st);
    }

  for (map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::rename_node, file_path(i->first));
      st.push_file_pair(syms::to, file_path(i->second));
      printer.print_stanza(st);
    }

  for (path_set::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, file_path(*i));
      printer.print_stanza(st);
    }

  for (map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, file_path(i->first));
      st.push_hex_pair(syms::content, i->second.inner()());
      printer.print_stanza(st);
    }

  for (map<split_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::patch, file_path(i->first));
      st.push_hex_pair(syms::from, i->second.first.inner()());
      st.push_hex_pair(syms::to, i->second.second.inner()());
      printer.print_stanza(st);
    }

  for (set<pair<split_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::clear, file_path(i->first));
      st.push_str_pair(syms::attr, i->second());
      printer.print_stanza(st);
    }

  for (map<pair<split_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, file_path(i->first.first));
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
}


void 
parse_cset(basic_io::parser & parser,
           cset & cs)
{
  cs.clear();
  while (parser.symp())
    {
      string t1, t2, t3;
      if (parser.symp(syms::delete_node))
        {
          parser.sym();
          parser.str(t1);
          safe_insert(cs.nodes_deleted, internal_string_to_split_path(t1));
        }
      else if (parser.symp(syms::rename_node))
        {
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          safe_insert(cs.nodes_renamed, make_pair(internal_string_to_split_path(t1),
                                                  internal_string_to_split_path(t2)));
        }
      else if (parser.symp(syms::add_dir))
        {
          parser.sym();
          parser.str(t1);
          safe_insert(cs.dirs_added, internal_string_to_split_path(t1));
        }
      else if (parser.symp(syms::add_file))
        {
          parser.sym();
          parser.str(t1);
          parser.esym(syms::content);
          parser.hex(t2);
          safe_insert(cs.files_added, make_pair(internal_string_to_split_path(t1),
                                                file_id(t2)));
        }
      else if (parser.symp(syms::patch))
        {
          parser.sym();
          parser.str(t1);
          parser.esym(syms::from);
          parser.hex(t2);
          parser.esym(syms::to);
          parser.hex(t3);
          safe_insert(cs.deltas_applied, 
                      make_pair(internal_string_to_split_path(t1),
                                make_pair(file_id(t2), 
                                          file_id(t3))));
        }
      else if (parser.symp(syms::clear))
        {
          parser.sym();
          parser.str(t1);
          parser.esym(syms::attr);
          parser.str(t2);
          safe_insert(cs.attrs_cleared, 
                      make_pair(internal_string_to_split_path(t1), 
                                attr_key(t2)));
        }
      else if (parser.symp(syms::set))
        {
          parser.sym();
          parser.str(t1);
          parser.esym(syms::attr);
          parser.str(t2);
          parser.esym(syms::value);
          parser.str(t3);
          safe_insert(cs.attrs_set, 
                      make_pair(make_pair(internal_string_to_split_path(t1),
                                          attr_key(t2)),
                                attr_value(t3)));
        }
      else
        break;
    }
}


void
write_cset(cset const & cs, data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_cset(pr, cs);
  dat = data(oss.str());
}

void
read_cset(data const & dat, cset & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "cset");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_cset(pars, cs);
  I(src.lookahead == EOF);
}

void
dump(cset const & cs, std::string & out)
{
  data dat;
  write_cset(cs, dat);
  out = dat();
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

#include "roster.hh"

static void
basic_csets_test()
{
  // FIXME_ROSTERS: write some tests here
  // some things to test:
  //   each cset thingie sets what it's supposed to
  //   the topdown/bottomup stuff works
  // don't forget to check normalization of written form, either...
  //   no duplicate entries (as would be silently ignored, if we don't use
  //     safe_insert in the parser!)
  //   ordering
  //   whitespace normalization
}

static void
setup_roster(roster_t & r, file_id const & fid, node_id_source & nis)
{
  // sets up r to have a root dir, a dir in it name "dir", and a file under
  // that named "file", and the file has the given id.
  // the file has attr "attr_file=value_file", and the dir has
  // "attr_dir=value_dir".
  r = roster_t();
  
  {
    split_path sp;
    file_path().split(sp);
    r.attach_node(r.create_dir_node(nis), sp);
  }
  {
    split_path sp;
    file_path_internal("foo").split(sp);
    r.attach_node(r.create_dir_node(nis), sp);
    r.set_attr(sp, attr_key("attr_dir"), attr_value("value_dir"));
  }
  {
    split_path sp;
    file_path_internal("foo/bar").split(sp);
    r.attach_node(r.create_file_node(fid, nis), sp);
    r.set_attr(sp, attr_key("attr_file"), attr_value("value_file"));
  }
}

static void
invalid_csets_test()
{
  temp_node_id_source nis;
  roster_t r;
  MM(r);
  editable_roster_base tree(r, nis);
  
  file_id f1(std::string("0000000000000000000000000000000000000001"));
  file_id f2(std::string("0000000000000000000000000000000000000002"));

  split_path root, foo, foo_bar, baz, quux;
  file_path().split(root);
  file_path_internal("foo").split(foo);
  file_path_internal("foo/bar").split(foo_bar);
  file_path_internal("baz").split(baz);
  file_path_internal("quux").split(quux);

  {
    L(F("TEST: can't double-delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't double-add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(baz, f2));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't add file on top of dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(foo, f2));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't delete+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_renamed.insert(std::make_pair(foo_bar, baz));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't add+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(baz);
    cs.nodes_renamed.insert(std::make_pair(baz, quux));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't rename 'a' 'a'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(std::make_pair(foo_bar, foo_bar));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't rename 'a' 'b'; rename 'a/foo' 'b/foo'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    split_path baz_bar;
    file_path_internal("baz/bar").split(baz_bar);
    cs.nodes_renamed.insert(std::make_pair(foo, baz));
    cs.nodes_renamed.insert(std::make_pair(foo_bar, baz_bar));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't attr_set + attr_cleared"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(std::make_pair(std::make_pair(foo_bar, attr_key("blah")),
                                       attr_value("blahblah")));
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("blah")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't no-op attr_set"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(std::make_pair(std::make_pair(foo_bar, attr_key("attr_file")),
                                       attr_value("value_file")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't clear non-existent attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("blah")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't clear non-existent attr that once existed"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("attr_file")));
    // exists now, so should be fine
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    // but last time killed it, so can't be killed again
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't have no-op deltas"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(std::make_pair(foo_bar,
                                            std::make_pair(f1, f1)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't have add+delta"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(baz, f1));
    cs.deltas_applied.insert(std::make_pair(baz,
                                            std::make_pair(f1, f2)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't delta a directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(std::make_pair(foo,
                                            std::make_pair(f1, f2)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't rename root (for now)"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    split_path sp1, sp2;
    cs.dirs_added.insert(root);
    cs.nodes_renamed.insert(std::make_pair(root, baz));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't delete non-empty directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't delete root"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't delete and replace root"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.dirs_added.insert(root);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: attach node with no root directory present"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    split_path sp;
    file_path_internal("blah/blah/blah").split(sp);
    cs.dirs_added.insert(sp);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(F("TEST: can't move a directory underneath itself"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    split_path foo_blah;
    file_path_internal("foo/blah").split(foo_blah);
    cs.nodes_renamed.insert(std::make_pair(foo, foo_blah));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
}

void
add_cset_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&basic_csets_test));
  suite->add(BOOST_TEST_CASE(&invalid_csets_test));
}

#endif // BUILD_UNIT_TESTS


