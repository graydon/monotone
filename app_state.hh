#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

class app_state;
class lua_hooks;

#include <string>

#include "database.hh"
#include "lua.hh"
#include "work.hh"

// this class is supposed to hold all (or.. well, most) of the state of the
// application, barring some unfortunate static objects like the debugging /
// logging system and the command objects, for the time being. the vague intent
// being to make life easier for anyone who wants to embed this program as a
// library someday

class app_state
{
public:
  rsa_keypair_id signing_key;
  std::string branch_name;
  database db;
  lua_hooks lua;
  bool options_changed;
  options_map options;

  void set_branch(std::string name);
  void set_database(std::string filename);
  void write_options();

  explicit app_state();
  ~app_state();
private:
};

#endif // __APP_STATE_HH__
