
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <map>
#include <vector>
#include <string>
#include <boost/lexical_cast.hpp>

#include "app_state.hh"
#include "database.hh"
#include "manifest.hh"
#include "sanity.hh"
#include "cert.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"

// these functions just encapsulate the (somewhat complex) logic behind
// picking an update target. the actual updating takes place in
// commands.cc, along with most other file-modifying actions.

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

static void 
find_deepest_acceptable_descendent(revision_id const & base,
                                   cert_value const & branch,
                                   app_state & app,
                                   set<revision_id> & chosen)
{
  chosen.clear();
  chosen.insert(base);

  set<revision_id> visited;
  set<revision_id> frontier = chosen;
  while(! frontier.empty())
    {     
      set<revision_id> next_frontier;
      set<revision_id> selected_children;

      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        {
          // quick check to prevent cycles
          if (visited.find(*i) != visited.end())
            continue;
          visited.insert(*i);

          // step 1: get children of i
          base64<cert_value> val;
          vector< revision<cert> > certs;
          set<revision_id> children;

          app.db.get_revision_children(*i, children);

          // step 2: weed out edges which cross a branch boundary
          set<revision_id> same_branch_children;
          encode_base64(branch, val);
          
          for (set<revision_id>::const_iterator j = children.begin();
               j != children.end(); ++j)
            {
              app.db.get_revision_certs(*j, branch_cert_name, val, certs);
              erase_bogus_certs(certs, app);
              if (certs.empty())
                W(F("update edge %s -> %s ignored since it exits branch %s\n")
                  % *i % *j % branch);
              else
                same_branch_children.insert(*j);
            }

          copy(same_branch_children.begin(), same_branch_children.end(),
               inserter(next_frontier, next_frontier.begin()));

          // step 3: pull aside acceptable testresult changes
          map<rsa_keypair_id, bool> old_results;
          get_test_results_for_revision(*i, old_results, app);
          for (set<revision_id>::const_iterator j = same_branch_children.begin();
               j != same_branch_children.end(); ++j)
            {
              map<rsa_keypair_id, bool> new_results;
              get_test_results_for_revision(*j, new_results, app);
              if (app.lua.hook_accept_testresult_change(old_results, new_results))
                selected_children.insert(*j);
              else
                W(F("update edge %s -> %s ignored due to unacceptable testresults\n")
                  % *i % *j);
            }
        }

      if (! selected_children.empty())
        chosen = selected_children;

      frontier = next_frontier;
    }
}

void pick_update_target(revision_id const & base_ident,
                        app_state & app,
                        revision_id & chosen)
{
  set<revision_id> chosen_set;
  N(app.branch_name() != "",
    F("cannot determine branch for update"));

  find_deepest_acceptable_descendent(base_ident, cert_value(app.branch_name()),
                                     app, chosen_set);

  N(chosen_set.size() != 0,
    F("no candidates remain after selection"));

  N(chosen_set.size() == 1,
    F("multiple candidates remain after selection"));
  
  chosen = *(chosen_set.begin());
}
  

