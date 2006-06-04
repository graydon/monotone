// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include "app_state.hh"
#include "inodeprint.hh"
#include "sanity.hh"
#include "platform.hh"
#include "constants.hh"

using std::ostream;
using std::ostringstream;
using std::string;

// this file defines the inodeprint_map structure, and some operations on it.
// it is currently heavily based on the old manifest.cc.

// reading inodeprint_maps

void
read_inodeprint_map(data const & dat,
                    inodeprint_map & ipm)
{
  string::size_type pos = 0;
  while (pos != dat().size())
    {
      // whenever we get here, pos points to the beginning of a inodeprint
      // line
      // inodeprint file has 40 characters hash, then 2 characters space, then
      // everything until next \n is filename.
      string ident = dat().substr(pos, constants::idlen);
      string::size_type file_name_begin = pos + constants::idlen + 2;
      pos = dat().find('\n', file_name_begin);
      string file_name;
      if (pos == string::npos)
        file_name = dat().substr(file_name_begin);
      else
        file_name = dat().substr(file_name_begin, pos - file_name_begin);
      ipm.insert(inodeprint_entry(file_path_internal(file_name),
                                  hexenc<inodeprint>(ident)));
      // skip past the '\n'
      ++pos;
    }
  return;
}

// writing inodeprint_maps

ostream &
operator<<(ostream & out, inodeprint_entry const & e)
{
  return (out << e.second << "  " << e.first << "\n");
}


void
write_inodeprint_map(inodeprint_map const & ipm,
                     data & dat)
{
  ostringstream sstr;
  for (inodeprint_map::const_iterator i = ipm.begin();
       i != ipm.end(); ++i)
    sstr << *i;
  dat = sstr.str();
}
