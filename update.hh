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

#include "app_state.hh"
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
