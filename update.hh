#ifndef __UPDATE_HH__
#define __UPDATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include "app_state.hh"
#include "manifest.hh"
#include "vocab.hh"

// this function just encapsulates the (somewhat complex) logic 
// behind picking an update target. the actual updating takes
// place in commands.cc, along with most other file-modifying
// actions.
//
// every equally revision that is tied for best update candidate will be
// returned in 'candidates'.  if no revisions are better than the current
// revision, then 'candidates' will contain exactly the current revision.

void pick_update_candidates(revision_id const & base_ident,
                            app_state & app,
                            std::set<revision_id> &candidates);

#endif // __UPDATE_HH__
