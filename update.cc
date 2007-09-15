// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <set>
#include <map>
#include "vector.hh"
#include "lexical_cast.hh"

#include "database.hh"
#include "sanity.hh"
#include "cert.hh"
#include "project.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"
#include "revision.hh"

// these functions just encapsulate the (somewhat complex) logic behind
// picking an update target. the actual updating takes place in
// commands.cc, along with most other file-modifying actions.

// the algorithm is:
//   - do a depth-first traversal of the current revision's descendent set
//   - for each revision, check to see whether it is
//     - in the correct branch
//     - has acceptable test results
//     and add it to the candidate set if so
//   - this gives a set containing every descendent that we might want to
//     update to.
//   - run erase_ancestors on that set, to get just the heads
//   - If there are any non-suspended revisions in the set, then remove the
//     suspended revisions.
// the idea is that this should be correct even in the presence of
// discontinuous branches, test results that go from good to bad to good to
// bad to good, etc.
// it may be somewhat inefficient to use erase_ancestors here, but deal with
// that when and if the time comes...

using std::make_pair;
using std::map;
using std::set;
using std::vector;

using boost::lexical_cast;

static void
get_test_results_for_revision(revision_id const & id,
                              map<rsa_keypair_id, bool> & results,
                              database & db)
{
  vector< revision<cert> > certs;
  db.get_project().get_revision_certs_by_name(id, cert_name(testresult_cert_name), certs);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value cv;
      decode_base64(i->inner().value, cv);
      try
        {
          bool test_ok = lexical_cast<bool>(cv());
          results.insert(make_pair(i->inner().key, test_ok));
        }
      catch(boost::bad_lexical_cast &)
        {
          W(F("failed to decode boolean testresult cert value '%s'") % cv);
        }
    }
}

static bool
acceptable_descendent(branch_name const & branch,
                      revision_id const & base,
                      map<rsa_keypair_id, bool> & base_results,
                      revision_id const & target,
                      database & db)
{
  L(FL("Considering update target %s") % target);

  // step 1: check the branch
  if (!db.get_project().revision_is_in_branch(target, branch))
    {
      L(FL("%s not in branch %s") % target % branch);
      return false;
    }

  // step 2: check the testresults
  map<rsa_keypair_id, bool> target_results;
  get_test_results_for_revision(target, target_results, db);
  if (db.hook_accept_testresult_change(base_results, target_results))
    {
      L(FL("%s is acceptable update candidate") % target);
      return true;
    }
  else
    {
      L(FL("%s has unacceptable test results") % target);
      return false;
    }
}

namespace
{
  struct suspended_in_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    suspended_in_branch(database & db,
                  base64<cert_value> const & branch_encoded)
      : db(db), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(suspend_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs, db);
      return !certs.empty();
    }
  };
}


static void
calculate_update_set(revision_id const & base,
                     branch_name const & branch,
                     database & db,
                     set<revision_id> & candidates)
{
  map<rsa_keypair_id, bool> base_results;
  get_test_results_for_revision(base, base_results, db);

  candidates.clear();
  // we possibly insert base into the candidate set as well; returning a set
  // containing just it means that we are up to date; returning an empty set
  // means that there is no acceptable update.
  if (acceptable_descendent(branch, base, base_results, base, db))
    candidates.insert(base);

  // keep a visited set to avoid repeating work
  set<revision_id> visited;
  set<revision_id> children;
  vector<revision_id> to_traverse;

  db.get_revision_children(base, children);
  copy(children.begin(), children.end(), back_inserter(to_traverse));

  while (!to_traverse.empty())
    {
      revision_id target = to_traverse.back();
      to_traverse.pop_back();

      // If we've traversed this id before via a different path, skip it.
      if (visited.find(target) != visited.end())
        continue;
      visited.insert(target);

      // then, possibly insert this revision as a candidate
      if (acceptable_descendent(branch, base, base_results, target, db))
        candidates.insert(target);

      // and traverse its children as well
      db.get_revision_children(target, children);
      copy(children.begin(), children.end(), back_inserter(to_traverse));
    }

  erase_ancestors(candidates, db);
  
  bool have_non_suspended_rev = false;
  
  for (set<revision_id>::const_iterator it = candidates.begin(); it != candidates.end(); it++)
    {
      if (!db.get_project().revision_is_suspended_in_branch(*it, branch))
        {
          have_non_suspended_rev = true;
          break;
        }
    }
  if (!db.get_opt_ignore_suspend_certs() && have_non_suspended_rev)
    {
      // remove all suspended revisions
      base64<cert_value> branch_encoded;
      encode_base64(cert_value(branch()), branch_encoded);
      suspended_in_branch s(db, branch_encoded);
      for(std::set<revision_id>::iterator it = candidates.begin(); it != candidates.end(); it++)
        if (s(*it))
          candidates.erase(*it);
    }
}


void pick_update_candidates(revision_id const & base_ident,
                            database & db,
                            set<revision_id> & candidates)
{
  branch_name const & branchname = db.get_opt_branchname();
  N(branchname() != "",
    F("cannot determine branch for update"));
  I(!null_id(base_ident));

  calculate_update_set(base_ident, branchname,
                       db, candidates);
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
