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

void
extract_rearranged_paths(cset const & cs, path_set & paths)
{
  paths.insert(cs.nodes_deleted.begin(), cs.nodes_deleted.end());
  paths.insert(cs.dirs_added.begin(), cs.dirs_added.end());

  for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      paths.insert(i->first);
    }

  for (std::map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin(); 
       i != cs.nodes_renamed.end(); ++i) 
    {
      paths.insert(i->first); 
      paths.insert(i->second); 
    }
}


void 
add_intermediate_paths(path_set & paths)
{
  path_set intermediate_paths;

  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      split_path sp;
      for (split_path::const_iterator j = i->begin(); j != i->end(); ++j)
        {
          sp.push_back(*j);
          intermediate_paths.insert(sp);
        }
    }
  paths.insert(intermediate_paths.begin(), intermediate_paths.end());
}

void
restrict_cset(cset const & cs, 
              cset & included,
              cset & excluded,
              app_state & app)
{
  included.clear();
  excluded.clear();

  for (path_set::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      if (app.restriction_includes(*i)) 
        safe_insert(included.nodes_deleted, *i);
      else
        safe_insert(excluded.nodes_deleted, *i);
    }

  for (std::map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin(); 
       i != cs.nodes_renamed.end(); ++i) 
    {
      if (app.restriction_includes(i->first) ||
          app.restriction_includes(i->second)) 
        safe_insert(included.nodes_renamed, *i);
      else
        safe_insert(excluded.nodes_renamed, *i);
    }

  for (path_set::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      // Here is a trick: when you're dealing with restrictions, you need
      // to make sure that any added parents required to make the
      // restriction-affected files exist come along for the ride.
      bool include_it = app.restriction_includes(*i);
      if (!include_it)
        {
          include_it = app.restriction_requires_parent(*i);
          if (include_it)
            W(F("Included required parent path '%s'\n") % *i);
        }
            
      if (include_it) 
        safe_insert(included.dirs_added, *i);
      else
        safe_insert(excluded.dirs_added, *i);
    }

  for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      if (app.restriction_includes(i->first)) 
        safe_insert(included.files_added, *i);
      else
        safe_insert(excluded.files_added, *i);
    }

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = 
         cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
    {
      if (app.restriction_includes(i->first)) 
        safe_insert(included.deltas_applied, *i);
      else
        safe_insert(excluded.deltas_applied, *i);
    }

  for (std::set<std::pair<split_path, attr_key> >::const_iterator i = 
         cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
    {
      if (app.restriction_includes(i->first)) 
        safe_insert(included.attrs_cleared, *i);
      else
        safe_insert(excluded.attrs_cleared, *i);
    }

  for (std::map<std::pair<split_path, attr_key>, attr_value>::const_iterator i =
         cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
    {
      if (app.restriction_includes(i->first.first)) 
        safe_insert(included.attrs_set, *i);
      else
        safe_insert(excluded.attrs_set, *i);
    }
}


// Project the old_paths through r_old + work, to find the new names of the
// paths (if they survived work)

void
remap_paths(path_set const & old_paths,
            roster_t const & r_old,
            cset const & work,
            path_set & new_paths)
{
  new_paths.clear();
  temp_node_id_source nis;
  roster_t r_tmp = r_old;
  editable_roster_base er(r_tmp, nis);
  work.apply_to(er);
  for (path_set::const_iterator i = old_paths.begin();
       i != old_paths.end(); ++i)
    {
      node_t n_old = r_old.get_node(*i);
      if (r_tmp.has_node(n_old->self))
        {
          split_path new_sp;
          r_tmp.get_name(n_old->self, new_sp);
          new_paths.insert(new_sp);
        }
    }
}

void
get_base_roster_and_working_cset(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_id & old_revision_id,
                                 roster_t & old_roster,
                                 path_set & old_paths, 
                                 path_set & new_paths,
                                 cset & included,
                                 cset & excluded)
{
  cset work;

  get_base_revision(app, old_revision_id, old_roster);
  get_work_cset(work);

  old_roster.extract_path_set(old_paths);

  path_set valid_paths(old_paths);
  extract_rearranged_paths(work, valid_paths);
  add_intermediate_paths(valid_paths);
  app.set_restriction(valid_paths, args); 

  restrict_cset(work, included, excluded, app);  
  remap_paths(old_paths, old_roster, work, new_paths);

  for (path_set::const_iterator i = included.dirs_added.begin();
       i != included.dirs_added.end(); ++i)
    new_paths.insert(*i);
  
  for (std::map<split_path, file_id>::const_iterator i = included.files_added.begin();
       i != included.files_added.end(); ++i)
    new_paths.insert(i->first);
  
  for (std::map<split_path, split_path>::const_iterator i = included.nodes_renamed.begin(); 
       i != included.nodes_renamed.end(); ++i) 
    new_paths.insert(i->second);
}

void
get_working_revision_and_rosters(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_set & rev,
                                 roster_t & old_roster,
                                 roster_t & new_roster,
                                 cset & excluded)
{
  revision_id old_revision_id;
  boost::shared_ptr<cset> cs(new cset());
  path_set old_paths, new_paths;

  rev.edges.clear();
  get_base_roster_and_working_cset(app, args, 
                                   old_revision_id,
                                   old_roster,
                                   old_paths,
                                   new_paths, 
                                   *cs, excluded);

  temp_node_id_source nis;
  new_roster = old_roster;
  editable_roster_base er(new_roster, nis);
  cs->apply_to(er);

  // Now update any idents in the new roster
  update_restricted_roster_from_filesystem(new_roster, app);

  calculate_ident(new_roster, rev.new_manifest);
  L(F("new manifest_id is %s\n") % rev.new_manifest);
  
  {
    // We did the following:
    //
    //  - restrict the working cset (MT/work)
    //  - apply the working cset to the new roster,
    //    giving us a rearranged roster (with incorrect content hashes)
    //  - re-scan file contents, updating content hashes
    // 
    // Alas, this is not enough: we must now re-calculate the cset
    // such that it contains the content deltas we found, and 
    // re-restrict that cset.
    //
    // FIXME: arguably, this *could* be made faster by doing a
    // "make_restricted_cset" (or "augment_restricted_cset_deltas_only" 
    // call, for maximum speed) but it's worth profiling before 
    // spending time on it.

    cset tmp_full, tmp_excluded;
    // We ignore excluded stuff, our 'excluded' argument is only really
    // supposed to have tree rearrangement stuff in it, and it already has
    // that
    make_cset(old_roster, new_roster, tmp_full);
    restrict_cset(tmp_full, *cs, tmp_excluded, app);
  }

  safe_insert(rev.edges, std::make_pair(old_revision_id, cs));
}

void
get_working_revision_and_rosters(app_state & app, 
                                 std::vector<utf8> const & args,
                                 revision_set & rev,
                                 roster_t & old_roster,
                                 roster_t & new_roster)
{
  cset excluded;
  get_working_revision_and_rosters(app, args, rev, 
                                   old_roster, new_roster, excluded);
}

void
get_unrestricted_working_revision_and_rosters(app_state & app, 
                                              revision_set & rev,
                                              roster_t & old_roster,
                                              roster_t & new_roster)
{
  std::vector<utf8> empty_args;
  std::set<utf8> saved_exclude_patterns(app.exclude_patterns);
  app.exclude_patterns.clear();
  get_working_revision_and_rosters(app, empty_args, rev, old_roster, new_roster);
  app.exclude_patterns = saved_exclude_patterns;
}


static void
extract_changed_paths(cset const & cs, path_set & paths)
{
  extract_rearranged_paths(cs, paths);

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i 
         = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
    paths.insert(i->first);
  
  for (std::set<std::pair<split_path, attr_key> >::const_iterator i =
         cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
    paths.insert(i->first);

  for (std::map<std::pair<split_path, attr_key>, attr_value>::const_iterator i =
         cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
    paths.insert(i->first.first);
}

void
calculate_restricted_cset(app_state & app, 
                          std::vector<utf8> const & args,
                          cset const & cs,
                          cset & included,
                          cset & excluded)
{
  path_set valid_paths;

  extract_changed_paths(cs, valid_paths);
  add_intermediate_paths(valid_paths);

  app.set_restriction(valid_paths, args); 
  restrict_cset(cs, included, excluded, app);
}
