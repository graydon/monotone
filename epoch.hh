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

#include <string>

// epochs are pairs (branch name, random data)

void read_epoch(std::string const & in,
                cert_value & branch, epoch_data & epoch);
void write_epoch(cert_value const & branch, epoch_data const & epoch,
                 std::string & out);
void epoch_hash_code(cert_value const & branch, epoch_data const & epoch,
                     epoch_id & eid);

#endif
