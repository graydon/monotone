// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#ifndef __SELECTORS_HH__
#define __SELECTORS_HH__

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
      sel_date,
      sel_tag,
      sel_ident,
      sel_cert,
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
