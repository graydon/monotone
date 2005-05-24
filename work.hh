#ifndef __WORK_HH__
#define __WORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <set>
#include <map>

#include "change_set.hh"
#include "manifest.hh"
#include "vocab.hh"

//
// this file defines structures to deal with the "working copy" of a tree
//

//
// working copy book-keeping files are stored in a directory called MT, off
// the root of the working copy source tree (analogous to the CVS or .svn
// directories). there is no hierarchy of MT directories; only one exists,
// and it is always at the root. it contains the following files:
//
// MT/revision       -- contains the id of the checked out revision
// MT/work           -- (optional) a set of added, deleted or moved pathnames
//                      this file is, syntactically, a path_rearrangement
// MT/options        -- the database, branch and key options currently in use
// MT/log            -- user edited log file
// MT/inodeprints    -- file fingerprint cache, presence turns on "reckless"
//                      mode
//
// as work proceeds, the files in the working directory either change their
// sha1 fingerprints from those listed in the revision's manifest, or else are
// added or deleted or renamed (and the paths of those changes recorded in
// 'MT/work').
// 
// when it comes time to commit, the change_set is calculated by applying
// the path_rearrangement to the manifest and then calculating the
// delta_set between the modified manifest and the files in the working
// copy.
//
// MT/inodes, if present, can be used to speed up this last step.

typedef std::set<file_path> path_set;

struct file_itemizer : public tree_walker
{
  app_state & app;
  path_set & known;
  path_set & unknown;
  path_set & ignored;
  file_itemizer(app_state & a, path_set & k, path_set & u, path_set & i) 
    : app(a), known(k), unknown(u), ignored(i) {}
  virtual void file_itemizer::visit_file(file_path const & path);
};

void 
build_additions(std::vector<file_path> const & args,
               manifest_map const & m_old,
               app_state & app,
               change_set::path_rearrangement & pr);

void 
build_deletions(std::vector<file_path> const & args,
               manifest_map const & m_old,
                app_state & app,
               change_set::path_rearrangement & pr);

void 
build_rename(file_path const & src,
             file_path const & dst,
             manifest_map const & m_old,
             change_set::path_rearrangement & pr);


// the "work" file contains the current path rearrangement representing
// uncommitted add/drop/rename operations in the serialized change set format

void get_path_rearrangement(change_set::path_rearrangement & w);
void remove_path_rearrangement();
void put_path_rearrangement(change_set::path_rearrangement & w);

// the "revision" file contains the base revision id that the current working
// copy was checked out from

void get_revision_id(revision_id & c);
void put_revision_id(revision_id const & rev);
void get_base_revision(app_state & app, 
                       revision_id & rid,
                       manifest_id & mid,
                       manifest_map & man);
void get_base_manifest(app_state & app, manifest_map & man);

// the "user log" is a file the user can edit as they program to record
// changes they make to their source code. Upon commit the file is read
// and passed to the edit_comment lua hook. If the commit is a success,
// the user log is then blanked. If the commit does not succeed, no
// change is made to the user log file.

void get_user_log_path(local_path & ul_path);

void read_user_log(data & dat);

void write_user_log(data const & dat);

void blank_user_log();

bool has_contents_user_log();

// the "options map" is another administrative file, stored in
// MT/options. it keeps a list of name/value pairs which are considered
// "persistent options", associated with a particular the working copy and
// implied unless overridden on the command line. the main ones are
// --branch and --db, although some others may follow in the future.

typedef std::map<std::string, utf8> options_map;

void get_options_path(local_path & o_path);

void read_options_map(data const & dat, options_map & options);

void write_options_map(data & dat,
                       options_map const & options);

// the "local dump file' is a debugging file, stored in MT/debug.  if we
// crash, we save some debugging information here.

void get_local_dump_path(local_path & d_path);

// the 'inodeprints file' contains inode fingerprints 

void get_inodeprints_path(local_path & ip_path);

bool in_inodeprints_mode();

void read_inodeprints(data & dat);

void write_inodeprints(data const & dat);

void enable_inodeprints();

// the "attribute map" is part of a working copy. it is *not* stored in MT,
// because its contents are considered part of the "content" of a tree of
// files. it is therefore stored in .mt-attrs, in the root of your
// tree. you do not need a .mt-attrs file, it's just an extension
// mechanism.
//
// the contents of the .mt-attrs file is a list of [file, name, value]
// triples, each of which assigns a particular "extended attribute" to a
// file in your manifest. example "extended attributes" are things like
// "set the execute bit" or "this file is read-only" or whatnot. they are
// intrinsic properties of the files, but not actually part of the file's
// data stream. so they're kept here.

typedef std::map<file_path, std::map<std::string, std::string> > attr_map;

void get_attr_path(file_path & a_path);

void read_attr_map(data const & dat, attr_map & attrs);

void write_attr_map(data & dat,
                    attr_map const & options);

extern std::string const encoding_attribute;

bool get_attribute_from_db(file_path const & file,
                           std::string const & attr_key,
                           manifest_map const & man,
                           std::string & attr_val,
                           app_state & app); 

bool get_attribute_from_working_copy(file_path const & file,
                                     std::string const & attr_key,
                                     std::string & attr_val); 

void update_any_attrs(app_state & app);

extern std::string const binary_encoding;
extern std::string const default_encoding;

#endif // __WORK_HH__
