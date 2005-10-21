// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>

#include "app_state.hh"
#include "constants.hh"
#include "database_check.hh"
#include "keys.hh"
#include "revision.hh"
#include "ui.hh"
#include "vocab.hh"
#include "transforms.hh"

// the database has roughly the following structure
//
//      certs
//        |
//    +---+---+
//    |       |
//   keys   revisions
//            | 
//          manifests
//            | 
//          files
//

struct checked_cert {
  revision<cert> rcert;
  bool found_key;
  bool good_sig;

  checked_cert(revision<cert> const & c): rcert(c), found_key(false), good_sig(false) {}
};

struct checked_key {
  bool found;       // found public keypair id in db
  size_t sigs;                // number of signatures by this key

  base64<rsa_pub_key> pub_encoded;

  checked_key(): found(false), sigs(0) {}
};

struct checked_file {
  bool found;           // found in db, retrieved and verified sha1 hash
  size_t manifest_refs; // number of manifest references to this file

  checked_file(): found(false), manifest_refs(0) {}
};

struct checked_manifest {
  bool found;           // found in db, retrieved and verified sha1 hash
  size_t revision_refs; // number of revision references to this manifest
  size_t missing_files; // number of missing files referenced by this manifest

  bool parseable;       // read_manifest_map does not throw
  bool normalized;      // write_manifest_map( read_manifest_map(dat) ) == dat

  checked_manifest(): 
    found(false), revision_refs(0), 
    missing_files(0), parseable(false), normalized(false) {}
};

// the number of times a revision is referenced (revision_refs)
// should match the number of times it is listed as a parent in 
// the ancestry cache (ancestry_parent_refs)
//
// the number of parents a revision has should match the number
// of times it is listed as a child in the ancestry cache 
// (ancestry_child_refs)

struct checked_revision {
  bool found;                  // found in db, retrieved and verified sha1 hash
  size_t revision_refs;        // number of references to this revision from other revisions
  size_t ancestry_parent_refs; // number of references to this revision by ancestry parent
  size_t ancestry_child_refs;  // number of references to this revision by ancestry child
  
  size_t missing_manifests;    // number of missing manifests referenced by this revision
  size_t missing_revisions;    // number of missing revisions referenced by this revision
  size_t incomplete_manifests; // number of manifests missing files referenced by this revision
  
  size_t cert_refs;            // number of references to this revision by revision certs;

  bool parseable;              // read_revision_set does not throw
  bool normalized;             // write_revision_set( read_revision_set(dat) ) == dat

  std::string history_error;

  std::set<revision_id> parents;
  std::vector<checked_cert> checked_certs;

  checked_revision(): 
    found(false),
    revision_refs(0), ancestry_parent_refs(0), ancestry_child_refs(0), 
    missing_manifests(0), missing_revisions(0), incomplete_manifests(0), 
    cert_refs(0), parseable(false), normalized(false) {}
};

