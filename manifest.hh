#ifndef __MANIFEST_HH__
#define __MANIFEST_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>
#include <string>

#include "quick_alloc.hh"
#include "vocab.hh"

// this file defines the class of manifest_map objects, and various
// comparison and i/o functions on them. a manifest_map is an unpacked
// (analyzed) manifest_data blob. it specifies exactly which versions of
// each file reside at which path location in a given tree. the textual
// encoding of a manifest looks like this:
//
// ...
// f2e5719b975e319c2371c98ed2c7231313fac9b5  fs/readdir.c
// 81f0c9a0df254bc8d51bb785713a9f6d0b020b22  fs/read_write.c
// 943851e7da46014cb07473b90d55dd5145f24de0  fs/pipe.c
// ddc2686e000e97f670180c60a3066989e56a11a3  fs/open.c
// 295d276e6c9ce64846d309a8e39507bcb0a14248  fs/namespace.c
// 71e0274f16cd68bdf9a2bf5743b86fcc1e597cdc  fs/namei.c
// 1112c0f8054cebc9978aa77384e3e45c0f3b6472  fs/iobuf.c
// 8ddcfcc568f33db6205316d072825d2e5c123275  fs/inode.c
// ...
//
// which is essentially the result of running:
//
// 'find -type f | xargs sha1sum'
//
// with some minor tidying up of pathnames and sorting. manifests must
// have only one entry for each pathname. the same sha1 can occur multiple
// times in a manifest.

typedef std::set<file_path> path_set;
extern std::string const manifest_file_name;
typedef std::pair<file_path, file_id> entry;
typedef std::pair<file_path const, file_id> manifest_map_entry;
typedef std::map<file_path, file_id, 
		 std::less<file_path>, 
		 QA(manifest_map_entry) > manifest_map;

// this helper exists solely to save us writing ->first and ->second all
// over the place when dealing with entries / members of manifest_maps. they're
// unreadable enough to make some otherwise clear functions obscure and
// buggy. maybe you can keep it clear in your head, but I can't always.

class path_id_pair
{
  entry dat;
public:
  path_id_pair() {}
  path_id_pair(manifest_map::const_iterator i) : dat(*i) {}
  path_id_pair(entry const & e) : dat(e) {}
  entry const & get_entry() { return dat; }

  file_path const & path() const { return dat.first; }
  path_id_pair & path(file_path const & p) { dat.first = p; return *this; } 

  file_id const & ident() const { return dat.second; }
  path_id_pair & ident(file_id const & i) { dat.second = i; return *this; }   
};

// analyzed changes to a manifest_map (not just blob-delta)
struct manifest_changes
{
  std::set<entry> adds;
  std::set<entry> dels;
};

void calculate_manifest_changes(manifest_map const & a,
				manifest_map const & b,
				manifest_changes & changes);

void apply_manifest_changes(manifest_map const & a,
			    manifest_changes const & changes,
			    manifest_map & b);

void write_manifest_changes(manifest_changes const & changes, 
			    data & dat);

std::ostream & operator<<(std::ostream & out, entry const & e);

class app_state;

// from cwd
void build_manifest_map(app_state & app,
			manifest_map & man);

// from some safe sub-dir within cwd
void build_manifest_map(file_path const & path,
			app_state & app,
			manifest_map & man);

void build_manifest_map(path_set const & paths,
			manifest_map & man,
			app_state & app);

void append_manifest_map(manifest_map const & m1,
			 manifest_map & m2);

void read_manifest_map(data const & dat,
		       manifest_map & man);

void read_manifest_map(manifest_data const & dat,
		       manifest_map & man);

void write_manifest_map(manifest_map const & man, 
			manifest_data & dat);

void write_manifest_map(manifest_map const & man, 
			data & dat);

#endif // __MANIFEST_HH__
