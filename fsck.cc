// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>

#include "app_state.hh"
#include "fsck.hh"
#include "revision.hh"
#include "ui.hh"
#include "vocab.hh"

struct checked_file {
  int db_gets;       // number of db.get's for this file which each check the sha1
  int manifest_refs; // number of manifest references to this file

  checked_file():
    db_gets(0), manifest_refs(0) {}
};

struct checked_manifest {
  int db_gets;       // number of db.get's for this manifest which each check the sha1
  int revision_refs; // number of revision references to this manifest
  int missing_files; // number of missing files referenced by this manifest

  checked_manifest(): 
    db_gets(0), revision_refs(0), missing_files(0) {}
};

// revision refs should match ancestry parent refs 
// number of parents should match ancestry child refs

struct checked_revision {
  int db_gets;              // number of db.get's for this revision which each check the sha1
  int revision_refs;        // number of references to this revision from other revisions
  int ancestry_parent_refs; // number of references to this revision by ancestry parent
  int ancestry_child_refs;  // number of references to this revision by ancestry child
  int missing_manifests;    // number of manifests missing
  int missing_revisions;    // number of revisions missing
  int incomplete_manifests; // number of manifests missing files referenced by this revision

  std::set<revision_id> parents;

  checked_revision():
    db_gets(0), 
    revision_refs(0), ancestry_parent_refs(0), ancestry_child_refs(0), 
    missing_manifests(0), missing_revisions(0), incomplete_manifests(0) {}
};

static void
check_files(app_state & app, std::map<file_id, checked_file> & checked_files)
{
  std::set<file_id> files;

  app.db.get_file_ids(files);
  L(F("checking %d files\n") % files.size());

  ticker ticks("files", "f", files.size()/70+1);

  for (std::set<file_id>::const_iterator i = files.begin();
       i != files.end(); ++i) 
    {
      L(F("checking file %s\n") % *i);
      file_data data;
      app.db.get_file_version(*i, data);
      checked_files[*i].db_gets++;
      ++ticks;
    }

  I(checked_files.size() == files.size());
}

static void
check_manifests(app_state & app, 
                std::map<manifest_id, checked_manifest> & checked_manifests, 
                std::map<file_id, checked_file> & checked_files)
{
  std::set<manifest_id> manifests;

  app.db.get_manifest_ids(manifests);
  L(F("checking %d manifests\n") % manifests.size());

  ticker ticks("manifests", "m", manifests.size()/70+1);

  for (std::set<manifest_id>::const_iterator i = manifests.begin();
       i != manifests.end(); ++i) 
    {
      L(F("checking manifest %s\n") % *i);
      manifest_data data;
      app.db.get_manifest_version(*i, data);
      checked_manifests[*i].db_gets++;

      manifest_map man;
      read_manifest_map(data, man);

      for (manifest_map::const_iterator entry = man.begin(); entry != man.end();
           ++entry)
        {
          checked_files[manifest_entry_id(entry)].manifest_refs++;

          if (checked_files[manifest_entry_id(entry)].db_gets == 0)
            checked_manifests[*i].missing_files++;
        }

      ++ticks;
    }

  I(checked_manifests.size() == manifests.size());
}

static void
check_revisions(app_state & app, 
                std::map<revision_id, checked_revision> & checked_revisions,
                std::map<manifest_id, checked_manifest> & checked_manifests)
{
  std::set<revision_id> revisions;

  app.db.get_revision_ids(revisions);
  L(F("checking %d revisions\n") % revisions.size());

  ticker ticks("revisions", "r", revisions.size()/70+1);

  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i) 
    {
      L(F("checking revision %s\n") % *i);
      revision_data data;
      app.db.get_revision(*i, data);
      checked_revisions[*i].db_gets++;

      revision_set rev;
      read_revision_set(data, rev);

      checked_manifests[rev.new_manifest].revision_refs++;

      if (checked_manifests[rev.new_manifest].db_gets == 0) 
        checked_revisions[*i].missing_manifests++;

      if (checked_manifests[rev.new_manifest].missing_files > 0) 
        checked_revisions[*i].incomplete_manifests++;

      for (edge_map::const_iterator edge = rev.edges.begin(); 
           edge != rev.edges.end(); ++edge)
        {
          // ignore [] -> [...] manifests

          if (!null_id(edge_old_manifest(edge)))
            {
              checked_manifests[edge_old_manifest(edge)].revision_refs++;

              if (checked_manifests[edge_old_manifest(edge)].db_gets == 0)
                checked_revisions[*i].missing_manifests++;

              if (checked_manifests[edge_old_manifest(edge)].missing_files > 0)
                checked_revisions[*i].incomplete_manifests++;
            }
            
          // ignore [] -> [...] revisions

          // delay checking parents until we've processed all revisions
          if (!null_id(edge_old_revision(edge))) 
            {
              checked_revisions[edge_old_revision(edge)].revision_refs++;
              checked_revisions[*i].parents.insert(edge_old_revision(edge));
            }

          // also check that change_sets applied to old manifests == new
          // manifests (which might be a merge)
        }
      
      ++ticks;
    }

  // now check for parent revision existence and problems

  for (std::map<revision_id, checked_revision>::iterator
         revision = checked_revisions.begin(); 
       revision != checked_revisions.end(); ++revision)
    {
      for (std::set<revision_id>::const_iterator p = revision->second.parents.begin();
           p != revision->second.parents.end(); ++p)
        {
          if (checked_revisions[*p].db_gets == 0)
            revision->second.missing_revisions++;
        }
    }
  
  L(F("checked %d revisions after starting with %d\n") 
    % checked_revisions.size()
    % revisions.size());

  //   I(checked_revisions.size() == revisions.size());
}

