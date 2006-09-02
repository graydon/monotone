#ifndef __INODEPRINT_HH__
#define __INODEPRINT_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include "quick_alloc.hh"
#include "paths.hh"

typedef std::pair<file_path const, hexenc<inodeprint> > inodeprint_entry;

typedef std::map<file_path, hexenc<inodeprint>,
                 std::less<file_path>,
                 QA(inodeprint_entry) > inodeprint_map;

std::ostream & operator<<(std::ostream & out, inodeprint_entry const & e);

class app_state;

void read_inodeprint_map(data const & dat,
                         inodeprint_map & ipm);

void write_inodeprint_map(inodeprint_map const & ipm,
                          data & dat);


bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
