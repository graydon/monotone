#ifndef __WORK_HH__
#define __WORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include "vocab.hh"
#include "manifest.hh"

//
// this file defines structures to deal with the "working copy" of a tree
//
// working copy book-keeping files are stored in a directory called MT, off
// the root of the working copy source tree (analogous to the CVS or SVN
// directories). there is no hierarchy of MT directories; only one exists,
// and it is always at the root. it contains the following files:
//
// MT/manifest       -- the check-out manifest, as defined in manifest.hh
// MT/work           -- (optional) a set of added and deleted pathnames
//
// as work proceeds, the files in the working directory either change their
// sha1 fingerprints from those listed in the manifest file, or else are
// added or deleted (and the paths of those changes recorded in 'MT/work').
// 
// when it comes time to commit, the change set is calculated by building a
// path set from the old manifest, deleting old path elements, adding new
// path elements, calculating the sha1 of each entry in the resulting path
// set, and calculating a manifest delta (see manifest.hh) between the
// check-out manifest and the new one.
//
// this is *completely decoupled* from the issue of importing appropriate 
// versions of files into the database. if the files already exist in the db,
// there is no reason to import them anew.
//

typedef set<file_path> path_set;

extern string const work_file_name;

struct work_set
{
  // imprecise, uncommitted work record
  path_set adds;
  path_set dels;
};

void read_work_set(data const & dat,
		   work_set & work);

void write_work_set(data & dat,
		    work_set const & work);

void extract_path_set(manifest_map const & man,
		      path_set & paths);

void apply_work_set(work_set const & work,
		    path_set & paths);

void build_addition(file_path const & path,
		    app_state & app,
		    work_set & work,
		    manifest_map & man,
 		    bool & rewrite_work);

void build_deletion(file_path const & path,
		    app_state & app,
		    work_set & work,
		    manifest_map & man,
 		    bool & rewrite_work);

#endif // __WORK_HH__
