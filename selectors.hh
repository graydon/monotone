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

#include "vector.hh"
#include <algorithm>
#include <set>

class database;

namespace selectors
{

  typedef enum
    {
      sel_author,
      sel_branch,
      sel_head,
      sel_date,
      sel_tag,
      sel_ident,
      sel_cert,
      sel_earlier,
      sel_later,
      sel_parent,
      sel_unknown
    }
  selector_type;

  void
  complete_selector(std::string const & orig_sel,
                    std::vector<std::pair<selector_type, std::string> > const & limit,
                    selector_type & type,
                    std::set<std::string> & completions,
                    database & db);
  std::vector<std::pair<selector_type, std::string> >
  parse_selector(std::string const & str,
                 database & db);

}; // namespace selectors

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __SELECTORS_HH__
