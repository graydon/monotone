// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains "diff"/"patch" code that operates directly on rosters
// (with their associated markings).

#include "base.hh"
#include <set>
#include <map>

#include "lexical_cast.hh"

#include "safe_map.hh"
#include "parallel_iter.hh"
#include "roster_delta.hh"
#include "basic_io.hh"
#include "paths.hh"
#include "transforms.hh"

using boost::lexical_cast;
using std::pair;
using std::make_pair;

namespace 
{

  struct roster_delta_t
  {
    typedef std::set<node_id> nodes_deleted_t;
    typedef std::map<pair<node_id, path_component>,
                     node_id> dirs_added_t;
    typedef std::map<pair<node_id, path_component>,
                     pair<node_id, file_id> > files_added_t;
    typedef std::map<node_id,
                     pair<node_id, path_component> > nodes_renamed_t;
    typedef std::map<node_id, file_id> deltas_applied_t;
    typedef std::set<pair<node_id, attr_key> > attrs_cleared_t;
    typedef std::set<pair<node_id,
                          pair<attr_key,
                               pair<bool, attr_value> > > > attrs_changed_t;
    typedef std::map<node_id, marking_t> markings_changed_t;

    nodes_deleted_t nodes_deleted;
    dirs_added_t dirs_added;
    files_added_t files_added;
    nodes_renamed_t nodes_renamed;
    deltas_applied_t deltas_applied;
    attrs_cleared_t attrs_cleared;
    attrs_changed_t attrs_changed;

    // nodes_deleted are automatically removed from the marking_map; these are
    // all markings that are new or changed
    markings_changed_t markings_changed;

    void
    apply(roster_t & roster, marking_map & markings) const;
  };

