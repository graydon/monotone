#ifndef __EPOCH_HH__
#define __EPOCH_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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
