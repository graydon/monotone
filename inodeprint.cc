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

#include <boost/regex.hpp>

#include "app_state.hh"
#include "manifest.hh"
#include "inodeprint.hh"
#include "sanity.hh"
#include "platform.hh"

// this file defines the inodeprint_map structure, and some operations on it.
// it is currently heavily based on manifest.cc.

// reading inodeprint_maps

struct 
add_to_inodeprint_map
{    
  inodeprint_map & ipm;
  explicit add_to_inodeprint_map(inodeprint_map & ipm) : ipm(ipm) {}
  bool operator()(boost::match_results<std::string::const_iterator> const & res) 
  {
    std::string ident(res[1].first, res[1].second);
    std::string path(res[2].first, res[2].second);
    file_path pth(path);
    ipm.insert(inodeprint_entry(pth, hexenc<inodeprint>(ident)));
    return true;
  }
};

void 
read_inodeprint_map(data const & dat,
                    inodeprint_map & ipm)
{
  boost::regex expr("^([[:xdigit:]]{40})  ([^[:space:]].*)$");
  boost::regex_grep(add_to_inodeprint_map(ipm), dat(), expr, boost::match_not_dot_newline);  
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
