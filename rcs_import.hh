#ifndef __RCS_IMPORT_HH__
#define __RCS_IMPORT_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

class system_path;
class database;
class key_store;
class project_t;
class branch_name;

void test_parse_rcs_file(system_path const & filename);
void import_cvs_repo(project_t & project,
                     key_store & keys,
                     system_path const & cvsroot,
                     branch_name const & branchname);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __RCS_IMPORT_HH__
