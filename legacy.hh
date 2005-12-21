#ifndef __LEGACY_HH__
#define __LEGACY_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// old code needed for reading legacy data (so we can then convert it)

#include <map>
#include <string>

#include "paths.hh"

namespace legacy
{
  typedef std::map<file_path, std::map<std::string, std::string> > dot_mt_attrs_map;

  void 
  read_dot_mt_attrs(data const & dat, dot_mt_attrs_map & attr);
}

#endif
