#ifndef __SELECTORS_HH__
#define __SELECTORS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include <set>

class app_state;
class project_t;

// In the normal case, to expand a selector on the command line, use one of
// these functions: the former if the selector can legitimately expand to
// more than one revision, the latter if it shouldn't.  Both treat a
// selector that expands to zero revisions, or a nonexistent revision, as an
// usage error, and generate progress messages when expanding selectors.

void complete(app_state & app, project_t & project, std::string const & str,
              std::set<revision_id> & completions);

void complete(app_state & app, project_t & project, std::string const & str,
              revision_id & completion);

// For extra control, use these functions.  expand_selector is just like the
// first overload of complete() except that it produces no progress messages
// or usage errors.  diagnose_ambiguous_expansion generates the canonical
// usage error if the set it is handed has more than one element.

void expand_selector(app_state & app, project_t & project,
                     std::string const & str,
                     std::set<revision_id> & completions);

void diagnose_ambiguous_expansion(project_t & project, std::string const & str,
                                  std::set<revision_id> const & completions);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __SELECTORS_HH__