static void
check_files(app_state & app, std::map<file_id, checked_file> & checked_files)
{
  std::set<file_id> files;

  app.db.get_file_ids(files);
  L(F("checking %d files\n") % files.size());

  ticker ticks(_("files"), "f", files.size()/70+1);

  for (std::set<file_id>::const_iterator i = files.begin();
       i != files.end(); ++i) 
    {
      L(F("checking file %s\n") % *i);
      file_data data;
      app.db.get_file_version(*i, data);
      checked_files[*i].found = true;
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

  ticker ticks(_("manifests"), "m", manifests.size()/70+1);

  for (std::set<manifest_id>::const_iterator i = manifests.begin();
       i != manifests.end(); ++i) 
    {
      L(F("checking manifest %s\n") % *i);
      manifest_data data;
      app.db.get_manifest_version(*i, data);
      checked_manifests[*i].found = true;

      manifest_map man;
      try
        {
          read_manifest_map(data, man);
        }
      catch (std::logic_error & e)
        {
          L(F("error parsing manifest %s: %s") % *i % e.what());
          checked_manifests[*i].parseable = false;
          continue;
        }
      checked_manifests[*i].parseable = true;

      // normalisation check
      manifest_id norm_ident;
      manifest_data norm_data;
      write_manifest_map(man, norm_data);
      calculate_ident(norm_data, norm_ident);
      if (norm_ident == *i)
          checked_manifests[*i].normalized = true;

      for (manifest_map::const_iterator entry = man.begin(); entry != man.end();
           ++entry)
        {
          checked_files[manifest_entry_id(entry)].manifest_refs++;

          if (!checked_files[manifest_entry_id(entry)].found)
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

  ticker ticks(_("revisions"), "r", revisions.size()/70+1);

  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i) 
    {
      L(F("checking revision %s\n") % *i);
      revision_data data;
      app.db.get_revision(*i, data);
      checked_revisions[*i].found = true;

      revision_set rev;
      try
        {
          read_revision_set(data, rev);
        }
      catch (std::logic_error & e)
        {
          L(F("error parsing revision %s: %s") % *i % e.what());
          checked_revisions[*i].parseable = false;
          continue;
        }
      checked_revisions[*i].parseable = true;

      // normalisation check
      revision_id norm_ident;
      revision_data norm_data;
      write_revision_set(rev, norm_data);
      calculate_ident(norm_data, norm_ident);
      if (norm_ident == *i)
          checked_revisions[*i].normalized = true;

      checked_manifests[rev.new_manifest].revision_refs++;

      if (!checked_manifests[rev.new_manifest].found) 
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

              if (!checked_manifests[edge_old_manifest(edge)].found)
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
          if (!checked_revisions[*p].found)
            revision->second.missing_revisions++;
        }
    }
  
  L(F("checked %d revisions after starting with %d\n") 
    % checked_revisions.size()
    % revisions.size());
}

static void
check_ancestry(app_state & app, 
               std::map<revision_id, checked_revision> & checked_revisions)
{
  std::multimap<revision_id, revision_id> graph;

  app.db.get_revision_ancestry(graph);
  L(F("checking %d ancestry edges\n") % graph.size());

  ticker ticks(_("ancestry"), "a", graph.size()/70+1);

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

static void
check_keys(app_state & app, 
           std::map<rsa_keypair_id, checked_key> & checked_keys)
{
  std::vector<rsa_keypair_id> pubkeys;

  app.db.get_public_keys(pubkeys);

  L(F("checking %d public keys\n") % pubkeys.size());

  ticker ticks(_("keys"), "k", 1);

  for (std::vector<rsa_keypair_id>::const_iterator i = pubkeys.begin();
       i != pubkeys.end(); ++i)
    {
      app.db.get_key(*i, checked_keys[*i].pub_encoded);
      checked_keys[*i].found = true;
      ++ticks;
    }

}

static void
check_certs(app_state & app, 
            std::map<revision_id, checked_revision> & checked_revisions,
            std::map<rsa_keypair_id, checked_key> & checked_keys,
            size_t & total_certs)
{

  std::vector< revision<cert> > certs;
  app.db.get_revision_certs(certs);

  total_certs = certs.size();

  L(F("checking %d revision certs\n") % certs.size());

  ticker ticks(_("certs"), "c", certs.size()/70+1);

  for (std::vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      checked_cert checked(*i);
      checked.found_key = checked_keys[i->inner().key].found;

      if (checked.found_key) 
        {
          std::string signed_text;
          cert_signable_text(i->inner(), signed_text);
          checked.good_sig = check_signature(app, i->inner().key, 
                                             checked_keys[i->inner().key].pub_encoded, 
                                             signed_text, i->inner().sig);
        }

      checked_keys[i->inner().key].sigs++;
      checked_revisions[i->inner().ident].checked_certs.push_back(checked);

      ++ticks;
    }
}

static void
check_sane(app_state & app, 
           std::map<revision_id, checked_revision> & checked_revisions)
{
  L(F("checking local history of %d revisions\n") % checked_revisions.size());

  ticker ticks(_("revisions"), "r", 1);

  for (std::map<revision_id, checked_revision>::iterator 
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
  {
    if (i->second.found)
      {
        try 
          {
            check_sane_history(i->first, constants::verify_depth, app);
          }
        catch (std::exception & e)
          {
            i->second.history_error = e.what();
          }
      }
    ++ticks;
  }
}

static void
report_files(std::map<file_id, checked_file> const & checked_files, 
             size_t & missing_files, 
             size_t & unreferenced_files)
{
  for (std::map<file_id, checked_file>::const_iterator 
         i = checked_files.begin(); i != checked_files.end(); ++i)
    {
      checked_file file = i->second;

      if (!file.found)
        {
          missing_files++;
          P(F("file %s missing (%d manifest references)\n") 
            % i->first % file.manifest_refs);
        }

      if (file.manifest_refs == 0)
        {
          unreferenced_files++;
          P(F("file %s unreferenced\n") % i->first);
        }

    }
}

static void
report_manifests(std::map<manifest_id, checked_manifest> const & checked_manifests, 
                 size_t & missing_manifests, 
                 size_t & unreferenced_manifests,
                 size_t & incomplete_manifests,
                 size_t & non_parseable_manifests,
                 size_t & non_normalized_manifests)
{
  for (std::map<manifest_id, checked_manifest>::const_iterator 
         i = checked_manifests.begin(); i != checked_manifests.end(); ++i)
    {
      checked_manifest manifest = i->second;

      if (!manifest.found)
        {
          missing_manifests++;
          P(F("manifest %s missing (%d revision references)\n") 
            % i->first % manifest.revision_refs);
        }

      if (manifest.revision_refs == 0)
        {
          unreferenced_manifests++;
          P(F("manifest %s unreferenced\n") % i->first);
        }

      if (manifest.missing_files > 0)
        {
          incomplete_manifests++;
          P(F("manifest %s incomplete (%d missing files)\n") 
            % i->first % manifest.missing_files);
        }

      if (!manifest.parseable)
        {
          non_parseable_manifests++;
          P(F("manifest %s is not parseable (perhaps with unnormalized paths?)\n")
            % i->first);
        }

      if (manifest.parseable && !manifest.normalized)
        {
          non_normalized_manifests++;
          P(F("manifest %s is not in normalized form\n")
            % i->first);
        }
    }
}

static void
report_revisions(std::map<revision_id, checked_revision> const & checked_revisions,
                 size_t & missing_revisions,
                 size_t & incomplete_revisions,
                 size_t & mismatched_parents,
                 size_t & mismatched_children,
                 size_t & bad_history,
                 size_t & non_parseable_revisions,
                 size_t & non_normalized_revisions)
{
  for (std::map<revision_id, checked_revision>::const_iterator 
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
    {
      checked_revision revision = i->second;

      if (!revision.found)
        {
          missing_revisions++;
          P(F("revision %s missing (%d revision references; %d cert references)\n") 
            % i->first % revision.revision_refs % revision.cert_refs);
        }

      if (revision.missing_manifests > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing manifests)\n") 
            % i->first % revision.missing_manifests);
        }

      if (revision.missing_revisions > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing revisions)\n") 
            % i->first % revision.missing_revisions);
        }

      if (revision.incomplete_manifests > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d incomplete manifests)\n") 
            % i->first % revision.incomplete_manifests);
        }

      if (revision.ancestry_parent_refs != revision.revision_refs)
        {
          mismatched_parents++;
          P(F("revision %s mismatched parents (%d ancestry parents; %d revision refs)\n") 
            % i->first 
            % revision.ancestry_parent_refs
            % revision.revision_refs );
        }

      if (revision.ancestry_child_refs != revision.parents.size())
        {
          mismatched_children++;
          P(F("revision %s mismatched children (%d ancestry children; %d parents)\n") 
            % i->first 
            % revision.ancestry_child_refs
            % revision.parents.size() );
        }

      if (!revision.history_error.empty())
        {
          bad_history++;
          std::string tmp = revision.history_error;
          if (tmp[tmp.length() - 1] == '\n')
            tmp.erase(tmp.length() - 1);
          P(F("revision %s has bad history (%s)\n")
            % i->first % tmp);
        }

      if (!revision.parseable)
        {
          non_parseable_revisions++;
          P(F("revision %s is not parseable (perhaps with unnormalized paths?)\n")
            % i->first);
        }

      if (revision.parseable && !revision.normalized)
        {
          non_normalized_revisions++;
          P(F("revision %s is not in normalized form\n")
            % i->first);
        }
    }
}

