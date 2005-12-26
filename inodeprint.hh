#ifndef __INODEPRINT_HH__
#define __INODEPRINT_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "quick_alloc.hh"

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


#endif
