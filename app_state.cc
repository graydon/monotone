#include <iostream>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "database.hh"
#include "file_io.hh"
#include "sanity.hh"
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
  : branch_name(""), db(""), stdhooks(true), rcfiles(true)
{
}

app_state::~app_state()
{
}

// working_copy true  MT/options must exist and is read and written
// working_copy false MT/options may  exist and is read for defaults

void
app_state::initialize(bool working_copy)
{
  fs::path root;
  fs::path subdir;

  bool found = find_working_copy(root, subdir);

  N(!working_copy || found, F("working copy directory required but not found"));

  if (found) 
    {
      L(F("initializing from directory %s\n") % fs::initial_path().string());
      L(F("found working copy directory %s\n") % root.string());
      chdir(root.native_directory_string().c_str());

      read_options();

      string dbname = absolutify(options[database_option]());
      if (dbname != "") db.set_filename(mkpath(dbname));
      branch_name = options[branch_option];
      internalize_rsa_keypair_id(options[key_option], signing_key);

      if (working_copy) write_options();

      // is there any point to building a subdir restriction for 
      // non-working copy commands?

      if (!subdir.empty()) 
        {
          // we want the trailing directory separator on a subdir restriction
          // so that a directory restriction like lua/ doesn't match files
          // like lua.cc

          // this is a poor attempt at a portable way of getting this
          subdir /= "a";
          string prefix = subdir.native_directory_string();
          prefix = prefix.substr(0, prefix.length()-1);

          L(F("sub-directory restriction is '%s'\n") % prefix);

          // add subdir prefix to all existing restrictions

          for (vector<restriction>::iterator i = restrictions.begin();
               i != restrictions.end(); ++i)
            {
              i->second = prefix + i->second();
              L(F("adjusted restriction is '%s'\n") % i->second);
            }

          // add initial include restriction for subdir prefix

          if (restrictions.empty() || restrictions.begin()->first) 
            {
              restrictions.insert(restrictions.begin(), make_pair(false, prefix));
            }
        }
      else
        {
          L(F("sub-directory restriction is empty\n"));
        }
    }

  load_rcfiles();
}

// new working copy... create MT dir and write MT/options

void
app_state::initialize(std::string const & dir)
{
  if (dir != string("."))
    {
      fs::path co_dir = mkpath(dir);
      fs::create_directories(co_dir);
      chdir(co_dir.native_directory_string().c_str());
    }

  local_path mt(book_keeping_dir);

  N(!directory_exists(mt),
    F("monotone book-keeping directory '%s' already exists in '%s'\n") 
    % book_keeping_dir % dir);

  L(F("creating book-keeping directory '%s' for working copy in '%s'\n") 
    % book_keeping_dir % dir);

  mkdir_p(mt);

  write_options();

  load_rcfiles();
}

void 
app_state::set_database(utf8 const & filename)
{
  string dbname = absolutify(filename());
  if (dbname != "") db.set_filename(mkpath(dbname));

  options[database_option] = utf8(dbname);
}

void 
app_state::set_branch(utf8 const & branch)
{
  branch_name = branch();

  options[branch_option] = branch;
}

void 
app_state::set_signing_key(utf8 const & key)
{
  internalize_rsa_keypair_id(key, signing_key);

  options[key_option] = key;
}

void
app_state::set_stdhooks(bool b)
{
  stdhooks = b;
}

void
app_state::set_rcfiles(bool b)
{
  rcfiles = b;
}

void
app_state::add_rcfile(utf8 const & filename)
{
  extra_rcfiles.push_back(filename);
}

void
app_state::add_restriction(bool restrict, utf8 const & path)
{
  L(F("%s %s\n") % (restrict ? "exclude" : "include") % path);
  restrictions.push_back(make_pair(restrict, path));
}

bool
app_state::is_restricted(file_path const & path)
{
  if (restrictions.empty()) return false;

  vector<restriction>::const_iterator i = restrictions.begin();

  // set the initial status to the inverse of the first element 
  // in the list so that "--include something" means everything else
  // is excluded and "--exclude something" means everything else is
  // included

  bool status = !i->first;

  L(F("%s %s\n") % path() % (status ? "excluded" : "included"));

  for (; i != restrictions.end(); ++i)
    {
      if (path().compare(0, i->second().length(), i->second()) == 0)
        {
          status = i->first;
          L(F("%s %s by %s\n") % path() % (status ? "excluded" : "included") % i->second);
        }
    }

  return status;
}

// rc files are loaded after we've changed to the working copy
// directory so that MT/monotonerc can be loaded between .monotonerc
// and other rcfiles

void
app_state::load_rcfiles()
{
  // built-in rc settings are defaults

  if (stdhooks)
    lua.add_std_hooks();

  // ~/.monotonerc overrides that, and
  // MT/monotonerc overrides *that*

  if (rcfiles)
    {
      fs::path default_rcfile;
      fs::path working_copy_rcfile;
      lua.default_rcfilename(default_rcfile);
      lua.working_copy_rcfilename(working_copy_rcfile);
      lua.load_rcfile(default_rcfile);
      lua.load_rcfile(working_copy_rcfile);
    }

  // command-line rcfiles override even that

  for (vector<utf8>::const_iterator i = extra_rcfiles.begin();
       i != extra_rcfiles.end(); ++i)
    {
      lua.load_rcfile(mkpath((*i)()));
    }
}

void
app_state::read_options()
{
  local_path o_path;
  get_options_path(o_path);

  if (file_exists(o_path))
    {
      data dat;
      read_data(o_path, dat);
      read_options_map(dat, options);
    }
}

void 
app_state::write_options()
{
  local_path o_path;
  get_options_path(o_path);

  data dat;
  write_options_map(dat, options);
  write_data(o_path, dat);
}
