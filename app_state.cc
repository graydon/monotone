#include <termios.h>
#include <iostream>

#include "app_state.hh"
#include "database.hh"
#include "transforms.hh"
#include "work.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

using namespace std;

static string const database_option("database");
static string const branch_option("branch");
static string const key_option("key");

app_state::app_state() 
  : branch_name(""), db(""), options_changed(false)
{
  options[database_option] = string("");
  options[branch_option] = string("");
  local_path o_path;
  get_options_path(o_path);
  if (file_exists(o_path))
    {
      data dat;
      read_data(o_path, dat);
      read_options_map(dat, options);

      string dbname = absolutify(options[database_option]());
      if (dbname != "")
	db.set_filename(dbname);
      branch_name = options[branch_option];
      internalize_rsa_keypair_id(options[key_option], signing_key);
    }
}

app_state::~app_state()
{
}

void 
app_state::set_database(utf8 const & filename)
{
  options[database_option] = utf8(absolutify(filename()));
  db.set_filename(filename());
  options_changed = true;
}

void 
app_state::set_branch(utf8 const & branch)
{
  options[branch_option] = branch;
  branch_name = branch();
  options_changed = true;
}

void 
app_state::set_signing_key(utf8 const & key)
{
  rsa_keypair_id k;
  internalize_rsa_keypair_id(key, k);
  options[key_option] = key;
  signing_key = k;
  options_changed = true;
}

void 
app_state::write_options()
{
  if (options_changed)
    {
      local_path o_path;
      get_options_path(o_path);

      data dat;
      write_options_map(dat, options);
      write_data(o_path, dat);
    }
}
