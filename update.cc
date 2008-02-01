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
#include "safe_map.hh"
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
#include "lua_hooks.hh"

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
                              database & db, project_t & project)
{
  vector< revision<cert> > certs;
  project.get_revision_certs_by_name(id, cert_name(testresult_cert_name),
                                     certs);
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
                      database & db, project_t & project,
                      lua_hooks & lua)
{
  L(FL("Considering update target %s") % target);

  // step 1: check the branch
  if (!project.revision_is_in_branch(target, branch))
    {
      L(FL("%s not in branch %s") % target % branch);
      return false;
    }

  // step 2: check the testresults
  map<rsa_keypair_id, bool> target_results;
  get_test_results_for_revision(target, target_results, db, project);
  if (lua.hook_accept_testresult_change(base_results, target_results))
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

void
pick_update_candidates(set<revision_id> & candidates,
                       revision_id const & base,
                       branch_name const & branch,
                       bool ignore_suspend_certs,
                       database & db, project_t & project,
                       lua_hooks & lua)
{
  I(!null_id(base));
  I(!branch().empty());

  map<rsa_keypair_id, bool> base_results;
  get_test_results_for_revision(base, base_results, db, project);

  candidates.clear();
  // we possibly insert base into the candidate set as well; returning a set
  // containing just it means that we are up to date; returning an empty set
  // means that there is no acceptable update.
  if (acceptable_descendent(branch, base, base_results, base, db, project, lua))
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
      if (acceptable_descendent(branch, base, base_results,
                                target, db, project, lua))
        candidates.insert(target);

      // and traverse its children as well
      db.get_revision_children(target, children);
      copy(children.begin(), children.end(), back_inserter(to_traverse));
    }

  erase_ancestors(candidates, db);

  if (ignore_suspend_certs)
    return;
  
   set<revision_id> active_candidates;
   for (set<revision_id>::const_iterator i = candidates.begin();
        i != candidates.end(); i++)
     if (!project.revision_is_suspended_in_branch(*i, branch))
       safe_insert(active_candidates, *i);

   if (!active_candidates.empty())
     candidates = active_candidates;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
