#ifndef __ANNOTATE_HH__
#define __ANNOTATE_HH__

// Copyright (C) 2005 Emile Snyder <emile@alumni.reed.edu>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include "rev_types.hh"

class project_t;

void
do_annotate(project_t & project, file_t file_node, revision_id rid,
            bool just_revs);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // defined __ANNOTATE_HH__
