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
#include "transforms.hh"

void
extract_rearranged_paths(change_set::path_rearrangement const & rearrangement, path_set & paths)
{
  paths.insert(rearrangement.deleted_files.begin(), rearrangement.deleted_files.end());
  paths.insert(rearrangement.deleted_dirs.begin(), rearrangement.deleted_dirs.end());

  for (std::map<file_path, file_path>::const_iterator i = rearrangement.renamed_files.begin(); 
       i != rearrangement.renamed_files.end(); ++i) 
    {
      paths.insert(i->first); 
      paths.insert(i->second); 
    }

  for (std::map<file_path, file_path>::const_iterator i = rearrangement.renamed_dirs.begin(); 
       i != rearrangement.renamed_dirs.end(); ++i) 
    {
      paths.insert(i->first); 
      paths.insert(i->second); 
    }

  paths.insert(rearrangement.added_files.begin(), rearrangement.added_files.end());
}

static void 
extract_delta_paths(change_set::delta_map const & deltas, path_set & paths)
{
  for (change_set::delta_map::const_iterator i = deltas.begin(); i != deltas.end(); ++i)
    {
      paths.insert(i->first);
    }
}

static void
extract_changed_paths(change_set const & cs, path_set & paths)
{
  extract_rearranged_paths(cs.rearrangement, paths);
  extract_delta_paths(cs.deltas, paths);
}

void 
add_intermediate_paths(path_set & paths)
{
  path_set intermediate_paths;

  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      // we know that file_path's are normalized relative paths.  So we can
      // find intermediate paths simply by searching for /.
      std::string::size_type j = std::string::npos;
      while ((j = (*i)().rfind('/', j)) != std::string::npos)
        {
          file_path dir((*i)().substr(0, j));
          if (intermediate_paths.find(dir) != intermediate_paths.end()) break;
          if (paths.find(dir) != paths.end()) break;
          intermediate_paths.insert(dir);
          --j;
        }
    }

  paths.insert(intermediate_paths.begin(), intermediate_paths.end());
}


static void
restrict_path_set(std::string const & type,
                  path_set const & paths, 
                  path_set & included, 
                  path_set & excluded,
                  app_state & app)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (app.restriction_includes(*i)) 
        {
          L(F("restriction includes %s %s\n") % type % *i);
          included.insert(*i);
        }
      else
        {
          L(F("restriction excludes %s %s\n") % type % *i);
          excluded.insert(*i);
        }
    }
}

static void 
restrict_rename_set(std::string const & type,
                    std::map<file_path, file_path> const & renames, 
                    std::map<file_path, file_path> & included,
                    std::map<file_path, file_path> & excluded, 
                    app_state & app)
{
  for (std::map<file_path, file_path>::const_iterator i = renames.begin();
       i != renames.end(); ++i)
    {
      // include renames if either source or target name is included in the restriction
      if (app.restriction_includes(i->first) || app.restriction_includes(i->second))
        {
          L(F("restriction includes %s '%s' to '%s'\n") % type % i->first % i->second);
          included.insert(*i);
        }
      else
        {
          L(F("restriction excludes %s '%s' to '%s'\n") % type % i->first % i->second);
          excluded.insert(*i);
        }
    }
}

void
restrict_path_rearrangement(change_set::path_rearrangement const & work, 
                            change_set::path_rearrangement & included,
                            change_set::path_rearrangement & excluded,
                            app_state & app)
{
  restrict_path_set("delete file", work.deleted_files, 
                    included.deleted_files, excluded.deleted_files, app);
  restrict_path_set("delete dir", work.deleted_dirs, 
                    included.deleted_dirs, excluded.deleted_dirs, app);

  restrict_rename_set("rename file", work.renamed_files, 
                      included.renamed_files, excluded.renamed_files, app);
  restrict_rename_set("rename dir", work.renamed_dirs, 
                      included.renamed_dirs, excluded.renamed_dirs, app);

  restrict_path_set("add file", work.added_files, 
                    included.added_files, excluded.added_files, app);
}

static void
restrict_delta_map(change_set::delta_map const & deltas,
                   change_set::delta_map & included,
                   change_set::delta_map & excluded,
                   app_state & app)
{
  for (change_set::delta_map::const_iterator i = deltas.begin(); i!= deltas.end(); ++i)
    {
      if (app.restriction_includes(i->first)) 
        {
          L(F("restriction includes delta on %s\n") % i->first);
          included.insert(*i);
        }
      else
        {
          L(F("restriction excludes delta on %s\n") % i->first);
          excluded.insert(*i);
        }
    }
}

void
calculate_restricted_rearrangement(app_state & app, 
                                   std::vector<utf8> const & args,
                                   manifest_id & old_manifest_id,
                                   revision_id & old_revision_id,
                                   manifest_map & m_old,
                                   path_set & old_paths, 
                                   path_set & new_paths,
                                   change_set::path_rearrangement & included,
                                   change_set::path_rearrangement & excluded)
{
  change_set::path_rearrangement work;

  get_base_revision(app, 
                    old_revision_id,
                    old_manifest_id, m_old);

  extract_path_set(m_old, old_paths);

  get_path_rearrangement(work);

  path_set valid_paths(old_paths);
  extract_rearranged_paths(work, valid_paths);
  add_intermediate_paths(valid_paths);

  app.set_restriction(valid_paths, args); 

  restrict_path_rearrangement(work, included, excluded, app);

  apply_path_rearrangement(old_paths, included, new_paths);
}

void
calculate_restricted_revision(app_state & app, 
                              std::vector<utf8> const & args,
                              revision_set & rev,
                              manifest_map & m_old,
                              manifest_map & m_new,
                              change_set::path_rearrangement & excluded)
{
  manifest_id old_manifest_id;
  revision_id old_revision_id;
  boost::shared_ptr<change_set> cs(new change_set());
  path_set old_paths, new_paths;

  rev.edges.clear();
  m_old.clear();
  m_new.clear();

  calculate_restricted_rearrangement(app, args, 
                                     old_manifest_id, old_revision_id,
                                     m_old, old_paths, new_paths, 
                                     cs->rearrangement, excluded);

  build_restricted_manifest_map(new_paths, m_old, m_new, app);
  complete_change_set(m_old, m_new, *cs);

  calculate_ident(m_new, rev.new_manifest);
  L(F("new manifest is %s\n") % rev.new_manifest);

  rev.edges.insert(std::make_pair(old_revision_id,
                                  std::make_pair(old_manifest_id, cs)));
}

void
calculate_restricted_revision(app_state & app, 
                              std::vector<utf8> const & args,
                              revision_set & rev,
                              manifest_map & m_old,
                              manifest_map & m_new)
{
  change_set::path_rearrangement work;
  calculate_restricted_revision(app, args, rev, m_old, m_new, work);
}

void
calculate_unrestricted_revision(app_state & app, 
                                revision_set & rev,
                                manifest_map & m_old,
                                manifest_map & m_new)
{
  std::vector<utf8> empty_args;
  calculate_restricted_revision(app, empty_args, rev, m_old, m_new);
}

void
calculate_restricted_change_set(app_state & app, 
                                std::vector<utf8> const & args,
                                change_set const & cs,
                                change_set & included,
                                change_set & excluded)
{
  path_set valid_paths;

  extract_changed_paths(cs, valid_paths);
  add_intermediate_paths(valid_paths);

  app.set_restriction(valid_paths, args); 

  restrict_path_rearrangement(cs.rearrangement, 
                              included.rearrangement, excluded.rearrangement, app);

  restrict_delta_map(cs.deltas, included.deltas, excluded.deltas, app);
}


