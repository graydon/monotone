// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <set>

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
    map<file_path, file_id>::const_iterator a = cs.files_added.begin();
    map<file_path, pair<file_id, file_id> >::const_iterator
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
    set<pair<file_path, attr_key> >::const_iterator c = cs.attrs_cleared.begin();
    map<pair<file_path, attr_key>, attr_value>::const_iterator
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
  detach(file_path const & src)
    : src_path(src),
      reattach(false)
  {}

  detach(file_path const & src,
         file_path const & dst)
    : src_path(src),
      reattach(true),
      dst_path(dst)
  {}

  file_path src_path;
  bool reattach;
  file_path dst_path;

  bool operator<(struct detach const & other) const
  {
    // We sort detach operations bottom-up by src path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return other.src_path < src_path;
  }
};

struct
attach
{
  attach(node_id n,
         file_path const & p)
    : node(n), path(p)
  {}

  node_id node;
  file_path path;

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

  for (set<file_path>::const_iterator i = dirs_added.begin();
       i != dirs_added.end(); ++i)
    safe_insert(attaches, attach(t.create_dir_node(), *i));

  for (map<file_path, file_id>::const_iterator i = files_added.begin();
       i != files_added.end(); ++i)
    safe_insert(attaches, attach(t.create_file_node(i->second), i->first));


  // Decompose all path deletion and the first-half of renamings on
  // existing paths into the set of pending detaches, to be executed
  // bottom-up.

  for (set<file_path>::const_iterator i = nodes_deleted.begin();
       i != nodes_deleted.end(); ++i)
    safe_insert(detaches, detach(*i));

  for (map<file_path, file_path>::const_iterator i = nodes_renamed.begin();
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
  for (map<file_path, pair<file_id, file_id> >::const_iterator i = deltas_applied.begin();
       i != deltas_applied.end(); ++i)
    t.apply_delta(i->first, i->second.first, i->second.second);

  for (set<pair<file_path, attr_key> >::const_iterator i = attrs_cleared.begin();
       i != attrs_cleared.end(); ++i)
    t.clear_attr(i->first, i->second);

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = attrs_set.begin();
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
    symbol const delete_node("delete");
    symbol const rename_node("rename");
    symbol const content("content");
    symbol const add_file("add_file");
    symbol const add_dir("add_dir");
    symbol const patch("patch");
    symbol const from("from");
    symbol const to("to");
    symbol const clear("clear");
    symbol const set("set");
    symbol const attr("attr");
    symbol const value("value");
  }
}

void
print_cset(basic_io::printer & printer,
           cset const & cs)
{
  for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, *i);
      printer.print_stanza(st);
    }

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::rename_node, i->first);
      st.push_file_pair(syms::to, i->second);
      printer.print_stanza(st);
    }

  for (set<file_path>::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, *i);
      printer.print_stanza(st);
    }

  for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, i->first);
      st.push_binary_pair(syms::content, i->second.inner());
      printer.print_stanza(st);
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::patch, i->first);
      st.push_binary_pair(syms::from, i->second.first.inner());
      st.push_binary_pair(syms::to, i->second.second.inner());
      printer.print_stanza(st);
    }

  for (set<pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::clear, i->first);
      st.push_str_pair(syms::attr, i->second());
      printer.print_stanza(st);
    }

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, i->first.first);
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
}


static inline void
parse_path(basic_io::parser & parser, file_path & sp)
{
  string s;
  parser.str(s);
  sp = file_path_internal(s);
}

