#ifndef __CHANGE_SET_HH__
#define __CHANGE_SET_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include "interner.hh"
#include "vocab.hh"

// a change_set is a text object. It has a precise, normalizable serial form
// as UTF-8 text. it also has some sub-components. not all of these
// sub-components are separately serialized (they could be but there is no
// call for it). a grammar (aside from the parsing code) for the serialized
// form will show up here eventually. until then, here is an example.
//
// change_set:
// {
//   paths:
//   {
//      rename_file:
//      {
//        src: [s10:usr/bin/sh]
//        dst: [s11:usr/bin/foo]
//      }
//      delete_dir: [s7:usr/bin]
//      add_file: [s15:tmp/foo/bar.txt]
//   }
//   deltas:
//   {
//     delta:
//     {
//       path: [s15:tmp/foo/bar.txt]
//       src: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]     
//       dst: [x40:71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc]
//     }
//   }
// } 
//
// note that this object is made up of two important sub-components: 
// path_edits and deltas. "path_edits" is exactly the same object stored
// in MT/path_edits (formerly MT/work). 

struct
change_set
{

  typedef enum {ptype_directory, ptype_file} ptype;
  typedef unsigned long long tid;
  
  struct
  path_item
  {
    tid parent;
    ptype ty;
    file_path name;      
    path_item(tid p, ptype t, file_path const & n);
    path_item(path_item const & other);
    path_item const & operator=(path_item const & other);
  };

  typedef std::map<tid, path_item> path_state;
  typedef std::pair<path_state, path_state> path_rearrangement;
  typedef std::map<file_path, std::pair<file_id, file_id> > delta_map;
  
  path_rearrangement rearrangement;
  delta_map deltas;
};

void
merge_change_sets(change_set const & a,
		  change_set const & b,
		  change_set & merged);

void
concatenate_change_sets(change_set const & a,
			change_set const & b,
			change_set & concatenated);

// value-oriented access to printers and parsers

void
read_change_set(data const & dat,
		change_set & cs);

void
write_change_set(change_set const & cs,
		 data & dat);


// basic_io access to printers and parsers

namespace basic_io { struct printer; struct parser; }

void 
print_change_set(basic_io::printer & printer,
		 change_set const & cs);

void 
parse_change_set(basic_io::parser & parser,
		 change_set & cs);

void 
print_path_rearrangement(basic_io::printer & basic_printer,
			 change_set::path_rearrangement const & pr);

void 
parse_path_rearrangement(basic_io::parser & pa,
			 change_set::path_rearrangement & pr);

#endif // __CHANGE_SET_HH__
