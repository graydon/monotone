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

typedef std::pair<bool, utf8> restriction;

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
  std::vector<utf8> extra_rcfiles;
  std::vector<restriction> restrictions;

  void initialize(bool working_copy);
  void initialize(std::string const & dir);

  void set_branch(utf8 const & name);
  void set_database(utf8 const & filename);
  void set_signing_key(utf8 const & key);

  void set_stdhooks(bool b);
  void set_rcfiles(bool b);
  void add_rcfile(utf8 const & filename);

  void add_restriction(bool restrict, utf8 const & path);
  bool is_restricted(file_path const & path);

  explicit app_state();
  ~app_state();

private:
  void load_rcfiles();
  void read_options();
  void write_options();
};

#endif // __APP_STATE_HH__