static void
check_ancestry(app_state & app, 
               std::map<revision_id, checked_revision> & checked_revisions)
{
  std::multimap<revision_id, revision_id> graph;

  app.db.get_revision_ancestry(graph);
  L(F("checking %d ancestry edges\n") % graph.size());

  ticker ticks("ancestry", "a", graph.size()/70+1);

  // checked revision has set of parents
  // graph has revision and associated parents
  // these two representations of the graph should agree!

  std::set<revision_id> seen;
  for (std::multimap<revision_id, revision_id>::const_iterator i = graph.begin();
       i != graph.end(); ++i)
    {
      // ignore the [] -> [...] edges here too
      if (!null_id(i->first)) 
        {
          checked_revisions[i->first].ancestry_parent_refs++;

          if (!null_id(i->second)) 
            checked_revisions[i->second].ancestry_child_refs++;
        }

      ++ticks;
    }
}

void
check_db(app_state & app)
{
  std::map<file_id, checked_file> checked_files;
  std::map<manifest_id, checked_manifest> checked_manifests;
  std::map<revision_id, checked_revision> checked_revisions;

  check_files(app, checked_files);
  check_manifests(app, checked_manifests, checked_files);
  check_revisions(app, checked_revisions, checked_manifests);
  check_ancestry(app, checked_revisions);

  // revision certs
  // public keys

  // report findings

  int missing_files = 0;
  int unreferenced_files = 0;

  for (std::map<file_id, checked_file>::const_iterator 
         i = checked_files.begin(); i != checked_files.end(); ++i)
    {
      if (i->second.db_gets == 0)
        {
          missing_files++;
          P(F("file %s missing (%d manifest references)\n") 
            % i->first % i->second.manifest_refs);
        }

      if (i->second.manifest_refs == 0)
        {
          unreferenced_files++;
          P(F("file %s unreferenced\n") % i->first);
        }

    }

  int missing_manifests = 0;
  int unreferenced_manifests = 0;
  int incomplete_manifests = 0;

  for (std::map<manifest_id, checked_manifest>::const_iterator 
         i = checked_manifests.begin(); i != checked_manifests.end(); ++i)
    {
      if (i->second.db_gets == 0)
        {
          missing_manifests++;
          P(F("manifest %s missing (%d revision references)\n") 
            % i->first % i->second.revision_refs);
        }

      if (i->second.revision_refs == 0)
        {
          unreferenced_manifests++;
          P(F("manifest %s unreferenced\n") % i->first);
        }

      if (i->second.missing_files > 0)
        {
          incomplete_manifests++;
          P(F("manifest %s incomplete (%d missing files)\n") 
            % i->first % i->second.missing_files);
        }
    }

  int missing_revisions = 0;
  int incomplete_revisions = 0;
  int mismatched_parents = 0;
  int mismatched_children = 0;

  for (std::map<revision_id, checked_revision>::const_iterator 
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
    {
      if (i->second.db_gets == 0)
        {
          missing_revisions++;
          P(F("revision %s missing (%d revision references)\n") 
            % i->first % i->second.revision_refs);
        }

      if (i->second.missing_manifests > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing manifests)\n") 
            % i->first % i->second.missing_manifests);
        }

      if (i->second.missing_revisions > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing revisions)\n") 
            % i->first % i->second.missing_revisions);
        }

      if (i->second.incomplete_manifests > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d incomplete manifests)\n") 
            % i->first % i->second.incomplete_manifests);
        }

      if (i->second.ancestry_parent_refs != i->second.revision_refs)
        {
          mismatched_parents++;
          P(F("revision %s mismatched parents (%d ancestry parents; %d revision refs)\n") 
            % i->first 
            % i->second.ancestry_parent_refs
            % i->second.revision_refs );
        }

      if (i->second.ancestry_child_refs != i->second.parents.size())
        {
          mismatched_children++;
          P(F("revision %s mismatched children (%d ancestry children; %d parents)\n") 
            % i->first 
            % i->second.ancestry_child_refs
            % i->second.parents.size() );
        }
    }

  if (missing_files > 0) 
    W(F("%d missing files\n") % missing_files);
  if (unreferenced_files > 0) 
    W(F("%d unreferenced files\n") % unreferenced_files);

  if (missing_manifests > 0)
    W(F("%d missing manifests\n") % missing_manifests);
  if (unreferenced_manifests > 0) 
    W(F("%d unreferenced manifests\n") % unreferenced_manifests);
  if (incomplete_manifests > 0)
    W(F("%d incomplete manifests\n") % incomplete_manifests);

  if (missing_revisions > 0)
    W(F("%d missing revisions\n") % missing_revisions);
  if (incomplete_revisions > 0)
    W(F("%d incomplete revisions\n") % incomplete_revisions);
  if (mismatched_parents > 0)
    W(F("%d mismatched parents\n") % mismatched_parents);
  if (mismatched_children > 0)
    W(F("%d mismatched children\n") % mismatched_children);

  int total = missing_files + unreferenced_files +
    missing_manifests + unreferenced_manifests + incomplete_manifests +
    missing_revisions + incomplete_revisions + 
    mismatched_parents + mismatched_children;

  if (total > 0)
    P(F("check complete: %d files; %d manifests; %d revisions; %d problems detected\n") 
      % checked_files.size()
      % checked_manifests.size()
      % checked_revisions.size()
      % total);
  else
    P(F("check complete: %d files; %d manifests; %d revisions; database is good\n") 
      % checked_files.size()
      % checked_manifests.size()
      % checked_revisions.size());
}
