// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include "app_state.hh"
#include "manifest.hh"
#include "inodeprint.hh"
#include "sanity.hh"
#include "platform.hh"
#include "constants.hh"

// this file defines the inodeprint_map structure, and some operations on it.
// it is currently heavily based on manifest.cc.

// reading inodeprint_maps

void 
read_inodeprint_map(data const & dat,
                    inodeprint_map & ipm)
{
  std::string::size_type pos = 0;
  while (pos != dat().size())
    {
      // whenever we get here, pos points to the beginning of a inodeprint
      // line
      // inodeprint file has 40 characters hash, then 2 characters space, then
      // everything until next \n is filename.
      std::string ident = dat().substr(pos, constants::idlen);
      std::string::size_type file_name_begin = pos + constants::idlen + 2;
      pos = dat().find('\n', file_name_begin);
      std::string file_name;
      if (pos == std::string::npos)
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

std::ostream & 
operator<<(std::ostream & out, inodeprint_entry const & e)
{
  return (out << e.second << "  " << e.first << "\n");
}


void 
write_inodeprint_map(inodeprint_map const & ipm,
                     data & dat)
{
  std::ostringstream sstr;
  for (inodeprint_map::const_iterator i = ipm.begin();
       i != ipm.end(); ++i)
    sstr << *i;
  dat = sstr.str();
}
