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

// search for working copy
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

      if (!subdir.empty()) 
        {
          subdir_restriction = file_path(subdir.native_directory_string());
          L(F("sub-directory restriction is '%s'\n") % subdir_restriction());
        }
      else
        {
          L(F("sub-directory restriction is empty\n"));
        }
    }

  load_rcfiles();
}

// create new working copy, create MT dir and write MT/options

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

file_path
app_state::prefix(utf8 const & path)
{
  fs::path p1 = mkpath(subdir_restriction()) / mkpath(path());
  file_path p2(p1.normalize().native_directory_string());
  return p2;
}

void 
app_state::add_restriction(utf8 const & path)
{
  file_path p = prefix(path);
  L(F("adding '%s' to restricted path set as '%s'\n") % path() % p());
  path_restrictions.insert(p);
}

// subdir_restriction is only used here when there are no explicit
// path_restrictions. explicit path restrictions will all be prefixed
// with the contents of subdir_restriction as they are created.

// if subdir_restriction or any element of path_restrictions "matches"
// path it will be considered to be "in the restriction". note that
// matching is somewhat complicated by the fact that a restriction
// can be a directory or file path and if it's a directory we need
// to see if path is in this directory.

bool
app_state::in_restriction(file_path const & path)
{
  if (path_restrictions.empty()) 
    {
      fs::path subdir = mkpath(subdir_restriction());

      if (subdir.empty()) 
        {
          L(F("no restrictions: '%s' included'\n") % path());
          return true;
        }

      L(F("checking subdir restriction\n"));

      fs::path test = mkpath(path());

      while (!test.empty())
        {
          if (test.string() == subdir.string()) 
            {
              L(F("matched subdir restriction '%s' includes '%s'\n") 
                % subdir_restriction() % test.string());
              L(F("path '%s' found in restricted path set; '%s' included\n") 
                % test.string() % path());
              return true;
            }
          else
            {
              L(F("unmatched subdir restriction '%s' excludes '%s'\n") 
                % subdir_restriction() % test.string());
            }
          test = test.branch_path();
        }
      
      return false;
    }
  else
    {
      L(F("checking path restrictions\n"));

      fs::path test = mkpath(path());

      while (!test.empty()) 
        {
          L(F("test path is '%s'\n") % test.string());

          file_path p(test.string());
          set<file_path>::const_iterator i = path_restrictions.find(p);

          if (i != path_restrictions.end()) 
            {
              L(F("path '%s' found in restricted path set; '%s' included\n") 
                % test.string() % path());
              return true;
            }
          else
            {
              L(F("path '%s' not found in restricted path set; '%s' excluded\n") 
                % test.string() % path());
            }
          test = test.branch_path();
        }
      
      return false;
    }
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
app_state::set_message(utf8 const & m)
{
  message = m;
}

void
app_state::add_manifest(utf8 const & selector)
{
  manifest_selectors.push_back(selector);
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
