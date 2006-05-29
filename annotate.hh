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

#include "platform.hh"
#include "vocab.hh"
#include "revision.hh"
#include "app_state.hh"

void 
do_annotate(app_state &app, file_t file_node, revision_id rid);

#endif // defined __ANNOTATE_HH__
