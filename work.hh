// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
#ifndef __WORK_HH__
#define __WORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <set>
#include <map>

#include "cset.hh"
#include "paths.hh"
#include "roster.hh"
#include "vocab.hh"

//
// this file defines structures to deal with the "workspace" of a tree
//

//
// workspace book-keeping files are stored in a directory called MT, off
// the root of the workspace source tree (analogous to the CVS or .svn
// directories). there is no hierarchy of MT directories; only one exists,
// and it is always at the root. it contains the following files:
//
// MT/revision       -- contains the id of the checked out revision
// MT/work           -- (optional) a set of added, deleted or moved pathnames
//                      this file is, syntactically, a cset
// MT/options        -- the database, branch and key options currently in use
// MT/log            -- user edited log file
// MT/inodeprints    -- file fingerprint cache, presence turns on "reckless"
//                      mode
//
// as work proceeds, the files in the workspace either change their
// sha1 fingerprints from those listed in the revision's manifest, or else are
// added or deleted or renamed (and the paths of those changes recorded in
// 'MT/work').
// 
// when it comes time to commit, the cset in MT/work (which can have no
// deltas) is applied to the base roster, then a new roster is built by
// analyzing the content of every file in the roster, as it appears in the
// workspace. a final cset is calculated which contains the requisite
// deltas, and placed in a rev, which is written to the db.
//
// MT/inodes, if present, can be used to speed up this last step.

struct file_itemizer : public tree_walker
{
  app_state & app;
  path_set & known;
  path_set & unknown;
  path_set & ignored;
  file_itemizer(app_state & a, path_set & k, path_set & u, path_set & i) 
    : app(a), known(k), unknown(u), ignored(i) {}
  virtual void visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

void
perform_additions(path_set const & targets, app_state & app, bool recursive = true);

void
perform_deletions(path_set const & targets, app_state & app);

void
perform_rename(std::set<file_path> const & src_paths,
               file_path const & dst_dir,
               app_state & app);

void
perform_pivot_root(file_path const & new_root, file_path const & put_old,
                   app_state & app);

// the "work" file contains the current cset representing uncommitted
// add/drop/rename operations (not deltas)

void get_work_cset(cset & w);
void remove_work_cset();
void put_work_cset(cset & w);

// the "revision" file contains the base revision id that the current working
// copy was checked out from

void get_revision_id(revision_id & c);
void put_revision_id(revision_id const & rev);
void get_base_revision(app_state & app, 
                       revision_id & rid,
                       roster_t & ros,
                       marking_map & mm);
void get_base_revision(app_state & app,
                       revision_id & rid,
                       roster_t & ros);
void get_base_roster(app_state & app, roster_t & ros);

// This returns the current roster, except it does not bother updating the
// hashes in that roster -- the "shape" is correct, all files and dirs exist
// and under the correct names -- but do not trust file content hashes.
void get_current_roster_shape(roster_t & ros, node_id_source & nis, app_state & app);
// This does update hashes, but only those that match the current restriction
void get_current_restricted_roster(roster_t & ros, node_id_source & nis, app_state & app);

// This returns the current roster, except it does not bother updating the
// hashes in that roster -- the "shape" is correct, all files and dirs exist
// and under the correct names -- but do not trust file content hashes.
void get_base_and_current_roster_shape(roster_t & base_roster,
                                       roster_t & current_roster,
                                       node_id_source & nis,
                                       app_state & app);
// This does update hashes, but only those that match the current restriction
void get_base_and_current_restricted_roster(roster_t & base_roster,
                                            roster_t & current_roster,
                                            node_id_source & nis,
                                            app_state & app);

// the "user log" is a file the user can edit as they program to record
// changes they make to their source code. Upon commit the file is read
// and passed to the edit_comment lua hook. If the commit is a success,
// the user log is then blanked. If the commit does not succeed, no
// change is made to the user log file.

void get_user_log_path(bookkeeping_path & ul_path);

void read_user_log(data & dat);

void write_user_log(data const & dat);

void blank_user_log();

bool has_contents_user_log();

// the "options map" is another administrative file, stored in
// MT/options. it keeps a list of name/value pairs which are considered
// "persistent options", associated with a particular the workspace and
// implied unless overridden on the command line. the main ones are
// --branch and --db, although some others may follow in the future.

typedef std::map<std::string, utf8> options_map;

void get_options_path(bookkeeping_path & o_path);

void read_options_map(data const & dat, options_map & options);

void write_options_map(data & dat,
                       options_map const & options);

// the "local dump file' is a debugging file, stored in MT/debug.  if we
// crash, we save some debugging information here.

void get_local_dump_path(bookkeeping_path & d_path);

// the 'inodeprints file' contains inode fingerprints 

void get_inodeprints_path(bookkeeping_path & ip_path);

bool in_inodeprints_mode();

void read_inodeprints(data & dat);

void write_inodeprints(data const & dat);

void enable_inodeprints();

extern std::string const encoding_attribute;
extern std::string const manual_merge_attribute;

bool get_attribute_from_roster(roster_t const & ros,                               
                               file_path const & path,
                               attr_key const & key,
                               attr_value & val);

void update_any_attrs(app_state & app);

extern std::string const binary_encoding;
extern std::string const default_encoding;

struct file_content_source
{
  virtual void get_file_content(file_id const & fid,
                                file_data & dat) const = 0;
  virtual ~file_content_source() {};
};

struct empty_file_content_source : public file_content_source
{
  virtual void get_file_content(file_id const & fid,
                                file_data & dat) const
  {
    I(false);
  }
};

struct editable_working_tree : public editable_tree
{
  editable_working_tree(app_state & app, file_content_source const & source);

  virtual node_id detach_node(split_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, split_path const & dst);

  virtual void apply_delta(split_path const & pth, 
                           file_id const & old_id, 
                           file_id const & new_id);
  virtual void clear_attr(split_path const & pth,
                          attr_key const & name);
  virtual void set_attr(split_path const & pth,
                        attr_key const & name,
                        attr_value const & val);

  virtual void commit();

  virtual ~editable_working_tree();
private:
  app_state & app;
  file_content_source const & source;
  node_id next_nid;
  std::map<bookkeeping_path, file_id> written_content;
  std::map<bookkeeping_path, file_path> rename_add_drop_map;
  bool root_dir_attached;
};

#endif // __WORK_HH__
