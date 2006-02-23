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

  // normalize:
  //
  //   add_file foo@id1 + apply_delta id1->id2
  //   clear_attr foo:bar + set_attr foo:bar=baz
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

  t.commit();
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
      file_path p(*i);
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, file_path(*i));
      printer.print_stanza(st);
    }

  for (map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      file_path p(i->first);
      basic_io::stanza st;
      st.push_file_pair(syms::rename_node, file_path(i->first));
      st.push_file_pair(syms::to, file_path(i->second));
      printer.print_stanza(st);
    }

  for (path_set::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      file_path p(*i);
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, file_path(*i));
      printer.print_stanza(st);
    }

  for (map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      file_path p(i->first);
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, file_path(i->first));
      st.push_hex_pair(syms::content, i->second.inner()());
      printer.print_stanza(st);
    }

  for (map<split_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      file_path p(i->first);
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


static inline void
parse_path(basic_io::parser & parser, split_path & sp)
{
  std::string s;
  parser.str(s);
  file_path_internal(s).split(sp);
}

void 
parse_cset(basic_io::parser & parser,
           cset & cs)
{
  cs.clear();
  string t1, t2;
  MM(t1);
  MM(t2);
  split_path p1, p2;
  MM(p1);
  MM(p2);
  
  split_path prev_path;
  MM(prev_path);
  pair<split_path, attr_key> prev_pair;
  MM(prev_pair.first);
  MM(prev_pair.second);
  
  // we make use of the fact that a valid split_path is never empty
  prev_path.clear();
  while (parser.symp(syms::delete_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || p1 > prev_path);
      prev_path = p1;
      safe_insert(cs.nodes_deleted, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::rename_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || p1 > prev_path);
      prev_path = p1;
      parser.esym(syms::to);
      parse_path(parser, p2);
      safe_insert(cs.nodes_renamed, make_pair(p1, p2));
    }

  prev_path.clear();
  while (parser.symp(syms::add_dir))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || p1 > prev_path);
      prev_path = p1;
      safe_insert(cs.dirs_added, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::add_file))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || p1 > prev_path);
      prev_path = p1;
      parser.esym(syms::content);
      parser.hex(t1);
      safe_insert(cs.files_added, make_pair(p1, file_id(t1)));
    }

  prev_path.clear();
  while (parser.symp(syms::patch))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || p1 > prev_path);
      prev_path = p1;
      parser.esym(syms::from);
      parser.hex(t1);
      parser.esym(syms::to);
      parser.hex(t2);
      safe_insert(cs.deltas_applied, 
                  make_pair(p1, make_pair(file_id(t1), file_id(t2))));
    }

  prev_pair.first.clear(); 
  while (parser.symp(syms::clear))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<split_path, attr_key> new_pair(p1, t1);
      I(prev_pair.first.empty() || new_pair > prev_pair);
      prev_pair = new_pair;
      safe_insert(cs.attrs_cleared, new_pair);
    }

  prev_pair.first.clear(); 
  while (parser.symp(syms::set))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<split_path, attr_key> new_pair(p1, t1);
      I(prev_pair.first.empty() || new_pair > prev_pair);
      prev_pair = new_pair;
      parser.esym(syms::value);
      parser.str(t2);
      safe_insert(cs.attrs_set, make_pair(new_pair, attr_value(t2)));
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
  MM(dat);
  MM(cs);
  basic_io::input_source src(dat(), "cset");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_cset(pars, cs);
  I(src.lookahead == EOF);
}

