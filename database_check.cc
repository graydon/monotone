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
//          rosters
//            | 
//          files
//

using std::set;
using std::map;
using std::vector;

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
  size_t roster_refs; // number of roster references to this file

  checked_file(): found(false), roster_refs(0) {}
};

struct checked_roster {
  bool found;           // found in db, retrieved and verified sha1 hash
  size_t revision_refs; // number of revision references to this roster
  size_t missing_files; // number of missing files referenced by this roster
  size_t missing_mark_revs; // number of missing revisions referenced in node markings by this roster

  bool parseable;       // read_roster_and_marking does not throw
  bool normalized;      // write_roster_and_marking( read_roster_and_marking(dat) ) == dat

  manifest_id man_id;   // manifest id of this roster's public part

  checked_roster(): 
    found(false), revision_refs(0), 
    missing_files(0), missing_mark_revs(0),
    parseable(false), normalized(false), man_id() {}
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
  size_t marking_refs;         // number of references to this revision by roster markings
  
  bool found_roster_link;      // the revision->roster link for this revision exists
  bool found_roster;           // the roster for this revision exists
  bool manifest_mismatch;      // manifest doesn't match the roster for this revision
  bool incomplete_roster;      // the roster for this revision is missing files 
  size_t missing_manifests;    // number of missing manifests referenced by this revision
  size_t missing_revisions;    // number of missing revisions referenced by this revision
  
  size_t cert_refs;            // number of references to this revision by revision certs;

  bool parseable;              // read_revision_set does not throw
  bool normalized;             // write_revision_set( read_revision_set(dat) ) == dat

  std::string history_error;

  std::set<revision_id> parents;
  std::vector<checked_cert> checked_certs;

  checked_revision(): 
    found(false),
    revision_refs(0), ancestry_parent_refs(0), ancestry_child_refs(0), 
    marking_refs(0),
    found_roster(false), manifest_mismatch(false), incomplete_roster(false),
    missing_manifests(0), missing_revisions(0), 
    cert_refs(0), parseable(false), normalized(false) {}
};

static void
check_files(app_state & app, std::map<file_id, checked_file> & checked_files)
{
  std::set<file_id> files;

  app.db.get_file_ids(files);
  L(FL("checking %d files\n") % files.size());

  ticker ticks(_("files"), "f", files.size()/70+1);

  for (std::set<file_id>::const_iterator i = files.begin();
       i != files.end(); ++i) 
    {
      L(FL("checking file %s\n") % *i);
      file_data data;
      app.db.get_file_version(*i, data);
      checked_files[*i].found = true;
      ++ticks;
    }

  I(checked_files.size() == files.size());
}

// first phase of roster checking, checks manifest-related parts of the 
// roster, and general parsability/normalisation
static void
check_rosters_manifest(app_state & app,
              std::map<hexenc<id>, checked_roster> & checked_rosters,
              std::map<revision_id, checked_revision> & checked_revisions,
              std::set<manifest_id> & found_manifests,
              std::map<file_id, checked_file> & checked_files)
{
  set< hexenc<id> > rosters;

  app.db.get_roster_ids(rosters);
  L(FL("checking %d rosters, manifest pass\n") % rosters.size());

  ticker ticks(_("rosters"), "r", rosters.size()/70+1);

  for (set<hexenc<id> >::const_iterator i = rosters.begin();
       i != rosters.end(); ++i) 
    {

      L(FL("checking roster %s\n") % *i);
      data dat;
      app.db.get_roster(*i, dat);
      checked_rosters[*i].found = true;

      roster_t ros;
      marking_map mm;
      try
        {
          read_roster_and_marking(dat, ros, mm);
        }
      catch (std::logic_error & e)
        {
          L(FL("error parsing roster %s: %s") % *i % e.what());
          checked_rosters[*i].parseable = false;
          continue;
        }
      checked_rosters[*i].parseable = true;
      
      // normalisation check
      {
        hexenc<id> norm_ident;
        data norm_data;
        write_roster_and_marking(ros, mm, norm_data);
        calculate_ident(norm_data, norm_ident);
        if (norm_ident == *i)
          checked_rosters[*i].normalized = true;
      }

      manifest_id man_id;
      calculate_ident(ros, man_id);
      checked_rosters[*i].man_id = man_id;
      found_manifests.insert(man_id);

      for (node_map::const_iterator n = ros.all_nodes().begin();
           n != ros.all_nodes().end(); n++)
        {

          if (is_file_t(n->second))
            {
              file_id fid = downcast_to_file_t(n->second)->content;
              checked_files[fid].roster_refs++;
              if (!checked_files[fid].found)
                checked_rosters[*i].missing_files++;
            }
        }

      ++ticks;
    }
  I(checked_rosters.size() == rosters.size());
}

