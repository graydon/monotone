#ifndef __ANNOTATE_HH__
#define __ANNOTATE_HH__

// copyright (C) 2005 emile snyder <emile@alumni.reed.edu>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "platform.hh"
#include "vocab.hh"
#include "revision.hh"
#include "app_state.hh"

extern void do_annotate (app_state &app, file_t file_node, revision_id rid);

#endif // defined __ANNOTATE_HH__
