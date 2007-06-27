#ifndef __EPOCH_HH__
#define __EPOCH_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"


// epochs are pairs (branch name, random data)

void read_epoch(std::string const & in,
                branch_name & branch, epoch_data & epoch);
void write_epoch(branch_name const & branch, epoch_data const & epoch,
                 std::string & out);
void epoch_hash_code(branch_name const & branch, epoch_data const & epoch,
                     epoch_id & eid);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