// second phase of roster checking. examine the marking of a roster, checking
// that the referenced revisions exist.
static void
check_rosters_marking(app_state & app,
              std::map<hexenc<id>, checked_roster> & checked_rosters,
              std::map<revision_id, checked_revision> & checked_revisions)
{
  L(FL("checking %d rosters, marking pass\n") % checked_rosters.size());

  ticker ticks(_("markings"), "m", checked_rosters.size()/70+1);

  for (std::map<hexenc<id>, checked_roster>::const_iterator i 
       = checked_rosters.begin(); i != checked_rosters.end(); i++)
    {
      hexenc<id> ros_id = i->first;
      L(FL("checking roster %s\n") % i->first);
      if (!i->second.parseable)
          continue;

      data dat;
      app.db.get_roster(ros_id, dat);

      roster_t ros;
      marking_map mm;
      read_roster_and_marking(dat, ros, mm);
      
      for (node_map::const_iterator n = ros.all_nodes().begin();
           n != ros.all_nodes().end(); n++)
        {
          // lots of revisions that must exist
          marking_t mark = mm[n->first];
          checked_revisions[mark.birth_revision].marking_refs++;
          if (!checked_revisions[mark.birth_revision].found)
            checked_rosters[ros_id].missing_mark_revs++;

          for (set<revision_id>::const_iterator r = mark.parent_name.begin();
               r != mark.parent_name.end(); r++)
            {
              checked_revisions[*r].marking_refs++;
              if (!checked_revisions[*r].found)
                checked_rosters[ros_id].missing_mark_revs++;
            }

          for (set<revision_id>::const_iterator r = mark.file_content.begin();
               r != mark.file_content.end(); r++)
            {
              checked_revisions[*r].marking_refs++;
              if (!checked_revisions[*r].found)
                checked_rosters[ros_id].missing_mark_revs++;
            }

          for (map<attr_key,set<revision_id> >::const_iterator attr = 
               mark.attrs.begin(); attr != mark.attrs.end(); attr++)
            for (set<revision_id>::const_iterator r = attr->second.begin();
                 r != attr->second.end(); r++)
              {
                checked_revisions[*r].marking_refs++;
                if (!checked_revisions[*r].found)
                  checked_rosters[ros_id].missing_mark_revs++;
              }
        }
      ++ticks;
    }
}

static void 
check_roster_links(app_state & app, 
                   std::map<revision_id, checked_revision> & checked_revisions,
                   std::map<hexenc<id>, checked_roster> & checked_rosters,
                   size_t & unreferenced_roster_links,
                   size_t & missing_rosters)
{
  unreferenced_roster_links = 0;

  std::map<revision_id, hexenc<id> > links;
  app.db.get_roster_links(links);

  for (std::map<revision_id, hexenc<id> >::const_iterator i = links.begin();
       i != links.end(); ++i)
    {
      revision_id rev(i->first);
      hexenc<id> ros(i->second);

      std::map<revision_id, checked_revision>::const_iterator j 
        = checked_revisions.find(rev);
      if (j == checked_revisions.end() || (!j->second.found))
        ++unreferenced_roster_links;

      std::map<hexenc<id>, checked_roster>::const_iterator k 
        = checked_rosters.find(ros);
      if (k == checked_rosters.end() || (!k->second.found))
        ++missing_rosters;
    }
}


