#include "roster_delta.hh"

void
roster_delta::apply(roster_t & roster, marking_map & markings) const
{
  // detach everything that should be detached
  for (nodes_deleted_t i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
    roster.detach_node(*i);
  for (nodes_renamed_t::const_iterator i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
    roster.detach_node(i->first);
  // delete the delete-able things
  for (nodes_deleted_t::const_iterator i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
    roster.drop_detached_node(*i);
  // add the new things
  for (dirs_added_t::const_iterator i = dirs_added.begin(); i != dirs_added.end(); ++i)
    roster.create_dir_node(i->second);
  for (files_added_t::const_iterator i = files_added.begin(); i != files_added.end(); ++i)
    roster.create_file_node(i->second.second, i->second.first);
  // attach everything
  for (dirs_added_t::const_iterator i = dirs_added.begin(); i != dirs_added.end(); ++i)
    roster.attach_node(i->second, i->first.first, i->first.second);
  for (files_added_t::const_iterator i = files_added.begin(); i != files_added.end(); ++i)
    roster.attach_node(i->second.second, i->first.first, i->first.second);
  for (nodes_renamed_t::const_iterator i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
    roster.attach_node(i->first, i->second.first, i->second.second);

  // roight, all that tricky tree-rearranging done, just have to do some
  // individual node edits now
  for (deltas_applied_t::const_iterator i = deltas_applied.begin(); i != deltas_applied.end(); ++i)
    roster.set_delta(i->first, i->second);
  for (attrs_cleared_t::const_iterator i = attrs_cleared.begin(); i != attrs_cleared.end(); ++i)
    roster._attr(i->first, i->second.first, i->second.second);
  for (attrs_changed_t::const_iterator i = attrs_changed.begin(); i != attrs_changed.end(); ++i)
    roster.set_attr(i->first, i->second.first, i->second.second);

  // and the markings
  for (nodes_deleted_t::const_iterator i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
    safe_erase(markings, *i);
  for (markings_changed_t::const_iterator i = markings_changed.begin(); i != markings_changed.end(); ++i)
    {
      marking_map::iterator j = markings.find(i->first);
      I(j != markings.end());
      (*j) = i->second;
    }
}

static void
delta_only_in_to(node_t new_n, roster_delta & d)
{
  node_id nid = new_n->self;
  std::pair<node_id, path_component> new_loc(new_n->parent, new_n->name);

  if (is_dir_t(new_n))
    safe_insert(d.dirs_added, std::make_pair(new_loc, nid));
  else
    {
      file_id const & content = downcast_to_file_t(new_n)->content;
      safe_insert(d.dirs_added, std::make_pair(new_loc,
                                               std::make_pair(nid, content)));
    }
  for (full_attr_map_t::const_iterator i = new_n->attrs.begin();
       i != new_n->attrs.end(); ++i)
    safe_insert(d.attrs_changed, std::make_pair(nid, *i));
}

static void
delta_in_both(node_t old_n, node_t new_n, roster_delta & d)
{
  I(old_n->self == new_n->self);
  node_id nid = old_n->self;
  // rename?
  {
    std::pair<node_id, path_component> old_loc(old_n->parent, old_n->name);
    std::pair<node_id, path_component> new_loc(new_n->parent, new_n->name);
    if (old_loc != new_loc)
      safe_insert(d.nodes_renamed, std::make_pair(nid, new_loc));
  }
  // delta?
  if (is_file_t(old_n))
    {
      file_id const & old_content = downcast_to_file_t(old_n)->content;
      file_id const & new_content = downcast_to_file_t(new_n)->content;
      if (old_content != new_content)
        safe_insert(d.deltas_applied, std::make_pair(nid, new_content));
    }
  // attrs?
  {
    parallel::iter<full_attr_map_t> i(old_n->attrs(), new_n->attrs());
    MM(i);
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            safe_insert(d.attrs_cleared, std::make_pair(nid, i.left_key()));
            break;

          case parallel::in_right:
            safe_insert(d.attrs_changed, std::make_pair(nid, i.right_value()));
            break;

          case parallel::in_both:
            if (i.left_data() != i.right_data())
              safe_insert(d.attrs_changed, std::make_pair(nid, i.right_value()));
            break;
          }
      }
  }
}

void
make_roster_delta(roster_t const & from, marking_map const & from_markings,
                  roster_t const & to, marking_map const & to_markings,
                  roster_delta & d)
{
  MM(from);
  MM(from_markings);
  MM(to);
  MM(to_markings);
  MM(d);
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
            delta_only_in_to(i.right_data(), d);
            break;
            
          case parallel::in_both:
            // moved/patched/attribute changes
            delta_in_both(i.left_data(), i.right_data(), d);
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
            if (i.left_data() != i.right_data())
              safe_insert(d.markings_changed, i.right_value());
            break;
          }
      }
  }
}

