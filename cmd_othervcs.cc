#include "cmd.hh"

#include "rcs_import.hh"

using std::vector;

CMD(rcs_import, N_("debug"), N_("RCSFILE..."),
    N_("parse versions in RCS files\n"
       "this command doesn't reconstruct or import revisions."
       "you probably want cvs_import"),
    OPT_BRANCH_NAME)
{
  if (args.size() < 1)
    throw usage(name);

  for (vector<utf8>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      test_parse_rcs_file(system_path((*i)()), app.db);
    }
}


CMD(cvs_import, N_("rcs"), N_("CVSROOT"), N_("import all versions in CVS repository"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  import_cvs_repo(system_path(idx(args, 0)()), app);
}
