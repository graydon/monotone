// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "epoch.hh"
#include "netio.hh"
#include "constants.hh"
#include "transforms.hh"

#include <string>

void
read_epoch(std::string const & in,
           cert_value & branch, epoch_data & epoch)
{
  size_t pos = 0;
  std::string raw_branch;
  data raw_epoch;
  extract_variable_length_string(in, raw_branch, pos, "epoch, branch name");
  raw_epoch = data(extract_substring(in, pos, constants::epochlen_bytes,
                                     "epoch, epoch data"));
  branch = cert_value(raw_branch);
  hexenc<data> tmp;
  encode_hexenc(raw_epoch, tmp);
  epoch = epoch_data(tmp);
}

void
write_epoch(cert_value const & branch, epoch_data const & epoch,
            std::string & out)
{
  insert_variable_length_string(branch(), out);
  data raw_epoch;
  decode_hexenc(epoch.inner(), raw_epoch);
  out += raw_epoch();
}

void
epoch_hash_code(cert_value const & branch, epoch_data const & epoch,
                epoch_id & eid)
{
  std::string tmp(branch() + ":" + epoch.inner()());
  data tdat(tmp);
  hexenc<id> out;
  calculate_ident(tdat, out);
  eid = epoch_id(out);
}
