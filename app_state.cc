#include <iostream>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>

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
  db.set_app(this);
}

app_state::~app_state()
{
}

// search for working copy
// working_copy true  ==> MT/options must exist and is read and written
// working_copy false ==> MT/options may  exist and is read for defaults

// NB: this is not done in the constructor because checkout and setup
// must not do any searching for the MT dir and associated directory
// changing

// FIXME: this should be revisited, requiring explicit calls to
// app.initialize(true/false) for every command seems rather primitive

void
app_state::initialize(bool working_copy)
{
  fs::path root;
  fs::path subdir;

  // a lua hook or --root option here that returns a directory prefix
  // or regex to stop the directory search might be good

  bool found = find_working_copy(root, subdir);

  N(!working_copy || found, F("working copy directory required but not found"));

  if (found) 
    {
      L(F("initializing from directory %s\n") % fs::initial_path().string());
      L(F("found working copy directory %s\n") % root.string());
      N(chdir(root.native_directory_string().c_str()) != -1,
        F("cannot change to directory to %s\n") % root.native_directory_string());

      read_options();

      string dbname = absolutify(options[database_option]());
      if (dbname != "") db.set_filename(mkpath(dbname));
      branch_name = options[branch_option];
      internalize_rsa_keypair_id(options[key_option], signing_key);

      if (working_copy) write_options();

      if (!subdir.empty()) 
        {
          relative_directory = file_path(subdir.native_directory_string());
          L(F("relative directory is '%s'\n") % relative_directory());
        }
    }
  load_rcfiles();
}

// create new working copy, create MT dir and write MT/options

void
app_state::initialize(std::string const & dir)
{
  {
    fs::path new_dir = mkpath(dir);
    try
      {
        fs::create_directories(new_dir);
      }
    catch (fs::filesystem_error & err)
      {
        N(false,
          F("could not create directory: %s: %s\n")
          % err.path1().native_directory_string()
          % strerror(err.native_error()));
      }
    N(chdir(new_dir.native_directory_string().c_str()) != -1,
      F("cannot change to directory %s\n") % new_dir.native_directory_string());
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
  fs::path p1 = mkpath(relative_directory()) / mkpath(path());
  file_path p2(p1.normalize().native_directory_string());
  L(F("'%s' prefixed to '%s'\n") % path() % p2());
  return p2;
}

void 
app_state::add_restriction(utf8 const & path)
{
  file_path p = prefix(path);
  L(F("'%s' added to restricted path set\n") % p());
  restrictions.insert(p);
}

bool
app_state::restriction_includes(file_path const & path)
{
  if (restrictions.empty()) 
    {
      L(F("empty restricted path set; '%s' included\n") % path());
      return true;
    }
  
  // a path that normalizes to "." means that the restriction has been
  // essentially cleared (all files are included). rather than be
  // careful about what goes in to the restricted path set we just
  // check for this special case here.

  if (restrictions.find(file_path(".")) != restrictions.end())
    {
      L(F("restricted path set cleared; '%s' included\n") % path());
      return true;
    }

  fs::path test = mkpath(path());

  while (!test.empty()) 
    {
      L(F("checking restricted path set for '%s'\n") % test.string());

      file_path p(test.string());
      path_set::const_iterator i = restrictions.find(p);

      if (i != restrictions.end()) 
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
app_state::add_revision(utf8 const & selector)
{
  revision_selectors.push_back(selector);
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
      lua.load_rcfile(default_rcfile, false);
      lua.load_rcfile(working_copy_rcfile, false);
    }

  // command-line rcfiles override even that

  for (vector<utf8>::const_iterator i = extra_rcfiles.begin();
       i != extra_rcfiles.end(); ++i)
    {
      lua.load_rcfile(mkpath((*i)()), true);
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