  void
  roster_delta_t::apply(roster_t & roster, marking_map & markings) const
  {
    // Detach everything that should be detached.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.detach_node(*i);
    for (nodes_renamed_t::const_iterator
           i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.detach_node(i->first);

    // Delete the delete-able things.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.drop_detached_node(*i);

    // Add the new things.
    for (dirs_added_t::const_iterator
           i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.create_dir_node(i->second);
    for (files_added_t::const_iterator
           i = files_added.begin(); i != files_added.end(); ++i)
      roster.create_file_node(i->second.second, i->second.first);

    // Attach everything.
    for (dirs_added_t::const_iterator
           i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.attach_node(i->second, i->first.first, i->first.second);
    for (files_added_t::const_iterator
           i = files_added.begin(); i != files_added.end(); ++i)
      roster.attach_node(i->second.first, i->first.first, i->first.second);
    for (nodes_renamed_t::const_iterator
           i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.attach_node(i->first, i->second.first, i->second.second);

    // Okay, all the tricky tree-rearranging is done, just have to do some
    // individual node edits now.
    for (deltas_applied_t::const_iterator
           i = deltas_applied.begin(); i != deltas_applied.end(); ++i)
      roster.set_content(i->first, i->second);

    for (attrs_cleared_t::const_iterator
           i = attrs_cleared.begin(); i != attrs_cleared.end(); ++i)
      roster.erase_attr(i->first, i->second);

    for (attrs_changed_t::const_iterator
           i = attrs_changed.begin(); i != attrs_changed.end(); ++i)
      roster.set_attr_unknown_to_dead_ok(i->first, i->second.first, i->second.second);

    // And finally, update the marking map.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      safe_erase(markings, *i);
    for (markings_changed_t::const_iterator
           i = markings_changed.begin(); i != markings_changed.end(); ++i)
      markings[i->first] = i->second;
  }

  void
  do_delta_for_node_only_in_dest(node_t new_n, roster_delta_t & d)
  {
    node_id nid = new_n->self;
    pair<node_id, path_component> new_loc(new_n->parent, new_n->name);

    if (is_dir_t(new_n))
      safe_insert(d.dirs_added, make_pair(new_loc, nid));
    else
      {
        file_id const & content = downcast_to_file_t(new_n)->content;
        safe_insert(d.files_added, make_pair(new_loc,
                                             make_pair(nid, content)));
      }
    for (full_attr_map_t::const_iterator i = new_n->attrs.begin();
         i != new_n->attrs.end(); ++i)
      safe_insert(d.attrs_changed, make_pair(nid, *i));
  }

  void
  do_delta_for_node_in_both(node_t old_n, node_t new_n, roster_delta_t & d)
  {
    I(old_n->self == new_n->self);
    node_id nid = old_n->self;
    // rename?
    {
      pair<node_id, path_component> old_loc(old_n->parent, old_n->name);
      pair<node_id, path_component> new_loc(new_n->parent, new_n->name);
      if (old_loc != new_loc)
        safe_insert(d.nodes_renamed, make_pair(nid, new_loc));
    }
    // delta?
    if (is_file_t(old_n))
      {
        file_id const & old_content = downcast_to_file_t(old_n)->content;
        file_id const & new_content = downcast_to_file_t(new_n)->content;
        if (!(old_content == new_content))
          safe_insert(d.deltas_applied, make_pair(nid, new_content));
      }
    // attrs?
    {
      parallel::iter<full_attr_map_t> i(old_n->attrs, new_n->attrs);
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);

            case parallel::in_left:
              safe_insert(d.attrs_cleared, make_pair(nid, i.left_key()));
              break;

            case parallel::in_right:
              safe_insert(d.attrs_changed, make_pair(nid, i.right_value()));
              break;

            case parallel::in_both:
              if (i.left_data() != i.right_data())
                safe_insert(d.attrs_changed, make_pair(nid, i.right_value()));
              break;
            }
        }
    }
  }

  void
  make_roster_delta_t(roster_t const & from, marking_map const & from_markings,
                      roster_t const & to, marking_map const & to_markings,
                      roster_delta_t & d)
  {
    MM(from);
    MM(from_markings);
    MM(to);
    MM(to_markings);
    {
      parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);
            
            case parallel::in_left:
              // deleted
              safe_insert(d.nodes_deleted, i.left_key());
              break;
            
            case parallel::in_right:
              // added
              do_delta_for_node_only_in_dest(i.right_data(), d);
              break;
            
            case parallel::in_both:
              // moved/patched/attribute changes
              do_delta_for_node_in_both(i.left_data(), i.right_data(), d);
              break;
            }
        }
    }
    {
      parallel::iter<marking_map> i(from_markings, to_markings);
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);
            
            case parallel::in_left:
              // deleted; don't need to do anything (will be handled by
              // nodes_deleted set
              break;
            
            case parallel::in_right:
              // added
              safe_insert(d.markings_changed, i.right_value());
              break;
            
            case parallel::in_both:
              // maybe changed
              if (!(i.left_data() == i.right_data()))
                safe_insert(d.markings_changed, i.right_value());
              break;
            }
        }
    }
  }

  namespace syms
  {
    symbol const deleted("deleted");
    symbol const rename("rename");
    symbol const add_dir("add_dir");
    symbol const add_file("add_file");
    symbol const delta("delta");
    symbol const attr_cleared("attr_cleared");
    symbol const attr_changed("attr_changed");
    symbol const marking("marking");
    
    symbol const content("content");
    symbol const location("location");
    symbol const attr("attr");
    symbol const value("value");
  }

  void
  push_nid(symbol const & sym, node_id nid, basic_io::stanza & st)
  {
    st.push_str_pair(sym, lexical_cast<std::string>(nid));
  }

  static void
  push_loc(pair<node_id, path_component> const & loc,
           basic_io::stanza & st)
  {
    st.push_str_triple(syms::location,
                       lexical_cast<std::string>(loc.first),
                       loc.second());
  }

  void
  print_roster_delta_t(basic_io::printer & printer,
                       roster_delta_t & d)
  {
    for (roster_delta_t::nodes_deleted_t::const_iterator
           i = d.nodes_deleted.begin(); i != d.nodes_deleted.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::deleted, *i, st);
        printer.print_stanza(st);
      }
    for (roster_delta_t::nodes_renamed_t::const_iterator
           i = d.nodes_renamed.begin(); i != d.nodes_renamed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::rename, i->first, st);
        push_loc(i->second, st);
        printer.print_stanza(st);
      }
    for (roster_delta_t::dirs_added_t::const_iterator
           i = d.dirs_added.begin(); i != d.dirs_added.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::add_dir, i->second, st);
        push_loc(i->first, st);
        printer.print_stanza(st);
      }
    for (roster_delta_t::files_added_t::const_iterator
           i = d.files_added.begin(); i != d.files_added.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::add_file, i->second.first, st);
        push_loc(i->first, st);
        st.push_binary_pair(syms::content, i->second.second.inner());
        printer.print_stanza(st);
      }
    for (roster_delta_t::deltas_applied_t::const_iterator
           i = d.deltas_applied.begin(); i != d.deltas_applied.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::delta, i->first, st);
        st.push_binary_pair(syms::content, i->second.inner());
        printer.print_stanza(st);
      }
    for (roster_delta_t::attrs_cleared_t::const_iterator
           i = d.attrs_cleared.begin(); i != d.attrs_cleared.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::attr_cleared, i->first, st);
        st.push_str_pair(syms::attr, i->second());
        printer.print_stanza(st);
      }
    for (roster_delta_t::attrs_changed_t::const_iterator
           i = d.attrs_changed.begin(); i != d.attrs_changed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::attr_changed, i->first, st);
        st.push_str_pair(syms::attr, i->second.first());
        st.push_str_triple(syms::value,
                           lexical_cast<std::string>(i->second.second.first),
                           i->second.second.second());
        printer.print_stanza(st);
      }
    for (roster_delta_t::markings_changed_t::const_iterator
           i = d.markings_changed.begin(); i != d.markings_changed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::marking, i->first, st);
        // ...this second argument is a bit odd...
        push_marking(st, !i->second.file_content.empty(), i->second);
        printer.print_stanza(st);
      }
  }

  node_id
  parse_nid(basic_io::parser & parser)
  {
    std::string s;
    parser.str(s);
    return lexical_cast<node_id>(s);
  }

  void
  parse_loc(basic_io::parser & parser,
            pair<node_id, path_component> & loc)
  {
    parser.esym(syms::location);
    loc.first = parse_nid(parser);
    std::string name;
    parser.str(name);
    loc.second = path_component(name);
  }

  void
  parse_roster_delta_t(basic_io::parser & parser, roster_delta_t & d)
  {
    while (parser.symp(syms::deleted))
      {
        parser.sym();
        safe_insert(d.nodes_deleted, parse_nid(parser));
      }
    while (parser.symp(syms::rename))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.nodes_renamed, make_pair(nid, loc));
      }
    while (parser.symp(syms::add_dir))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.dirs_added, make_pair(loc, nid));
      }
    while (parser.symp(syms::add_file))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        parser.esym(syms::content);
        std::string s;
        parser.hex(s);
        safe_insert(d.files_added,
                    make_pair(loc, make_pair(nid, file_id(decode_hexenc(s)))));
      }
    while (parser.symp(syms::delta))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::content);
        std::string s;
        parser.hex(s);
        safe_insert(d.deltas_applied, make_pair(nid, file_id(decode_hexenc(s))));
      }
    while (parser.symp(syms::attr_cleared))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        std::string key;
        parser.str(key);
        safe_insert(d.attrs_cleared, make_pair(nid, attr_key(key)));
      }
    while (parser.symp(syms::attr_changed))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        std::string key;
        parser.str(key);
        parser.esym(syms::value);
        std::string value_bool, value_value;
        parser.str(value_bool);
        parser.str(value_value);
        pair<bool, attr_value> full_value(lexical_cast<bool>(value_bool),
                                          attr_value(value_value));
        safe_insert(d.attrs_changed,
                    make_pair(nid,
                              make_pair(attr_key(key), full_value)));
      }
    while (parser.symp(syms::marking))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        marking_t m;
        parse_marking(parser, m);
        safe_insert(d.markings_changed, make_pair(nid, m));
      }
  }
  
} // end anonymous namespace