namespace
{
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
}

static node_id
parse_nid(basic_io::parser & parser)
{
  std::string s;
  parser.str(s);
  return lexical_cast<node_id>(s);
}

static 
parse_loc(basic_io::parser & parser,
          std::pair<node_id, path_component> & loc)
{
  loc.first = parse_nid(parser);
  std::string name;
  parser.str(name);
  loc.second = path_component(name);
}

void
parse_roster_delta(basic_io::parser & parser, roster_delta & d)
{
  string t1, t2;
  MM(t1);
  MM(t2);
  
  while (parser.symp(syms::deleted))
    {
      parser.sym();
      safe_insert(d.nodes_deleted(parse_nid(parser)));
    }
  while (parser.symp(syms::rename))
    {
      parser.sym();
      node_id nid = parse_nid(parser);
      parser.esym(syms::location);
      std::pair<node_id, path_component> loc;
      parse_loc(parser, loc);
      safe_insert(d.nodes_renamed, std::make_pair(nid, loc));
    }
  while (parser.symp(syms::add_dir))
    {
      parser.sym();
      node_id nid = parse_nid(parser);
      parser.esym(sysm::location);
      std::pair<node_id, path_component> loc;
      parse_loc(parser, loc);
      safe_insert(d.dirs_added, std::make_pair(loc, nid));
    }
  while (parser.symp(syms::add_file))
    {
      parser.sym();
      node_id nid = parse_nid(parser);
      parser.esym(sysm::location);
      std::pair<node_id, path_component> loc;
      parse_loc(parser, loc);
      safe_insert(d.dirs_added, std::make_pair(loc, nid));
      parser.esym(syms::content);
      string s;
      parser.hex(s);
      safe_insert(d.files_added,
                  std::make_pair(loc, std::make_pair(nid, file_id(s))));
    }
  while (parser.symp(syms::delta))
    {
      parser.sym();
      node_id nid = parse_nid(parser);
      parser.esym(syms::content);
      string s;
      parser.hex(s);
      safe_insert(d.deltas_applied, std::make_pair(nid, file_id(s)));
    }
  while (parser.symp(syms::attr_cleared))
    {
      node_id nid = parse_nid(parser);
      parser.esym(syms::attr);
      string key;
      parser.str(key);
      safe_insert(d.attrs_cleared, std::make_pair(nid, attr_key(key)));
    }
  while (parser.symp(syms::attr_changed))
    {
      node_id nid = parse_nid(parser);
      parser.esym(syms::attr);
      string key;
      parser.str(key);
      parser.esym(syms::value);
      string value_bool, value_value;
      parser.str(value_bool);
      parser.str(value_value);
      safe_insert(d.attrs_changed,
                  std::make_pair(nid,
                                 std::make_pair(attr_key(key),
                                                std::make_pair(std::lexical_cast<bool>(value_bool),
                                                               attr_value(value_value)))));
    }
  while (parser.symp(syms::marking))
    {
      parser.sym();
      node_id nid = parse_nid(parser);
      marking_t m;
      parse_marking(parser, m);
      safe_insert(d.markings_changed, std::make_pair(nid, m));
    }
}
