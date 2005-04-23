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

/*
class annotation_formatter {
};

class annotation_text_formatter : public annotation_formatter {
};
*/


extern void do_annotate (app_state &app, file_path fpath, file_id fid, revision_id rid);

/*
extern void write_annotations (boost::shared_ptr<annotate_context> acp, 
                               boost::shared_ptr<annotation_formatter> frmt);
*/

#endif // defined __ANNOTATE_HH__