void
delta_rosters(roster_t const & from, marking_map const & from_markings,
              roster_t const & to, marking_map const & to_markings,
              roster_delta & del)
{
  MM(from);
  MM(from_markings);
  MM(to);
  MM(to_markings);
  roster_delta_t d;
  make_roster_delta_t(from, from_markings, to, to_markings, d);
  basic_io::printer printer;
  print_roster_delta_t(printer, d);
  del = roster_delta(printer.buf);
}

static
void read_roster_delta(roster_delta const & del,
                       roster_delta_t & d)
{
  basic_io::input_source src(del.inner()(), "roster_delta");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_roster_delta_t(pars, d);
}

void
apply_roster_delta(roster_delta const & del,
                   roster_t & roster, marking_map & markings)
{
  MM(del);
  MM(roster);
  MM(markings);

  roster_delta_t d;
  read_roster_delta(del, d);
  d.apply(roster, markings);
}

// Extract the marking for one node from the roster delta, or return false
// if they are not contained in that delta
bool
try_get_markings_from_roster_delta(roster_delta const & del,
                                   node_id const & nid,
                                   marking_t & markings)
{
  roster_delta_t d;
  read_roster_delta(del, d);

  std::map<node_id, marking_t>::iterator i = d.markings_changed.find(nid);
  if (i != d.markings_changed.end())
    {
      markings = i->second;
      return true;
    }
  else
    {
      return false;
    }
}

