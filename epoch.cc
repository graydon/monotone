// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "epoch.hh"
#include "netio.hh"
#include "constants.hh"
#include "transforms.hh"


using std::string;

void
read_epoch(string const & in,
           branch_name & branch, epoch_data & epoch)
{
  size_t pos = 0;
  string raw_branch;
  data raw_epoch;
  extract_variable_length_string(in, raw_branch, pos, "epoch, branch name");
  raw_epoch = data(extract_substring(in, pos, constants::epochlen_bytes,
                                     "epoch, epoch data"));
  branch = branch_name(raw_branch);
  epoch = epoch_data(raw_epoch);
}

void
write_epoch(branch_name const & branch, epoch_data const & epoch,
            string & out)
{
  insert_variable_length_string(branch(), out);
  out += epoch.inner()();
}

void
epoch_hash_code(branch_name const & branch, epoch_data const & epoch,
                epoch_id & eid)
{
  string tmp(branch() + ":" + encode_hexenc(epoch.inner()()));
  data tdat(tmp);
  id out;
  calculate_ident(tdat, out);
  eid = epoch_id(out);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