void
parse_cset(basic_io::parser & parser,
           cset & cs)
{
  cs.clear();
  string t1, t2;
  MM(t1);
  MM(t2);
  file_path p1, p2;
  MM(p1);
  MM(p2);

  file_path prev_path;
  MM(prev_path);
  pair<file_path, attr_key> prev_pair;
  MM(prev_pair.first);
  MM(prev_pair.second);

  // we make use of the fact that a valid file_path is never empty
  prev_path.clear();
  while (parser.symp(syms::delete_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      safe_insert(cs.nodes_deleted, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::rename_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
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
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      safe_insert(cs.dirs_added, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::add_file))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
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
      I(prev_path.empty() || prev_path < p1);
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
      pair<file_path, attr_key> new_pair(p1, attr_key(t1));
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
      pair<file_path, attr_key> new_pair(p1, attr_key(t1));
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
  basic_io::printer pr;
  print_cset(pr, cs);
  dat = data(pr.buf);
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
dump(cset const & cs, string & out)
{
  data dat;
  write_cset(cs, dat);
  out = dat();
}

#ifdef BUILD_UNIT_TESTS
#include "transforms.hh"
#include "unit_tests.hh"

#include "roster.hh"

using std::logic_error;

static void
setup_roster(roster_t & r, file_id const & fid, node_id_source & nis)
{
  // sets up r to have a root dir, a dir in it name "foo", and a file under
  // that named "bar", and the file has the given id.
  // the file has attr "attr_file=value_file", and the dir has
  // "attr_dir=value_dir".
  r = roster_t();

  {
    r.attach_node(r.create_dir_node(nis), file_path_internal(""));
  }
  {
    file_path fp = file_path_internal("foo");
    r.attach_node(r.create_dir_node(nis), fp);
    r.set_attr(fp, attr_key("attr_dir"), attr_value("value_dir"));
  }
  {
    file_path fp = file_path_internal("foo/bar");
    r.attach_node(r.create_file_node(fid, nis), fp);
    r.set_attr(fp, attr_key("attr_file"), attr_value("value_file"));
  }
}

UNIT_TEST(cset, cset_written)
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
    UNIT_TEST_CHECK_THROW(read_cset(d1, cs), logic_error);
    // check that it still fails if there's extra stanzas past the
    // mis-ordered entries
    data d2(s + "\n"
                "  set \"bar\"\n"
                " attr \"flavoursome\"\n"
                "value \"mostly\"\n");
    UNIT_TEST_CHECK_THROW(read_cset(d2, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in delete"));
    // bad cset, bar should be before foo
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"bar\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_dir"));
    // bad cset, bar should be before foo
    data dat("add_dir \"foo\"\n"
             "\n"
             "add_dir \"bar\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - duplicate entries"));
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"foo\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_NOT_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
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
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset writing - normalisation"));
    cset cs; MM(cs);
    file_id f1(decode_hexenc("1234567800000000000000000000000000000000"));
    file_id f2(decode_hexenc("9876543212394657263900000000000000000000"));
    file_id f3(decode_hexenc("0000000000011111111000000000000000000000"));

    file_path foo = file_path_internal("foo");
    file_path foo_quux = file_path_internal("foo/quux");
    file_path bar = file_path_internal("bar");
    file_path quux = file_path_internal("quux");
    file_path idle = file_path_internal("idle");
    file_path fish = file_path_internal("fish");
    file_path womble = file_path_internal("womble");
    file_path policeman = file_path_internal("policeman");

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
    UNIT_TEST_CHECK_NOT_THROW(I(expected == dat), logic_error);
  }
}

UNIT_TEST(cset, basic_csets)
{

  temp_node_id_source nis;
  roster_t r;
  MM(r);

  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc("0000000000000000000000000000000000000001"));
  file_id f2(decode_hexenc("0000000000000000000000000000000000000002"));

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path baz = file_path_internal("baz");
  file_path quux = file_path_internal("quux");

  // some basic tests that should succeed
  {
    L(FL("TEST: cset add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f2));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_file_t(r.get_node(baz)));
    UNIT_TEST_CHECK(downcast_to_file_t(r.get_node(baz))->content == f2);
    UNIT_TEST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset add dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(quux);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(quux)));
    UNIT_TEST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(r.all_nodes().size() == 1); // only the root left
  }

  {
    L(FL("TEST: cset rename file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, quux));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux)));
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo)));
    UNIT_TEST_CHECK(!r.has_node(foo_bar));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: cset rename dir"));
    file_path quux_bar = file_path_internal("quux/bar");
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo, quux));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(quux)));
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux_bar)));
    UNIT_TEST_CHECK(!r.has_node(foo));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: patch file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo_bar, make_pair(f1, f2)));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo)));
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_bar)));
    UNIT_TEST_CHECK(downcast_to_file_t(r.get_node(foo_bar))->content == f2);
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: set attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")),
                                  attr_value("klang")));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);

    full_attr_map_t attrs = (r.get_node(foo_bar))->attrs;
    UNIT_TEST_CHECK(attrs[attr_key("ping")] == make_pair(true, attr_value("klang")));

    attrs = (r.get_node(foo))->attrs;
    UNIT_TEST_CHECK(attrs[attr_key("attr_dir")] == make_pair(true, attr_value("value_dir")));

    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: clear attr file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")),
                                  attr_value("klang")));
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("attr_file")));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK((r.get_node(foo_bar))->attrs[attr_key("attr_file")]
                == make_pair(false, attr_value("")));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  // some renaming tests
  {
    L(FL("TEST: renaming at different levels"));
    setup_roster(r, f1, nis);

    file_path quux_bar = file_path_internal("quux/bar");
    file_path foo_bar = file_path_internal("foo/bar");
    file_path quux_sub = file_path_internal("quux/sub");
    file_path foo_sub = file_path_internal("foo/sub");
    file_path foo_sub_thing = file_path_internal("foo/sub/thing");
    file_path quux_sub_thing = file_path_internal("quux/sub/thing");
    file_path foo_sub_deep = file_path_internal("foo/sub/deep");
    file_path foo_subsub = file_path_internal("foo/subsub");
    file_path foo_subsub_deep = file_path_internal("foo/subsub/deep");

    { // build a tree
      cset cs; MM(cs);
      cs.dirs_added.insert(quux);
      cs.dirs_added.insert(quux_sub);
      cs.dirs_added.insert(foo_sub);
      cs.files_added.insert(make_pair(foo_sub_deep, f2));
      cs.files_added.insert(make_pair(quux_sub_thing, f1));
      UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
      UNIT_TEST_CHECK(r.all_nodes().size() == 8);
    }

    { // some renames
      cset cs; MM(cs);
      cs.nodes_renamed.insert(make_pair(foo, quux));
      cs.nodes_renamed.insert(make_pair(quux, foo));
      cs.nodes_renamed.insert(make_pair(foo_sub, foo_subsub));
      UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    }

    UNIT_TEST_CHECK(r.all_nodes().size() == 8);
    // /foo/bar -> /quux/bar
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux_bar)));
    UNIT_TEST_CHECK(!(r.has_node(foo_bar)));
    // /foo/sub/deep -> /foo/subsub/deep
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_subsub_deep)));
    UNIT_TEST_CHECK(!(r.has_node(foo_sub_deep)));
    // /quux/sub -> /foo/sub
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo_sub)));
    UNIT_TEST_CHECK(!(r.has_node(quux_sub)));
    // /quux/sub/thing -> /foo/sub/thing
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_sub_thing)));
  }

  {
    L(FL("delete targets pre-renamed nodes"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, foo));
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(r.all_nodes().size() == 2);
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo)));
  }
}

