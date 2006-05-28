#include "cmd.hh"

#include "automate.hh"

#include <iostream>

using std::cout;

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
