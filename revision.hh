#ifndef __REVISION_HH__
#define __REVISION_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <string>

#include "app_state.hh"
#include "change_set.hh"
#include "vocab.hh"

// a revision is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// revision:
// {
//   new_manifest: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//   edge:
//   {
//     old_revision: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//     old_manifest: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//     change_set:
//     {
//       paths:
//       {
//          rename_file:
//          {
//            src: [s10:usr/bin/sh]
//            dst: [s11:usr/bin/foo]
//          }
//          delete_dir: [s7:usr/bin]
//          add_file: [s15:tmp/foo/bar.txt]
//       }
//       deltas:
//       {
//         delta:
//         {
//           path: [s15:tmp/foo/bar.txt]
//           src: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]     
//           dst: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//         }
//     }
//   }
// }

extern std::string revision_file_name;

typedef std::map<revision_id, std::pair<manifest_id, change_set> > 
edge_map;

typedef edge_map::value_type
edge_entry;

struct 
revision_set
{
  manifest_id new_manifest;
  edge_map edges;
};

inline revision_id const & 
edge_old_revision(edge_entry const & e) 
{ 
  return e.first; 
}

inline revision_id const & 
edge_old_revision(edge_map::const_iterator i) 
{ 
  return i->first; 
}

inline manifest_id const & 
edge_old_manifest(edge_entry const & e) 
{ 
  return e.second.first; 
}

inline manifest_id const & 
edge_old_manifest(edge_map::const_iterator i) 
{ 
  return i->second.first; 
}

inline change_set const & 
edge_changes(edge_entry const & e) 
{ 
  return e.second.second; 
}

inline change_set const & 
edge_changes(edge_map::const_iterator i) 
{ 
  return i->second.second; 
}

void 
read_revision_set(data const & dat,
		  revision_set & rev);

void 
read_revision_set(revision_data const & dat,
		  revision_set & rev);

void
write_revision_set(revision_set const & rev,
		   data & dat);

void
write_revision_set(revision_set const & rev,
		   revision_data & dat);

// graph walking

bool 
find_common_ancestor(revision_id const & left,
		     revision_id const & right,
		     revision_id & anc,
		     app_state & app);

void 
calculate_composite_change_set(revision_id const & ancestor,
			       revision_id const & child,
			       app_state & app,
			       change_set & composed);


// basic_io access to printers and parsers

namespace basic_io { struct printer; struct parser; }

void 
print_revision(basic_io::printer & printer,
	       revision_set const & rev);

void 
parse_revision(basic_io::parser & parser,
	       revision_set & rev);

void 
print_edge(basic_io::printer & printer,
	   edge_entry const & e);

void 
parse_edge(basic_io::parser & parser,
	   edge_map & es);

#endif // __REVISION_HH__
