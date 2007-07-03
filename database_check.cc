// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
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

#include "app_state.hh"
#include "constants.hh"
#include "database_check.hh"
#include "keys.hh"
#include "revision.hh"
#include "ui.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "cert.hh"
#include "rev_height.hh"

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

// FIXME: add a test that for each revision, generates that rev's roster
// from scratch, and compares it to the one stored in the db.  (Do the
// comparison using something like equal_up_to_renumbering, except should
// say if (!temp_node(a) && !temp_node(b)) I(a == b).)

using std::logic_error;
using std::map;
using std::multimap;
using std::set;
using std::string;
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

  manifest_id man_id;   // manifest id of this roster's public part

  checked_roster():
    found(false), revision_refs(0),
    missing_files(0), missing_mark_revs(0),
    man_id() {}
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

  bool found_roster;           // the roster for this revision exists
  bool manifest_mismatch;      // manifest doesn't match the roster for this revision
  bool incomplete_roster;      // the roster for this revision is missing files
  size_t missing_manifests;    // number of missing manifests referenced by this revision
  size_t missing_revisions;    // number of missing revisions referenced by this revision

  size_t cert_refs;            // number of references to this revision by revision certs;

  bool parseable;              // read_revision does not throw
  bool normalized;             // write_revision( read_revision(dat) ) == dat

  string history_error;

  set<revision_id> parents;
  vector<checked_cert> checked_certs;

  checked_revision():
    found(false),
    revision_refs(0), ancestry_parent_refs(0), ancestry_child_refs(0),
    marking_refs(0),
    found_roster(false), manifest_mismatch(false), incomplete_roster(false),
    missing_manifests(0), missing_revisions(0),
    cert_refs(0), parseable(false), normalized(false) {}
};

struct checked_height {
  bool found;                  // found in db
  bool unique;                 // not identical to any height retrieved earlier
  bool sensible;               // greater than all parent heights
  checked_height(): found(false), unique(false), sensible(true) {}
};

/*
 * check integrity of the SQLite database
 */
static void
check_db_integrity_check(app_state & app )
{
    L(FL("asking sqlite to check db integrity"));
    E(app.db.check_integrity(),
      F("file structure is corrupted; cannot check further"));
}

