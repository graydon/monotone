// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <map>
#include <vector>
#include <boost/lexical_cast.hpp>

#include "app_state.hh"
#include "database.hh"
#include "sanity.hh"
#include "cert.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"

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
//   - run erase_ancestors on that set, to get just the heads; this is our
//     real candidate set.
// the idea is that this should be correct even in the presence of
// discontinuous branches, test results that go from good to bad to good to
// bad to good, etc.
// it may be somewhat inefficient to use erase_ancestors here, but deal with
// that when and if the time comes...

using boost::lexical_cast;

using namespace std;

static void 
get_test_results_for_revision(revision_id const & id,
                              map<rsa_keypair_id, bool> & results,
                              app_state & app)
{
  vector< revision<cert> > certs;
  app.db.get_revision_certs(id, testresult_cert_name, certs);
  erase_bogus_certs(certs, app);
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
      catch(boost::bad_lexical_cast & e)
        {
          W(F("failed to decode boolean testresult cert value '%s'\n") % cv);
        }
    }
}

static bool
acceptable_descendent(cert_value const & branch,
                      revision_id const & base,
                      map<rsa_keypair_id, bool> & base_results,
                      revision_id const & target,
                      app_state & app)
{
  L(F("Considering update target %s\n") % target);
  
  // step 1: check the branch
  base64<cert_value> val;
  encode_base64(branch, val);
  vector< revision<cert> > certs;
  app.db.get_revision_certs(target, branch_cert_name, val, certs);
  erase_bogus_certs(certs, app);
  if (certs.empty())
    {
      L(F("%s not in branch %s\n") % target % branch);
      return false;
    }
  
  // step 2: check the testresults
  map<rsa_keypair_id, bool> target_results;
  get_test_results_for_revision(target, target_results, app);
  if (app.lua.hook_accept_testresult_change(base_results, target_results))
    {
      L(F("%s is acceptable update candidate\n") % target);
      return true;
    }
  else
    {
      L(F("%s has unacceptable test results\n") % target);
      return false;
    }
}
      

static void
calculate_update_set(revision_id const & base,
                     cert_value const & branch,
                     app_state & app,
                     set<revision_id> & candidates)
{
  map<rsa_keypair_id, bool> base_results;
  get_test_results_for_revision(base, base_results, app);

  candidates.clear();
  // we possibly insert base into the candidate set as well; returning a set
  // containing just it means that we are up to date; returning an empty set
  // means that there is no acceptable update.
  if (acceptable_descendent(branch, base, base_results, base, app))
    candidates.insert(base);

  // keep a visited set to avoid repeating work
  set<revision_id> visited;
  set<revision_id> children;
  vector<revision_id> to_traverse;

  app.db.get_revision_children(base, children);
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
      if (acceptable_descendent(branch, base, base_results, target, app))
        candidates.insert(target);

      // and traverse its children as well
      app.db.get_revision_children(target, children);
      copy(children.begin(), children.end(), back_inserter(to_traverse));
    }

  erase_ancestors(candidates, app);
}
  
  
void pick_update_candidates(revision_id const & base_ident,
                            app_state & app,
                            set<revision_id> & candidates)
{
  N(app.branch_name() != "",
    F("cannot determine branch for update"));
  I(!null_id(base_ident));

  calculate_update_set(base_ident, cert_value(app.branch_name()),
                       app, candidates);
}
  