static void
check_revisions(app_state & app, 
                std::map<revision_id, checked_revision> & checked_revisions,
                std::map<hexenc<id>, checked_roster> & checked_rosters,
                std::set<manifest_id> const & found_manifests)
{
  std::set<revision_id> revisions;

  app.db.get_revision_ids(revisions);
  L(FL("checking %d revisions\n") % revisions.size());

  ticker ticks(_("revisions"), "r", revisions.size()/70+1);

  for (std::set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i) 
    {
      L(FL("checking revision %s\n") % *i);
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
          L(FL("error parsing revision %s: %s") % *i % e.what());
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

      // roster checks
      if (app.db.roster_link_exists_for_revision(*i))
        {
          hexenc<id> roster_id;
          checked_revisions[*i].found_roster_link = true;
          app.db.get_roster_id_for_revision(*i, roster_id);
          if (app.db.roster_exists_for_revision(*i))
            {
              checked_revisions[*i].found_roster = true;
              I(checked_rosters[roster_id].found);
              checked_rosters[roster_id].revision_refs++;
              if (!(rev.new_manifest == checked_rosters[roster_id].man_id))
                checked_revisions[*i].manifest_mismatch = true;
              if (checked_rosters[roster_id].missing_files > 0)
                checked_revisions[*i].incomplete_roster = true;
            }
        }

      if (found_manifests.find(rev.new_manifest) == found_manifests.end())
        checked_revisions[*i].missing_manifests++;

      for (edge_map::const_iterator edge = rev.edges.begin(); 
           edge != rev.edges.end(); ++edge)
        {
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
  
  L(FL("checked %d revisions after starting with %d\n") 
    % checked_revisions.size()
    % revisions.size());
}

static void
check_ancestry(app_state & app, 
               std::map<revision_id, checked_revision> & checked_revisions)
{
  std::multimap<revision_id, revision_id> graph;

  app.db.get_revision_ancestry(graph);
  L(FL("checking %d ancestry edges\n") % graph.size());

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

  L(FL("checking %d public keys\n") % pubkeys.size());

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

  L(FL("checking %d revision certs\n") % certs.size());

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
  L(FL("checking local history of %d revisions\n") % checked_revisions.size());

  ticker ticks(_("revisions"), "r", 1);

  for (std::map<revision_id, checked_revision>::iterator 
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
  {
    if (i->second.found)
      {
        try 
          {
/*
// FIXME_ROSTERS: disabled until rewritten to use rosters
            check_sane_history(i->first, constants::verify_depth, app);
*/
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
            % i->first % file.roster_refs);
        }

      if (file.roster_refs == 0)
        {
          unreferenced_files++;
          P(F("file %s unreferenced\n") % i->first);
        }

    }
}

static void
report_rosters(std::map<hexenc<id>, checked_roster> const & checked_rosters, 
                 size_t & unreferenced_rosters,
                 size_t & incomplete_rosters,
                 size_t & non_parseable_rosters,
                 size_t & non_normalized_rosters)
{
  for (std::map<hexenc<id>, checked_roster>::const_iterator 
         i = checked_rosters.begin(); i != checked_rosters.end(); ++i)
    {
      checked_roster roster = i->second;

      if (roster.revision_refs == 0)
        {
          unreferenced_rosters++;
          P(F("roster %s unreferenced\n") % i->first);
        }

      if (roster.missing_files > 0)
        {
          incomplete_rosters++;
          P(F("roster %s incomplete (%d missing files)\n") 
            % i->first % roster.missing_files);
        }

      if (roster.missing_mark_revs > 0)
        {
          incomplete_rosters++;
          P(F("roster %s incomplete (%d missing revisions)\n") 
            % i->first % roster.missing_mark_revs);
        }

      if (!roster.parseable)
        {
          non_parseable_rosters++;
          P(F("roster %s is not parseable (perhaps with unnormalized paths?)\n")
            % i->first);
        }

      if (roster.parseable && !roster.normalized)
        {
          non_normalized_rosters++;
          P(F("roster %s is not in normalized form\n")
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
                 size_t & manifest_mismatch,
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
          P(F("revision %s missing (%d revision references; %d cert references; %d parent references; %d child references; %d roster references)\n") 
            % i->first % revision.revision_refs % revision.cert_refs % revision.ancestry_parent_refs
            % revision.ancestry_child_refs % revision.marking_refs);
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

      if (!revision.found_roster_link)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (missing roster link)\n") % i->first);
        }

      if (!revision.found_roster)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (missing roster)\n") % i->first);
        }

      if (revision.manifest_mismatch)
        {
          manifest_mismatch++;
          P(F("revision %s mismatched roster and manifest\n") % i->first);
        }

      if (revision.incomplete_roster)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (incomplete roster)\n") % i->first);
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
          L(FL("key %s signed %d certs\n") 
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
  std::set<manifest_id> found_manifests;
  std::map<hexenc<id>, checked_roster> checked_rosters;
  std::map<revision_id, checked_revision> checked_revisions;
  std::map<rsa_keypair_id, checked_key> checked_keys;

  size_t missing_files = 0;
  size_t unreferenced_files = 0;

  size_t missing_rosters = 0;
  size_t unreferenced_rosters = 0;
  size_t incomplete_rosters = 0;
  size_t non_parseable_rosters = 0;
  size_t non_normalized_rosters = 0;
  size_t unreferenced_roster_links = 0;

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
  size_t manifest_mismatch = 0;
  size_t unchecked_sigs = 0;
  size_t bad_sigs = 0;

  check_files(app, checked_files);
  check_rosters_manifest(app, checked_rosters, checked_revisions, 
                         found_manifests, checked_files);
  check_revisions(app, checked_revisions, checked_rosters, found_manifests);
  check_rosters_marking(app, checked_rosters, checked_revisions);
  check_roster_links(app, checked_revisions, checked_rosters, 
                     unreferenced_roster_links,
                     missing_rosters);
  check_ancestry(app, checked_revisions);
  check_sane(app, checked_revisions);
  check_keys(app, checked_keys);
  check_certs(app, checked_revisions, checked_keys, total_certs);

  report_files(checked_files, missing_files, unreferenced_files);

  report_rosters(checked_rosters, 
                 unreferenced_rosters, 
                 incomplete_rosters,
                 non_parseable_rosters,
                 non_normalized_rosters);
  
  report_revisions(checked_revisions,
                   missing_revisions, incomplete_revisions, 
                   mismatched_parents, mismatched_children,
                   manifest_mismatch,
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

  if (unreferenced_rosters > 0) 
    W(F("%d unreferenced rosters\n") % unreferenced_rosters);
  if (incomplete_rosters > 0)
    W(F("%d incomplete rosters\n") % incomplete_rosters);
  if (non_parseable_rosters > 0)
    W(F("%d rosters not parseable (perhaps with invalid paths)\n")
      % non_parseable_rosters);
  if (non_normalized_rosters > 0)
    W(F("%d rosters not in normalized form\n") % non_normalized_rosters);

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


  if (unreferenced_roster_links > 0)
    W(F("%d unreferenced roster links\n") % unreferenced_roster_links);

  if (missing_rosters > 0)
    W(F("%d missing rosters\n") % missing_rosters);


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
    unreferenced_rosters + incomplete_rosters +
    non_parseable_rosters + non_normalized_rosters +
    missing_revisions + incomplete_revisions + 
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children +
    bad_history +
    unreferenced_roster_links + missing_rosters +
    missing_certs + mismatched_certs +
    unchecked_sigs + bad_sigs +
    missing_keys;
  // unreferenced files and rosters and mismatched certs are not actually
  // serious errors; odd, but nothing will break.
  size_t serious = missing_files + 
    incomplete_rosters + missing_rosters +
    non_parseable_rosters + non_normalized_rosters +
    missing_revisions + incomplete_revisions + 
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children + manifest_mismatch +
    bad_history +
    missing_certs +
    unchecked_sigs + bad_sigs +
    missing_keys;

  P(F("check complete: %d files; %d rosters; %d revisions; %d keys; %d certs\n")
    % checked_files.size()
    % checked_rosters.size()
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
