#include <termios.h>
#include <iostream>

#include "app_state.hh"
#include "database.hh"
#include "work.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

using namespace std;

static string const database_option("database");
static string const branch_option("branch");

app_state::app_state() 
  : branch_name(""), db("monotone.db"), options_changed(false)
{
  options[database_option] = string("monotone.db");
  options[branch_option] = string("");
  local_path o_path;
  get_options_path(o_path);
  if (file_exists(o_path))
    {
      data dat;
      read_data(o_path, dat);
      read_options_map(dat, options);

      string dbname = options[database_option];
      if (dbname != "monotone.db")
	db.set_filename(dbname);
      branch_name = options[branch_option];
    }
}

app_state::~app_state()
{
}

void app_state::set_database(string filename)
{
  options[database_option] = filename;
  db.set_filename(filename);
  options_changed = true;
}

void app_state::set_branch(string branch)
{
  options[branch_option] = branch;
  branch_name = string(branch);
  options_changed = true;
}

void app_state::write_options()
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
