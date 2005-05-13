#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

class app_state;
class lua_hooks;

#include <vector>

#include "database.hh"
#include "lua.hh"
#include "work.hh"
#include "vocab.hh"

// this class is supposed to hold all (or.. well, most) of the state of the
// application, barring some unfortunate static objects like the debugging /
// logging system and the command objects, for the time being. the vague intent
// being to make life easier for anyone who wants to embed this program as a
// library someday

class app_state
{
public:
  rsa_keypair_id signing_key;
  utf8 branch_name;
  database db;
  lua_hooks lua;
  bool stdhooks;
  bool rcfiles;
  options_map options;
  utf8 message;
  utf8 message_file;
  utf8 date;
  utf8 author;
  utf8 search_root;
  std::vector<utf8> revision_selectors;
  std::vector<utf8> extra_rcfiles;
  path_set restrictions;
  file_path relative_directory;
  bool found_working_copy;
  long depth;
  fs::path pidfile;

  void allow_working_copy();
  void require_working_copy();
  void create_working_copy(std::string const & dir);

  file_path prefix(utf8 const & path);
  void set_restriction(path_set const & valid_paths, std::vector<utf8> const & paths);
  bool restriction_includes(file_path const & path);

  // Set the branch name.  If you only invoke set_branch, the branch
  // name is not sticky (and won't be written to the working copy and
  // reused by subsequent monotone invocations).  Commands which
  // switch the working to a different branch should invoke
  // make_branch_sticky (before require_working_copy because this
  // function updates the working copy).
  void set_branch(utf8 const & name);
  void make_branch_sticky();

  void set_database(utf8 const & filename);
  void set_signing_key(utf8 const & key);
  void set_root(utf8 const & root);
  void set_message(utf8 const & message);
  void set_message_file(utf8 const & message_file);
  void set_date(utf8 const & date);
  void set_author(utf8 const & author);
  void set_depth(long depth);
  void set_pidfile(utf8 const & pidfile);
  void add_revision(utf8 const & selector);

  void set_stdhooks(bool b);
  void set_rcfiles(bool b);
  void add_rcfile(utf8 const & filename);

  explicit app_state();
  ~app_state();

private:
  void load_rcfiles();
  void read_options();
  void write_options();
};

#endif // __APP_STATE_HH__
