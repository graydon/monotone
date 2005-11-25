// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "file_io.hh"
#include "manifest.hh"
#include "transforms.hh"
#include "sanity.hh"
#include "inodeprint.hh"
#include "paths.hh"
#include "platform.hh"
#include "constants.hh"

// this file defines the class of manifest_map objects, and various comparison
// and i/o functions on them. a manifest specifies exactly which versions
// of each file reside at which path location in a given tree.

using namespace std;

// building manifest_maps

class 
manifest_map_builder : public tree_walker
{
  app_state & app;
  manifest_map & man;
public:
  manifest_map_builder(app_state & a, manifest_map & m);
  virtual void visit_file(file_path const & path);
};

manifest_map_builder::manifest_map_builder(app_state & a, manifest_map & m) 
  : app(a), man(m) 
{
}

void 
manifest_map_builder::visit_file(file_path const & path)
{      
  if (app.lua.hook_ignore_file(path))
    return;
  hexenc<id> ident;
  L(F("scanning file %s\n") % path);
  calculate_ident(path, ident, app.lua);
  man.insert(manifest_entry(path, file_id(ident)));
}

inline static bool
inodeprint_unchanged(inodeprint_map const & ipm, file_path const & path) 
{
  inodeprint_map::const_iterator old_ip = ipm.find(path);
  if (old_ip != ipm.end())
    {
      hexenc<inodeprint> ip;
      if (inodeprint_file(path, ip) && ip == old_ip->second)
          return true; // unchanged
      else
          return false; // changed or unavailable
    }
  else
    return false; // unavailable
}

// reading manifest_maps

void 
read_manifest_map(data const & dat,
                  manifest_map & man)
{
  std::string::size_type pos = 0;
  while (pos != dat().size())
    {
      // whenever we get here, pos points to the beginning of a manifest
      // line
      // manifest file has 40 characters hash, then 2 characters space, then
      // everything until next \n is filename.
      std::string ident = dat().substr(pos, constants::idlen);
      std::string::size_type file_name_begin = pos + constants::idlen + 2;
      pos = dat().find('\n', file_name_begin);
      std::string file_name;
      if (pos == std::string::npos)
        file_name = dat().substr(file_name_begin);
      else
        file_name = dat().substr(file_name_begin, pos - file_name_begin);
      man.insert(manifest_entry(file_path_internal(file_name),
                                hexenc<id>(ident)));
      // skip past the '\n'
      ++pos;
    }
  return;
}

void 
read_manifest_map(manifest_data const & dat,
                  manifest_map & man)
{  
  read_manifest_map(dat.inner(), man);
}



// writing manifest_maps

std::ostream & 
operator<<(std::ostream & out, manifest_entry const & e)
{
  return (out << manifest_entry_id(e) << "  " << manifest_entry_path(e) << "\n");
}


void 
write_manifest_map(manifest_map const & man,
                   manifest_data & dat)
{
  ostringstream sstr;
  copy(man.begin(),
       man.end(),
       ostream_iterator<manifest_entry>(sstr));

  dat = manifest_data(sstr.str());
}

void 
write_manifest_map(manifest_map const & man,
                   data & dat)
{
  ostringstream sstr;
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    sstr << *i;
  dat = sstr.str();
}

void
dump(manifest_map const & man, std::string & out)
{
  data dat;
  write_manifest_map(man, dat);
  out = dat();
}
