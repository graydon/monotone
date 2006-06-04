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

#include "vocab.hh"
#include "database.hh"

void test_parse_rcs_file(system_path const & filename, database & db);
void import_cvs_repo(system_path const & cvsroot, app_state & app);

#endif // __RCS_IMPORT_HH__
