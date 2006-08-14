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
    parallel_iter<full_attr_map_t> i(old_n->attrs(), new_n->attrs());
    while (i.next())
      {
        MM(i);
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
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);
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
