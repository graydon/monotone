#ifndef __UPDATE_HH__
#define __UPDATE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <set>
#include "vocab.hh"

class database;
class project_t;
class lua_hooks;

// this function just encapsulates the (somewhat complex) logic
// behind picking an update target. the actual updating takes
// place in commands.cc, along with most other file-modifying
// actions.
//
// every equally revision that is tied for best update candidate will be
// returned in 'candidates'.  if no revisions are better than the current
// revision, then 'candidates' will contain exactly the current revision.

void pick_update_candidates(std::set<revision_id> & candidates,
                            revision_id const & base_ident,
                            branch_name const & branchname,
                            bool ignore_suspend_certs,
                            database & db, project_t & project,
                            lua_hooks & lua);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __UPDATE_HH__