static void
report_keys(std::map<rsa_keypair_id, checked_key> const & checked_keys,
            size_t & missing_keys)
{
  for (std::map<rsa_keypair_id, checked_key>::const_iterator 
         i = checked_keys.begin(); i != checked_keys.end(); ++i)
    {
      checked_key key = i->second;

      if (key.found)
        {
          L(F("key %s signed %d certs\n") 
            % i->first
            % key.sigs);
        }
      else
        {
          missing_keys++;
          P(F("key %s missing (signed %d certs)\n") 
            % i->first
            % key.sigs);
        }
    }
}

static void
report_certs(std::map<revision_id, checked_revision> const & checked_revisions,
             size_t & missing_certs,
             size_t & mismatched_certs,
             size_t & unchecked_sigs,
             size_t & bad_sigs)
{
  std::set<cert_name> cnames;

  cnames.insert(cert_name(author_cert_name));
  cnames.insert(cert_name(branch_cert_name));
  cnames.insert(cert_name(changelog_cert_name));
  cnames.insert(cert_name(date_cert_name));

  for (std::map<revision_id, checked_revision>::const_iterator
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
    {
      checked_revision revision = i->second;
      std::map<cert_name, size_t> cert_counts;
      
      for (std::vector<checked_cert>::const_iterator checked = revision.checked_certs.begin();
           checked != revision.checked_certs.end(); ++checked)
        {
          if (!checked->found_key)
            {
              unchecked_sigs++;
              P(F("revision %s unchecked signature in %s cert from missing key %s\n") 
                % i->first 
                % checked->rcert.inner().name
                % checked->rcert.inner().key);
            }
          else if (!checked->good_sig)
            {
              bad_sigs++;
              P(F("revision %s bad signature in %s cert from key %s\n") 
                % i->first 
                % checked->rcert.inner().name
                % checked->rcert.inner().key);
            }

          cert_counts[checked->rcert.inner().name]++;
        }

      for (std::set<cert_name>::const_iterator n = cnames.begin(); 
           n != cnames.end(); ++n)
        {
          if (revision.found && cert_counts[*n] == 0)
            {
              missing_certs++;
              P(F("revision %s missing %s cert\n") % i->first % *n);
            }
        }

      if (cert_counts[cert_name(author_cert_name)] != cert_counts[cert_name(changelog_cert_name)] ||
          cert_counts[cert_name(author_cert_name)] != cert_counts[cert_name(date_cert_name)] ||
          cert_counts[cert_name(date_cert_name)]   != cert_counts[cert_name(changelog_cert_name)])
        {
          mismatched_certs++;
          P(F("revision %s mismatched certs (%d authors %d dates %d changelogs)\n") 
            % i->first
            % cert_counts[cert_name(author_cert_name)]
            % cert_counts[cert_name(date_cert_name)]
            % cert_counts[cert_name(changelog_cert_name)]);
        }

    }
}

void
check_db(app_state & app)
{
  std::map<file_id, checked_file> checked_files;
  std::map<manifest_id, checked_manifest> checked_manifests;
  std::map<revision_id, checked_revision> checked_revisions;
  std::map<rsa_keypair_id, checked_key> checked_keys;

  size_t missing_files = 0;
  size_t unreferenced_files = 0;

  size_t missing_manifests = 0;
  size_t unreferenced_manifests = 0;
  size_t incomplete_manifests = 0;
  size_t non_parseable_manifests = 0;
  size_t non_normalized_manifests = 0;

  size_t missing_revisions = 0;
  size_t incomplete_revisions = 0;
  size_t mismatched_parents = 0;
  size_t mismatched_children = 0;
  size_t bad_history = 0;
  size_t non_parseable_revisions = 0;
  size_t non_normalized_revisions = 0;
  
  size_t missing_keys = 0;

  size_t total_certs = 0;
  size_t missing_certs = 0;
  size_t mismatched_certs = 0;
  size_t unchecked_sigs = 0;
  size_t bad_sigs = 0;

  check_files(app, checked_files);
  check_manifests(app, checked_manifests, checked_files);
  check_revisions(app, checked_revisions, checked_manifests);
  check_ancestry(app, checked_revisions);
  check_sane(app, checked_revisions);
  check_keys(app, checked_keys);
  check_certs(app, checked_revisions, checked_keys, total_certs);

  report_files(checked_files, missing_files, unreferenced_files);

  report_manifests(checked_manifests, 
                   missing_manifests, unreferenced_manifests, 
                   incomplete_manifests,
                   non_parseable_manifests,
                   non_normalized_manifests);

  report_revisions(checked_revisions,
                   missing_revisions, incomplete_revisions, 
                   mismatched_parents, mismatched_children,
                   bad_history, non_parseable_revisions,
                   non_normalized_revisions);

  report_keys(checked_keys, missing_keys);

  report_certs(checked_revisions,
               missing_certs, mismatched_certs,
               unchecked_sigs, bad_sigs);

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
  if (non_parseable_manifests > 0)
    W(F("%d manifests not parseable (perhaps with invalid paths)\n")
      % non_parseable_manifests);
  if (non_normalized_manifests > 0)
    W(F("%d manifests not in normalized form\n") % non_normalized_manifests);

  if (missing_revisions > 0)
    W(F("%d missing revisions\n") % missing_revisions);
  if (incomplete_revisions > 0)
    W(F("%d incomplete revisions\n") % incomplete_revisions);
  if (mismatched_parents > 0)
    W(F("%d mismatched parents\n") % mismatched_parents);
  if (mismatched_children > 0)
    W(F("%d mismatched children\n") % mismatched_children);
  if (bad_history > 0)
    W(F("%d revisions with bad history\n") % bad_history);
  if (non_parseable_revisions > 0)
    W(F("%d revisions not parseable (perhaps with invalid paths)\n")
      % non_parseable_revisions);
  if (non_normalized_revisions > 0)
    W(F("%d revisions not in normalized form\n") % non_normalized_revisions);

  if (missing_keys > 0)
    W(F("%d missing keys\n") % missing_keys);

  if (missing_certs > 0)
    W(F("%d missing certs\n") % missing_certs);
  if (mismatched_certs > 0)
    W(F("%d mismatched certs\n") % mismatched_certs);
  if (unchecked_sigs > 0)
    W(F("%d unchecked signatures due to missing keys\n") % unchecked_sigs);
  if (bad_sigs > 0)
    W(F("%d bad signatures\n") % bad_sigs);

  size_t total = missing_files + unreferenced_files +
    missing_manifests + unreferenced_manifests + incomplete_manifests +
    non_parseable_manifests + non_normalized_manifests +
    missing_revisions + incomplete_revisions + 
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children +
    bad_history +
    missing_certs + mismatched_certs +
    unchecked_sigs + bad_sigs +
    missing_keys;
  // unreferenced files and manifests and mismatched certs are not actually
  // serious errors; odd, but nothing will break.
  size_t serious = missing_files + 
    missing_manifests + incomplete_manifests +
    non_parseable_manifests + non_normalized_manifests +
    missing_revisions + incomplete_revisions + 
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children +
    bad_history +
    missing_certs +
    unchecked_sigs + bad_sigs +
    missing_keys;

  P(F("check complete: %d files; %d manifests; %d revisions; %d keys; %d certs\n")
    % checked_files.size()
    % checked_manifests.size()
    % checked_revisions.size()
    % checked_keys.size()
    % total_certs);
  P(F("total problems detected: %d (%d serious)\n") % total % serious);
  if (serious)
    E(false, F("serious problems detected"));
  else if (total)
    P(F("minor problems detected\n"));
  else
    P(F("database is good\n"));
}