static void
check_files(app_state & app, map<file_id, checked_file> & checked_files)
{
  set<file_id> files;

  app.db.get_file_ids(files);
  L(FL("checking %d files") % files.size());

  ticker ticks(_("files"), "f", files.size()/70+1);

  for (set<file_id>::const_iterator i = files.begin();
       i != files.end(); ++i)
    {
      L(FL("checking file %s") % *i);
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
                       map<revision_id, checked_roster> & checked_rosters,
                       map<revision_id, checked_revision> & checked_revisions,
                       set<manifest_id> & found_manifests,
                       map<file_id, checked_file> & checked_files)
{
  set<revision_id> rosters;

  app.db.get_roster_ids(rosters);
  L(FL("checking %d rosters, manifest pass") % rosters.size());
  
  ticker ticks(_("rosters"), "r", rosters.size()/70+1);
  
  for (set<revision_id>::const_iterator i = rosters.begin();
       i != rosters.end(); ++i)
    {
      L(FL("checking roster %s") % *i);

      roster_t ros;
      marking_map mm;
      try
        {
          app.db.get_roster(*i, ros, mm);
        }
      // When attempting to fetch a roster with no corresponding revision,
      // we fail with E(), not I() (when it tries to look up the manifest_id
      // to check).  std::exception catches both informative_failure's and
      // logic_error's.
      catch (std::exception & e)
        {
          L(FL("error loading roster %s: %s") % *i % e.what());
          checked_rosters[*i].found = false;
          continue;
        }
      checked_rosters[*i].found = true;

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

// Second phase of roster checking. examine the marking of a roster, checking
// that the referenced revisions exist.
// This function assumes that check_revisions has been called!
static void
check_rosters_marking(app_state & app,
              map<revision_id, checked_roster> & checked_rosters,
              map<revision_id, checked_revision> & checked_revisions)
{
  L(FL("checking %d rosters, marking pass") % checked_rosters.size());

  ticker ticks(_("markings"), "m", checked_rosters.size()/70+1);

  for (map<revision_id, checked_roster>::const_iterator i
       = checked_rosters.begin(); i != checked_rosters.end(); i++)
    {
      revision_id ros_id = i->first;
      L(FL("checking roster %s") % i->first);
      if (!i->second.found)
          continue;

      // skip marking check on unreferenced rosters -- they're left by
      // kill_rev_locally, and not expected to have everything they
      // reference existing
      if (!i->second.revision_refs)
        continue;

      roster_t ros;
      marking_map mm;
      app.db.get_roster(ros_id, ros, mm);

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
check_revisions(app_state & app,
                map<revision_id, checked_revision> & checked_revisions,
                map<revision_id, checked_roster> & checked_rosters,
                set<manifest_id> const & found_manifests,
                size_t & missing_rosters)
{
  set<revision_id> revisions;

  app.db.get_revision_ids(revisions);
  L(FL("checking %d revisions") % revisions.size());

  ticker ticks(_("revisions"), "r", revisions.size()/70+1);

  for (set<revision_id>::const_iterator i = revisions.begin();
       i != revisions.end(); ++i)
    {
      L(FL("checking revision %s") % *i);
      revision_data data;
      app.db.get_revision(*i, data);
      checked_revisions[*i].found = true;

      revision_t rev;
      try
        {
          read_revision(data, rev);
        }
      catch (logic_error & e)
        {
          L(FL("error parsing revision %s: %s") % *i % e.what());
          checked_revisions[*i].parseable = false;
          continue;
        }
      checked_revisions[*i].parseable = true;

      // normalisation check
      revision_id norm_ident;
      revision_data norm_data;
      write_revision(rev, norm_data);
      calculate_ident(norm_data, norm_ident);
      if (norm_ident == *i)
          checked_revisions[*i].normalized = true;

      // roster checks
      if (app.db.roster_version_exists(*i))
        {
          checked_revisions[*i].found_roster = true;
          I(checked_rosters[*i].found);
          checked_rosters[*i].revision_refs++;
          if (!(rev.new_manifest == checked_rosters[*i].man_id))
            checked_revisions[*i].manifest_mismatch = true;
          if (checked_rosters[*i].missing_files > 0)
            checked_revisions[*i].incomplete_roster = true;
        }
      else
        ++missing_rosters;

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

  for (map<revision_id, checked_revision>::iterator
         revision = checked_revisions.begin();
       revision != checked_revisions.end(); ++revision)
    {
      for (set<revision_id>::const_iterator p = revision->second.parents.begin();
           p != revision->second.parents.end(); ++p)
        {
          if (!checked_revisions[*p].found)
            revision->second.missing_revisions++;
        }
    }

  L(FL("checked %d revisions after starting with %d")
    % checked_revisions.size()
    % revisions.size());
}

static void
check_ancestry(app_state & app,
               map<revision_id, checked_revision> & checked_revisions)
{
  multimap<revision_id, revision_id> graph;

  app.db.get_revision_ancestry(graph);
  L(FL("checking %d ancestry edges") % graph.size());

  ticker ticks(_("ancestry"), "a", graph.size()/70+1);

  // checked revision has set of parents
  // graph has revision and associated parents
  // these two representations of the graph should agree!

  set<revision_id> seen;
  for (multimap<revision_id, revision_id>::const_iterator i = graph.begin();
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
           map<rsa_keypair_id, checked_key> & checked_keys)
{
  vector<rsa_keypair_id> pubkeys;

  app.db.get_public_keys(pubkeys);

  L(FL("checking %d public keys") % pubkeys.size());

  ticker ticks(_("keys"), "k", 1);

  for (vector<rsa_keypair_id>::const_iterator i = pubkeys.begin();
       i != pubkeys.end(); ++i)
    {
      app.db.get_key(*i, checked_keys[*i].pub_encoded);
      checked_keys[*i].found = true;
      ++ticks;
    }

}

static void
check_certs(app_state & app,
            map<revision_id, checked_revision> & checked_revisions,
            map<rsa_keypair_id, checked_key> & checked_keys,
            size_t & total_certs)
{

  vector< revision<cert> > certs;
  app.db.get_revision_certs(certs);

  total_certs = certs.size();

  L(FL("checking %d revision certs") % certs.size());

  ticker ticks(_("certs"), "c", certs.size()/70+1);

  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      checked_cert checked(*i);
      checked.found_key = checked_keys[i->inner().key].found;

      if (checked.found_key)
        {
          string signed_text;
          cert_signable_text(i->inner(), signed_text);
          checked.good_sig = check_signature(app, i->inner().key,
                                             checked_keys[i->inner().key].pub_encoded,
                                             signed_text, i->inner().sig);
        }

      checked_keys[i->inner().key].sigs++;
      checked_revisions[revision_id(i->inner().ident)].checked_certs.push_back(checked);

      ++ticks;
    }
}

// - check that every rev has a height
// - check that no two revs have the same height
static void
check_heights(app_state & app,
              map<revision_id, checked_height> & checked_heights)
{
  set<revision_id> heights;
  app.db.get_revision_ids(heights);

  // add revision [], it is the (imaginary) root of all revisions, and
  // should have a height, too
  {
    revision_id null_id;
    heights.insert(null_id);
  }
  
  L(FL("checking %d heights") % heights.size());

  set<rev_height> seen;

  ticker ticks(_("heights"), "h", heights.size()/70+1);

  for (set<revision_id>::const_iterator i = heights.begin();
       i != heights.end(); ++i)
    {
      L(FL("checking height for %s") % *i);
      
      rev_height h;
      try
        {
          app.db.get_rev_height(*i, h);
        }
      catch (std::exception & e)
        {
          L(FL("error loading height: %s") % e.what());
          continue;
        }
      checked_heights[*i].found = true; // defaults to false

      if (seen.find(h) != seen.end())
        {
          L(FL("error: height not unique: %s") % h());
          continue;
        }
      checked_heights[*i].unique = true; // defaults to false
      seen.insert(h);

      ++ticks;
    }
}

// check that every rev's height is a sensible height to assign, given its
// parents
static void
check_heights_relation(app_state & app,
                       map<revision_id, checked_height> & checked_heights)
{
  set<revision_id> heights;

  multimap<revision_id, revision_id> graph; // parent, child
  app.db.get_revision_ancestry(graph);

  L(FL("checking heights for %d edges") % graph.size());

  ticker ticks(_("height relations"), "h", graph.size()/70+1);

  typedef multimap<revision_id, revision_id>::const_iterator gi;
  for (gi i = graph.begin(); i != graph.end(); ++i)
    {
      revision_id const & p_id = i->first;
      revision_id const & c_id = i->second;

      if (!checked_heights[p_id].found || !checked_heights[c_id].found)
        {
          L(FL("missing height(s), skipping edge %s -> %s") % p_id % c_id);
          continue;
        }

      L(FL("checking heights for edges %s -> %s") %
        p_id % c_id);
      
      rev_height parent, child;
      app.db.get_rev_height(p_id, parent);
      app.db.get_rev_height(c_id, child);

      if (!(child > parent))
        {
          L(FL("error: height %s of child %s not greater than height %s of parent %s")
            % child % c_id % parent % p_id);
          checked_heights[c_id].sensible = false; // defaults to true
          continue;
        }

      ++ticks;
    }
}

static void
report_files(map<file_id, checked_file> const & checked_files,
             size_t & missing_files,
             size_t & unreferenced_files)
{
  for (map<file_id, checked_file>::const_iterator
         i = checked_files.begin(); i != checked_files.end(); ++i)
    {
      checked_file file = i->second;

      if (!file.found)
        {
          missing_files++;
          P(F("file %s missing (%d manifest references)")
            % i->first % file.roster_refs);
        }

      if (file.roster_refs == 0)
        {
          unreferenced_files++;
          P(F("file %s unreferenced") % i->first);
        }

    }
}

static void
report_rosters(map<revision_id, checked_roster> const & checked_rosters,
                 size_t & unreferenced_rosters,
                 size_t & incomplete_rosters)
{
  for (map<revision_id, checked_roster>::const_iterator
         i = checked_rosters.begin(); i != checked_rosters.end(); ++i)
    {
      checked_roster roster = i->second;

      if (roster.revision_refs == 0)
        {
          unreferenced_rosters++;
          P(F("roster %s unreferenced") % i->first);
        }

      if (roster.missing_files > 0)
        {
          incomplete_rosters++;
          P(F("roster %s incomplete (%d missing files)")
            % i->first % roster.missing_files);
        }

      if (roster.missing_mark_revs > 0)
        {
          incomplete_rosters++;
          P(F("roster %s incomplete (%d missing revisions)")
            % i->first % roster.missing_mark_revs);
        }
    }
}

static void
report_revisions(map<revision_id, checked_revision> const & checked_revisions,
                 size_t & missing_revisions,
                 size_t & incomplete_revisions,
                 size_t & mismatched_parents,
                 size_t & mismatched_children,
                 size_t & manifest_mismatch,
                 size_t & bad_history,
                 size_t & non_parseable_revisions,
                 size_t & non_normalized_revisions)
{
  for (map<revision_id, checked_revision>::const_iterator
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
    {
      checked_revision revision = i->second;

      if (!revision.found)
        {
          missing_revisions++;
          P(F("revision %s missing (%d revision references; %d cert references; %d parent references; %d child references; %d roster references)")
            % i->first % revision.revision_refs % revision.cert_refs % revision.ancestry_parent_refs
            % revision.ancestry_child_refs % revision.marking_refs);
        }

      if (revision.missing_manifests > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing manifests)")
            % i->first % revision.missing_manifests);
        }

      if (revision.missing_revisions > 0)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (%d missing revisions)")
            % i->first % revision.missing_revisions);
        }

      if (!revision.found_roster)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (missing roster)") % i->first);
        }

      if (revision.manifest_mismatch)
        {
          manifest_mismatch++;
          P(F("revision %s mismatched roster and manifest") % i->first);
        }

      if (revision.incomplete_roster)
        {
          incomplete_revisions++;
          P(F("revision %s incomplete (incomplete roster)") % i->first);
        }

      if (revision.ancestry_parent_refs != revision.revision_refs)
        {
          mismatched_parents++;
          P(F("revision %s mismatched parents (%d ancestry parents; %d revision refs)")
            % i->first
            % revision.ancestry_parent_refs
            % revision.revision_refs );
        }

      if (revision.ancestry_child_refs != revision.parents.size())
        {
          mismatched_children++;
          P(F("revision %s mismatched children (%d ancestry children; %d parents)")
            % i->first
            % revision.ancestry_child_refs
            % revision.parents.size() );
        }

      if (!revision.history_error.empty())
        {
          bad_history++;
          string tmp = revision.history_error;
          if (tmp[tmp.length() - 1] == '\n')
            tmp.erase(tmp.length() - 1);
          P(F("revision %s has bad history (%s)")
            % i->first % tmp);
        }

      if (!revision.parseable)
        {
          non_parseable_revisions++;
          P(F("revision %s is not parseable (perhaps with unnormalized paths?)")
            % i->first);
        }

      if (revision.parseable && !revision.normalized)
        {
          non_normalized_revisions++;
          P(F("revision %s is not in normalized form")
            % i->first);
        }
    }
}

static void
report_keys(map<rsa_keypair_id, checked_key> const & checked_keys,
            size_t & missing_keys)
{
  for (map<rsa_keypair_id, checked_key>::const_iterator
         i = checked_keys.begin(); i != checked_keys.end(); ++i)
    {
      checked_key key = i->second;

      if (key.found)
        {
          L(FL("key %s signed %d certs")
            % i->first
            % key.sigs);
        }
      else
        {
          missing_keys++;
          P(F("key %s missing (signed %d certs)")
            % i->first
            % key.sigs);
        }
    }
}

static void
report_certs(map<revision_id, checked_revision> const & checked_revisions,
             size_t & missing_certs,
             size_t & mismatched_certs,
             size_t & unchecked_sigs,
             size_t & bad_sigs)
{
  set<cert_name> cnames;

  cnames.insert(cert_name(author_cert_name));
  cnames.insert(cert_name(branch_cert_name));
  cnames.insert(cert_name(changelog_cert_name));
  cnames.insert(cert_name(date_cert_name));

  for (map<revision_id, checked_revision>::const_iterator
         i = checked_revisions.begin(); i != checked_revisions.end(); ++i)
    {
      checked_revision revision = i->second;
      map<cert_name, size_t> cert_counts;

      for (vector<checked_cert>::const_iterator checked = revision.checked_certs.begin();
           checked != revision.checked_certs.end(); ++checked)
        {
          if (!checked->found_key)
            {
              unchecked_sigs++;
              P(F("revision %s unchecked signature in %s cert from missing key %s")
                % i->first
                % checked->rcert.inner().name
                % checked->rcert.inner().key);
            }
          else if (!checked->good_sig)
            {
              bad_sigs++;
              P(F("revision %s bad signature in %s cert from key %s")
                % i->first
                % checked->rcert.inner().name
                % checked->rcert.inner().key);
            }

          cert_counts[checked->rcert.inner().name]++;
        }

      for (set<cert_name>::const_iterator n = cnames.begin();
           n != cnames.end(); ++n)
        {
          if (revision.found && cert_counts[*n] == 0)
            {
              missing_certs++;
              P(F("revision %s missing %s cert") % i->first % *n);
            }
        }

      if (cert_counts[cert_name(author_cert_name)] != cert_counts[cert_name(changelog_cert_name)] ||
          cert_counts[cert_name(author_cert_name)] != cert_counts[cert_name(date_cert_name)] ||
          cert_counts[cert_name(date_cert_name)]   != cert_counts[cert_name(changelog_cert_name)])
        {
          mismatched_certs++;
          P(F("revision %s mismatched certs (%d authors %d dates %d changelogs)")
            % i->first
            % cert_counts[cert_name(author_cert_name)]
            % cert_counts[cert_name(date_cert_name)]
            % cert_counts[cert_name(changelog_cert_name)]);
        }

    }
}

static void
report_heights(map<revision_id, checked_height> const & checked_heights,
               size_t & missing_heights,
               size_t & duplicate_heights,
               size_t & incorrect_heights)
{
  for (map<revision_id, checked_height>::const_iterator
         i = checked_heights.begin(); i != checked_heights.end(); ++i)
    {
      checked_height height = i->second;

      if (!height.found)
        {
          missing_heights++;
          P(F("height missing for revision %s") % i->first);
          continue;
        }

      if (!height.unique)
        {
          duplicate_heights++;
          P(F("duplicate height for revision %s") % i->first);
        }

      if (!height.sensible)
        {
          incorrect_heights++;
          P(F("height of revision %s not greater than that of parent") % i->first);
        }
    }
}

void
check_db(app_state & app)
{
  map<file_id, checked_file> checked_files;
  set<manifest_id> found_manifests;
  map<revision_id, checked_roster> checked_rosters;
  map<revision_id, checked_revision> checked_revisions;
  map<rsa_keypair_id, checked_key> checked_keys;
  map<revision_id, checked_height> checked_heights;

  size_t missing_files = 0;
  size_t unreferenced_files = 0;

  size_t missing_rosters = 0;
  size_t unreferenced_rosters = 0;
  size_t incomplete_rosters = 0;

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

  size_t missing_heights = 0;
  size_t duplicate_heights = 0;
  size_t incorrect_heights = 0;

  transaction_guard guard(app.db, false);

  check_db_integrity_check(app);
  check_files(app, checked_files);
  check_rosters_manifest(app, checked_rosters, checked_revisions,
                         found_manifests, checked_files);
  check_revisions(app, checked_revisions, checked_rosters, found_manifests,
                  missing_rosters);
  check_rosters_marking(app, checked_rosters, checked_revisions);
  check_ancestry(app, checked_revisions);
  check_keys(app, checked_keys);
  check_certs(app, checked_revisions, checked_keys, total_certs);
  check_heights(app, checked_heights);
  check_heights_relation(app, checked_heights);

  report_files(checked_files, missing_files, unreferenced_files);

  report_rosters(checked_rosters,
                 unreferenced_rosters,
                 incomplete_rosters);

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

  report_heights(checked_heights,
                 missing_heights, duplicate_heights, incorrect_heights);

  // NOTE: any new sorts of problems need to have added:
  //   -- a message here, that tells the use about them
  //   -- entries in one _or both_ of the sums calculated at the end
  //   -- an entry added to the manual, which describes in detail why the
  //      error occurs and what it means to the user

  if (missing_files > 0)
    W(F("%d missing files") % missing_files);
  if (unreferenced_files > 0)
    W(F("%d unreferenced files") % unreferenced_files);

  if (unreferenced_rosters > 0)
    W(F("%d unreferenced rosters") % unreferenced_rosters);
  if (incomplete_rosters > 0)
    W(F("%d incomplete rosters") % incomplete_rosters);

  if (missing_revisions > 0)
    W(F("%d missing revisions") % missing_revisions);
  if (incomplete_revisions > 0)
    W(F("%d incomplete revisions") % incomplete_revisions);
  if (mismatched_parents > 0)
    W(F("%d mismatched parents") % mismatched_parents);
  if (mismatched_children > 0)
    W(F("%d mismatched children") % mismatched_children);
  if (bad_history > 0)
    W(F("%d revisions with bad history") % bad_history);
  if (non_parseable_revisions > 0)
    W(F("%d revisions not parseable (perhaps with invalid paths)")
      % non_parseable_revisions);
  if (non_normalized_revisions > 0)
    W(F("%d revisions not in normalized form") % non_normalized_revisions);


  if (missing_rosters > 0)
    W(F("%d missing rosters") % missing_rosters);


  if (missing_keys > 0)
    W(F("%d missing keys") % missing_keys);

  if (missing_certs > 0)
    W(F("%d missing certs") % missing_certs);
  if (mismatched_certs > 0)
    W(F("%d mismatched certs") % mismatched_certs);
  if (unchecked_sigs > 0)
    W(F("%d unchecked signatures due to missing keys") % unchecked_sigs);
  if (bad_sigs > 0)
    W(F("%d bad signatures") % bad_sigs);

  if (missing_heights > 0)
    W(F("%d missing heights") % missing_heights);
  if (duplicate_heights > 0)
    W(F("%d duplicate heights") % duplicate_heights);
  if (incorrect_heights > 0)
    W(F("%d incorrect heights") % incorrect_heights);

  size_t total = missing_files + unreferenced_files +
    unreferenced_rosters + incomplete_rosters +
    missing_revisions + incomplete_revisions +
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children +
    bad_history +
    missing_rosters +
    missing_certs + mismatched_certs +
    unchecked_sigs + bad_sigs +
    missing_keys +
    missing_heights + duplicate_heights + incorrect_heights;
  // unreferenced files and rosters and mismatched certs are not actually
  // serious errors; odd, but nothing will break.
  size_t serious = missing_files +
    incomplete_rosters + missing_rosters +
    missing_revisions + incomplete_revisions +
    non_parseable_revisions + non_normalized_revisions +
    mismatched_parents + mismatched_children + manifest_mismatch +
    bad_history +
    missing_certs +
    unchecked_sigs + bad_sigs +
    missing_keys +
    missing_heights + duplicate_heights + incorrect_heights;

  P(F("check complete: %d files; %d rosters; %d revisions; %d keys; %d certs; %d heights")
    % checked_files.size()
    % checked_rosters.size()
    % checked_revisions.size()
    % checked_keys.size()
    % total_certs
    % checked_heights.size());
  P(F("total problems detected: %d (%d serious)") % total % serious);
  if (serious)
    E(false, F("serious problems detected"));
  else if (total)
    P(F("minor problems detected"));
  else
    P(F("database is good"));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
