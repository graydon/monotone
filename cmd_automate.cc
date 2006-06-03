// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>

#include "automate.hh"
#include "cmd.hh"

using std::cout;
using std::vector;

CMD(automate, N_("automation"),
    N_("interface_version\n"
       "heads [BRANCH]\n"
       "ancestors REV1 [REV2 [REV3 [...]]]\n"
       "attributes [FILE]\n"
       "parents REV\n"
       "descendents REV1 [REV2 [REV3 [...]]]\n"
       "children REV\n"
       "graph\n"
       "erase_ancestors [REV1 [REV2 [REV3 [...]]]]\n"
       "toposort [REV1 [REV2 [REV3 [...]]]]\n"
       "ancestry_difference NEW_REV [OLD_REV1 [OLD_REV2 [...]]]\n"
       "common_ancestors REV1 [REV2 [REV3 [...]]]\n"
       "leaves\n"
       "inventory\n"
       "stdio\n"
       "certs REV\n"
       "select SELECTOR\n"
       "get_file FILEID\n"
       "get_manifest_of [REVID]\n"
       "get_revision [REVID]\n"
       "get_base_revision_id\n"
       "get_current_revision_id\n"
       "packet_for_rdata REVID\n"
       "packets_for_certs REVID\n"
       "packet_for_fdata FILEID\n"
       "packet_for_fdelta OLD_FILE NEW_FILE\n"
       "keys\n"),
    N_("automation interface"), 
    OPT_NONE)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  utf8 cmd = *i;
  ++i;
  vector<utf8> cmd_args(i, args.end());

  make_io_binary();

  automate_command(cmd, cmd_args, name, app, cout);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