template <> void
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
setup_roster(roster_t & r, file_id const & fid, node_id_source & nis)
{
  // sets up r to have a root dir, a dir in it name "foo", and a file under
  // that named "bar", and the file has the given id.
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
cset_written_test()
{
  { 
    L(FL("TEST: cset reading - operation misordering"));
    // bad cset, add_dir should be before add_file
    string s("delete \"foo\"\n"
             "\n"
             "rename \"quux\"\n"
             "    to \"baz\"\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_dir \"pling\"\n");
    data d1(s);
    cset cs;
    BOOST_CHECK_THROW(read_cset(d1, cs), std::logic_error);
    // check that it still fails if there's extra stanzas past the
    // mis-ordered entries
    data d2(s + "\n"
                "  set \"bar\"\n"
                " attr \"flavoursome\"\n"
                "value \"mostly\"\n");
    BOOST_CHECK_THROW(read_cset(d2, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in delete"));
    // bad cset, bar should be before foo
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"bar\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in rename"));
    // bad cset, bar should be before foo
    data dat("rename \"foo\"\n"
             "    to \"foonew\"\n"
             "\n"
             "rename \"bar\"\n"
             "    to \"barnew\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_dir"));
    // bad cset, bar should be before foo
    data dat("add_dir \"foo\"\n"
             "\n"
             "add_dir \"bar\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_file"));
    // bad cset, bar should be before foo
    data dat("add_file \"foo\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_file"));
    // bad cset, bar should be before foo
    data dat("add_file \"foo\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in patch"));
    // bad cset, bar should be before foo
    data dat("patch \"foo\"\n"
             " from [0000000000000000000000000000000000000000]\n"
             "   to [1000000000000000000000000000000000000000]\n"
             "\n"
             "patch \"bar\"\n"
             " from [0000000000000000000000000000000000000000]\n"
             "   to [1000000000000000000000000000000000000000]\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in clear"));
    // bad cset, bar should be before foo
    data dat("clear \"foo\"\n"
             " attr \"flavoursome\"\n"
             "\n"
             "clear \"bar\"\n"
             " attr \"flavoursome\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in set"));
    // bad cset, bar should be before foo
    data dat("  set \"foo\"\n"
             " attr \"flavoursome\"\n"
             "value \"yes\"\n"
             "\n"
             "  set \"bar\"\n"
             " attr \"flavoursome\"\n"
             "value \"yes\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - duplicate entries"));
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"foo\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - multiple different attrs"));
    // should succeed
    data dat( "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"smell\"\n"
              "value \"socks\"\n");
    cset cs;
    BOOST_CHECK_NOT_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - wrong attr ordering in clear"));
    // fooish should be before quuxy
    data dat( "clear \"bar\"\n"
              " attr \"quuxy\"\n"
              "\n"
              "clear \"bar\"\n"
              " attr \"fooish\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - wrong attr ordering in set"));
    // fooish should be before quuxy
    data dat( "  set \"bar\"\n"
              " attr \"quuxy\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"fooish\"\n"
              "value \"seldom\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset reading - duplicate attrs"));
    // can't have dups.
    data dat( "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"sometimes\"\n");
    cset cs;
    BOOST_CHECK_THROW(read_cset(dat, cs), std::logic_error);
  }

  {
    L(FL("TEST: cset writing - normalisation"));
    cset cs; MM(cs);
    split_path foo, bar, quux, foo_quux, idle, fish, womble, policeman;
    file_id f1(std::string("1234567800000000000000000000000000000000"));
    file_id f2(std::string("9876543212394657263900000000000000000000"));
    file_id f3(std::string("0000000000011111111000000000000000000000"));
    file_path_internal("foo").split(foo);
    file_path_internal("foo/quux").split(foo_quux);
    file_path_internal("bar").split(bar);
    file_path_internal("quux").split(quux);
    file_path_internal("idle").split(idle);
    file_path_internal("fish").split(fish);
    file_path_internal("womble").split(womble);
    file_path_internal("policeman").split(policeman);

    cs.dirs_added.insert(foo_quux);
    cs.dirs_added.insert(foo);
    cs.files_added.insert(make_pair(bar, f1));
    cs.nodes_deleted.insert(quux);
    cs.nodes_deleted.insert(idle);
    cs.nodes_renamed.insert(make_pair(fish, womble));
    cs.deltas_applied.insert(make_pair(womble, make_pair(f2, f3)));
    cs.attrs_cleared.insert(make_pair(policeman, attr_key("yodel")));
    cs.attrs_set.insert(make_pair(make_pair(policeman, 
                        attr_key("axolotyl")), attr_value("fruitily")));
    cs.attrs_set.insert(make_pair(make_pair(policeman, 
                        attr_key("spin")), attr_value("capybara")));

    data dat; MM(dat);
    write_cset(cs, dat);
    data expected("delete \"idle\"\n"
                  "\n"
                  "delete \"quux\"\n"
                  "\n"
                  "rename \"fish\"\n"
                  "    to \"womble\"\n"
                  "\n"
                  "add_dir \"foo\"\n"
                  "\n"
                  "add_dir \"foo/quux\"\n"
                  "\n"
                  "add_file \"bar\"\n"
                  " content [1234567800000000000000000000000000000000]\n"
                  "\n"
                  "patch \"womble\"\n"
                  " from [9876543212394657263900000000000000000000]\n"
                  "   to [0000000000011111111000000000000000000000]\n"
                  "\n"
                  "clear \"policeman\"\n"
                  " attr \"yodel\"\n"
                  "\n"
                  "  set \"policeman\"\n"
                  " attr \"axolotyl\"\n"
                  "value \"fruitily\"\n"
                  "\n"
                  "  set \"policeman\"\n"
                  " attr \"spin\"\n"
                  "value \"capybara\"\n"
                 );
    MM(expected);
    // I() so that it'll dump on failure
    BOOST_CHECK_NOT_THROW(I(expected == dat), std::logic_error);
  }
}