// Extract the content hash for one node from the roster delta -- if it is
// available.  If we know what the file_id was, we return true and set
// 'content' appropriately.  If we can prove that the file did not exist in
// this revision, we return true and set 'content' to a null id.  If we
// cannot determine anything about the file contents, then we return false
// -- in this case content is left undefined.
bool
try_get_content_from_roster_delta(roster_delta const & del,
                              node_id const & nid,
                              file_id & content)
{
  roster_delta_t d;
  read_roster_delta(del, d);
  
  roster_delta_t::deltas_applied_t::const_iterator i = d.deltas_applied.find(nid);
  if (i != d.deltas_applied.end())
    {
      content = i->second;
      return true;
    }

  // special case 1: the node was deleted, so we know for sure it's not
  // there anymore in this roster
  if (d.nodes_deleted.find(nid) != d.nodes_deleted.end())
    {
      content = file_id();
      return true;
    }

  // special case 2: the node was added, so we need to get the current
  // content hash from the add stanza
  for (roster_delta_t::files_added_t::const_iterator j = d.files_added.begin();
       j != d.files_added.end(); ++j)
    {
      if (j->second.first == nid)
        {
          content = j->second.second;
          return true;
        }
    }
   
  return false;
}

#ifdef BUILD_UNIT_TESTS

static void
spin(roster_t const & from, marking_map const & from_marking,
     roster_t const & to, marking_map const & to_marking)
{
  MM(from);
  MM(from_marking);
  MM(to);
  MM(to_marking);
  roster_delta del;
  MM(del);
  delta_rosters(from, from_marking, to, to_marking, del);

  roster_t tmp(from);
  MM(tmp);
  marking_map tmp_marking(from_marking);
  MM(tmp_marking);
  apply_roster_delta(del, tmp, tmp_marking);
  I(tmp == to);
  I(tmp_marking == to_marking);

  roster_delta del2;
  delta_rosters(from, from_marking, tmp, tmp_marking, del2);
  I(del == del2);
}

void test_roster_delta_on(roster_t const & a, marking_map const & a_marking,
                          roster_t const & b, marking_map const & b_marking)
{
  spin(a, a_marking, b, b_marking);
  spin(b, b_marking, a, a_marking);
}

#endif // BUILD_UNIT_TESTS


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