UNIT_TEST(cset, invalid_csets)
{
  temp_node_id_source nis;
  roster_t r;
  MM(r);
  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc("0000000000000000000000000000000000000001"));
  file_id f2(decode_hexenc("0000000000000000000000000000000000000002"));

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path baz = file_path_internal("baz");
  file_path quux = file_path_internal("quux");

  {
    L(FL("TEST: can't double-delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't double-add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f2));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add file on top of dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(foo, f2));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delete+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_renamed.insert(make_pair(foo_bar, baz));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(baz);
    cs.nodes_renamed.insert(make_pair(baz, quux));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add on top of root dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(root);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename on top of root dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo, root));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'a'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, foo_bar));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'b'; rename 'a/foo' 'b/foo'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path baz_bar = file_path_internal("baz/bar");
    cs.nodes_renamed.insert(make_pair(foo, baz));
    cs.nodes_renamed.insert(make_pair(foo_bar, baz_bar));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't attr_set + attr_cleared"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("blah")),
                                       attr_value("blahblah")));
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("blah")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't no-op attr_set"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("attr_file")),
                                       attr_value("value_file")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("blah")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr that once existed"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("attr_file")));
    // exists now, so should be fine
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    // but last time killed it, so can't be killed again
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't have no-op deltas"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo_bar,
                                            make_pair(f1, f1)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't have add+delta"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f1));
    cs.deltas_applied.insert(make_pair(baz,
                                            make_pair(f1, f2)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delta a directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo,
                                            make_pair(f1, f2)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delete non-empty directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: attach node with no root directory present"));
    // for this test, make sure original roster has no contents
    r = roster_t();
    cset cs; MM(cs);
    file_path sp = file_path_internal("blah/blah/blah");
    cs.dirs_added.insert(sp);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't move a directory underneath itself"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path foo_blah = file_path_internal("foo/blah");
    cs.nodes_renamed.insert(make_pair(foo, foo_blah));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
}

UNIT_TEST(cset, root_dir)
{
  temp_node_id_source nis;
  roster_t r;
  MM(r);
  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc("0000000000000000000000000000000000000001"));

  file_path root, baz = file_path_internal("baz");

  {
    L(FL("TEST: can rename root"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path sp1, sp2;
    cs.dirs_added.insert(root);
    cs.nodes_renamed.insert(make_pair(root, baz));
    cs.apply_to(tree);
    r.check_sane(true);
  }
  {
    L(FL("TEST: can delete root (but it makes us insane)"));
    // for this test, make sure root has no contents
    r = roster_t();
    r.attach_node(r.create_dir_node(nis), root);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.apply_to(tree);
    UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
  }
  {
    L(FL("TEST: can delete and replace root"));
    r = roster_t();
    r.attach_node(r.create_dir_node(nis), root);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.dirs_added.insert(root);
    cs.apply_to(tree);
    r.check_sane(true);
  }
}

#endif // BUILD_UNIT_TESTS



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
