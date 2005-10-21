#ifndef __RCS_IMPORT_HH__
#define __RCS_IMPORT_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "database.hh"

void test_parse_rcs_file(system_path const & filename, database & db);
void import_cvs_repo(system_path const & cvsroot, app_state & app);

#endif // __RCS_IMPORT_HH__