static void
basic_csets_test()
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

  // some basic tests that should succeed
  {
    L(FL("TEST: cset add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f2));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(is_file_t(r.get_node(baz)));
    BOOST_CHECK(downcast_to_file_t(r.get_node(baz))->content == f2);
    BOOST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset add dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(quux);
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(is_dir_t(r.get_node(quux)));
    BOOST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_deleted.insert(foo);
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(r.all_nodes().size() == 1); // only the root left
  }

  {
    L(FL("TEST: cset rename file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, quux));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(is_file_t(r.get_node(quux)));
    BOOST_CHECK(is_dir_t(r.get_node(foo)));
    BOOST_CHECK(!r.has_node(foo_bar));
    BOOST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: cset rename dir"));
    split_path quux_bar;
    file_path_internal("quux/bar").split(quux_bar);
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo, quux));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(is_dir_t(r.get_node(quux)));
    BOOST_CHECK(is_file_t(r.get_node(quux_bar)));
    BOOST_CHECK(!r.has_node(foo));
    BOOST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: patch file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo_bar, make_pair(f1, f2)));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(is_dir_t(r.get_node(foo)));
    BOOST_CHECK(is_file_t(r.get_node(foo_bar)));
    BOOST_CHECK(downcast_to_file_t(r.get_node(foo_bar))->content == f2);
    BOOST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: set attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")), 
                                  attr_value("klang")));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);

    full_attr_map_t attrs = (r.get_node(foo_bar))->attrs;
    BOOST_CHECK(attrs[attr_key("ping")] == make_pair(true, attr_value("klang")));

    attrs = (r.get_node(foo))->attrs;
    BOOST_CHECK(attrs[attr_key("attr_dir")] == make_pair(true, attr_value("value_dir")));

    BOOST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: clear attr file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")), 
                                  attr_value("klang")));
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("attr_file")));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK((r.get_node(foo_bar))->attrs[attr_key("attr_file")] 
                == make_pair(false, attr_value("")));
    BOOST_CHECK(r.all_nodes().size() == 3);
  }

  // some renaming tests
  {
    L(FL("TEST: renaming at different levels"));
    setup_roster(r, f1, nis);
    split_path quux_sub, foo_sub, foo_sub_deep, foo_subsub, 
               foo_subsub_deep, quux_bar, foo_bar,
               quux_sub_thing, foo_sub_thing;
    file_path_internal("quux/bar").split(quux_bar);
    file_path_internal("foo/bar").split(foo_bar);
    file_path_internal("quux/sub").split(quux_sub);
    file_path_internal("foo/sub").split(foo_sub);
    file_path_internal("foo/sub/thing").split(foo_sub_thing);
    file_path_internal("quux/sub/thing").split(quux_sub_thing);
    file_path_internal("foo/sub/deep").split(foo_sub_deep);
    file_path_internal("foo/subsub").split(foo_subsub);
    file_path_internal("foo/subsub/deep").split(foo_subsub_deep);

    { // build a tree
      cset cs; MM(cs);
      cs.dirs_added.insert(quux);
      cs.dirs_added.insert(quux_sub);
      cs.dirs_added.insert(foo_sub);
      cs.files_added.insert(make_pair(foo_sub_deep, f2));
      cs.files_added.insert(make_pair(quux_sub_thing, f1));
      BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
      BOOST_CHECK(r.all_nodes().size() == 8);
    }

    { // some renames
      cset cs; MM(cs);
      cs.nodes_renamed.insert(make_pair(foo, quux));
      cs.nodes_renamed.insert(make_pair(quux, foo));
      cs.nodes_renamed.insert(make_pair(foo_sub, foo_subsub));
      BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    }

    BOOST_CHECK(r.all_nodes().size() == 8);
    // /foo/bar -> /quux/bar
    BOOST_CHECK(is_file_t(r.get_node(quux_bar)));
    BOOST_CHECK(!(r.has_node(foo_bar)));
    // /foo/sub/deep -> /foo/subsub/deep
    BOOST_CHECK(is_file_t(r.get_node(foo_subsub_deep)));
    BOOST_CHECK(!(r.has_node(foo_sub_deep)));
    // /quux/sub -> /foo/sub
    BOOST_CHECK(is_dir_t(r.get_node(foo_sub)));
    BOOST_CHECK(!(r.has_node(quux_sub)));
    // /quux/sub/thing -> /foo/sub/thing
    BOOST_CHECK(is_file_t(r.get_node(foo_sub_thing)));
  }

  {
    L(FL("delete targets pre-renamed nodes"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, foo));
    cs.nodes_deleted.insert(foo);
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK(r.all_nodes().size() == 2);
    BOOST_CHECK(is_file_t(r.get_node(foo)));
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
    L(FL("TEST: can't double-delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't double-add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(baz, f2));
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't add file on top of dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(foo, f2));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't delete+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_renamed.insert(std::make_pair(foo_bar, baz));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't add+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(baz);
    cs.nodes_renamed.insert(std::make_pair(baz, quux));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'a'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(std::make_pair(foo_bar, foo_bar));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'b'; rename 'a/foo' 'b/foo'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    split_path baz_bar;
    file_path_internal("baz/bar").split(baz_bar);
    cs.nodes_renamed.insert(std::make_pair(foo, baz));
    cs.nodes_renamed.insert(std::make_pair(foo_bar, baz_bar));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't attr_set + attr_cleared"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(std::make_pair(std::make_pair(foo_bar, attr_key("blah")),
                                       attr_value("blahblah")));
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("blah")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't no-op attr_set"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(std::make_pair(std::make_pair(foo_bar, attr_key("attr_file")),
                                       attr_value("value_file")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("blah")));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr that once existed"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(std::make_pair(foo_bar, attr_key("attr_file")));
    // exists now, so should be fine
    BOOST_CHECK_NOT_THROW(cs.apply_to(tree), std::logic_error);
    // but last time killed it, so can't be killed again
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't have no-op deltas"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(std::make_pair(foo_bar,
                                            std::make_pair(f1, f1)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't have add+delta"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(std::make_pair(baz, f1));
    cs.deltas_applied.insert(std::make_pair(baz,
                                            std::make_pair(f1, f2)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't delta a directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(std::make_pair(foo,
                                            std::make_pair(f1, f2)));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't rename root (for now)"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    split_path sp1, sp2;
    cs.dirs_added.insert(root);
    cs.nodes_renamed.insert(std::make_pair(root, baz));
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't delete non-empty directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't delete root"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't delete and replace root"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.dirs_added.insert(root);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: attach node with no root directory present"));
    // for this test, make sure root has no contents
    r = roster_t();
    cset cs; MM(cs);
    split_path sp;
    file_path_internal("blah/blah/blah").split(sp);
    cs.dirs_added.insert(sp);
    BOOST_CHECK_THROW(cs.apply_to(tree), std::logic_error);
  }
  {
    L(FL("TEST: can't move a directory underneath itself"));
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
  suite->add(BOOST_TEST_CASE(&cset_written_test));
}

#endif // BUILD_UNIT_TESTS


