// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "project.hh"
#include "rcs_import.hh"
#include "keys.hh"
#include "key_store.hh"

using std::vector;

CMD(rcs_import, "rcs_import", "", CMD_REF(debug), N_("RCSFILE..."),
    N_("Parses versions in RCS files"),
    N_("This command doesn't reconstruct or import revisions.  "
       "You probably want to use cvs_import."),
    options::opts::branch)
{
  if (args.size() < 1)
    throw usage(execid);

  for (args_vector::const_iterator i = args.begin();
       i != args.end(); ++i)
    test_parse_rcs_file(system_path((*i)()));
}


CMD(cvs_import, "cvs_import", "", CMD_REF(rcs), N_("CVSROOT"), 
    N_("Imports all versions in a CVS repository"),
    "",
    options::opts::branch)
{
  key_store keys(app);
  project_t project(app.db);

  if (args.size() != 1)
    throw usage(execid);

  N(app.opts.branchname() != "",
    F("need base --branch argument for importing"));

  system_path cvsroot(idx(args, 0)());
  require_path_is_directory(cvsroot,
                            F("path %s does not exist") % cvsroot,
                            F("'%s' is not a directory") % cvsroot);

  // make sure we can sign certs using the selected key; also requests
  // the password (if necessary) up front rather than after some arbitrary
  // amount of work
  cache_user_key(app.opts, app.lua, app.db, keys);

  import_cvs_repo(project, keys, cvsroot, app.opts.branchname);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
