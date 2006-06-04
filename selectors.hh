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

#include <string>
#include <vector>
#include <algorithm>
#include <set>

class app_state;

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
      sel_unknown
    }
  selector_type;

  void
  complete_selector(std::string const & orig_sel,
                    std::vector<std::pair<selector_type, std::string> > const & limit,
                    selector_type & type,
                    std::set<std::string> & completions,
                    app_state & app);
  std::vector<std::pair<selector_type, std::string> >
  parse_selector(std::string const & str,
                 app_state & app);

}; // namespace selectors

#endif // __SELECTORS_HH__
