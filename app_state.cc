#include <iostream>
#include <string>
#include <vector>
#ifdef WIN32
#include <io.h> /* for chdir() */
#else
#include <unistd.h> /* for chdir() on POSIX */
#endif
#include <cstdlib>              // for strtoul()

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
  : branch_name(""), db(""), stdhooks(true), rcfiles(true), diffs(false),
    no_merges(false), set_default(false), verbose(false), search_root("/"),
    depth(-1), last(-1), diff_format(unified_diff), diff_args_provided(false)
{
  db.set_app(this);
}

app_state::~app_state()
{
}

void
app_state::allow_working_copy()
{
  fs::path root = mkpath(search_root());
  fs::path working;
  fs::path current;

  found_working_copy = find_working_copy(root, working, current);

  if (found_working_copy) 
    {
      L(F("initializing from directory %s\n") % fs::initial_path().string());
      L(F("found working copy directory %s\n") % working.string());
      N(chdir(working.native_directory_string().c_str()) != -1,
        F("cannot change to directory to %s\n") % working.native_directory_string());

      read_options();

      string dbname = absolutify(options[database_option]());
      if (dbname != "") db.set_filename(mkpath(dbname));
      if (branch_name().empty())
        branch_name = options[branch_option];
      L(F("branch name is '%s'\n") % branch_name());
      internalize_rsa_keypair_id(options[key_option], signing_key);

      if (!current.empty()) 
        {
          relative_directory = file_path(current.native_directory_string());
          L(F("relative directory is '%s'\n") % relative_directory());
        }

      if (global_sanity.filename == "")
        {
          local_path dump_path;
          get_local_dump_path(dump_path);
          L(F("setting dump path to %s\n") % dump_path);
          global_sanity.filename = dump_path();
        }
    }
  load_rcfiles();
}

void 
app_state::require_working_copy(std::string const & explanation)
{
  N(found_working_copy,
    F("working copy directory required but not found%s%s")
    % (explanation.empty() ? "" : "\n") % explanation);
  write_options();
}

void 
app_state::create_working_copy(std::string const & dir)
{
  N(dir.size(), F("invalid directory ''"));

  // cd back to where we started from
  N(chdir(fs::initial_path().native_directory_string().c_str()) != -1,
    F("cannot change to initial directory %s\n") 
    % fs::initial_path().native_directory_string());

  string target = absolutify(dir);
  L(F("create working copy in %s\n") % target);
  
  {
    fs::path new_dir = mkpath(target);
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
      F("cannot change to new directory %s\n") 
      % new_dir.native_directory_string());
    
    relative_directory = file_path();
  }

  local_path mt(book_keeping_dir);

  N(!directory_exists(mt),
    F("monotone book-keeping directory '%s' already exists in '%s'\n") 
    % book_keeping_dir % target);

  L(F("creating book-keeping directory '%s' for working copy in '%s'\n")
    % book_keeping_dir % target);

  mkdir_p(mt);

  make_branch_sticky();

  write_options();

  blank_user_log();

  if (lua.hook_use_inodeprints())
    enable_inodeprints();

  load_rcfiles();
}

file_path
app_state::prefix(utf8 const & path)
{
  fs::path p1 = mkpath(relative_directory()) / mkpath(path());
  file_path p2(p1.normalize().string());
  L(F("'%s' prefixed to '%s'\n") % path() % p2());
  return p2;
}

void 
app_state::set_restriction(path_set const & valid_paths, 
                           vector<utf8> const & paths,
                           bool respect_ignore)
{
  // this can't be a file-global static, because file_path's initializer
  // depends on another global static being defined.
  static file_path dot(".");
  restrictions.clear();
  for (vector<utf8>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      file_path p = prefix(*i);

      if (respect_ignore && lua.hook_ignore_file(p)) 
        {
          L(F("'%s' ignored by restricted path set\n") % p());
          continue;
        }

      N(p == dot || valid_paths.find(p) != valid_paths.end(),
        F("unknown path '%s'\n") % p());

      L(F("'%s' added to restricted path set\n") % p());
      restrictions.insert(p);
    }

  // if user supplied a depth but provided no paths 
  // assume current directory
  if ((depth != -1) && restrictions.empty()) 
    {
      restrictions.insert(dot);
    }
}

bool
app_state::restriction_includes(file_path const & path)
{
  // this can't be a file-global static, because file_path's initializer
  // depends on another global static being defined.
  static file_path dot(".");
  if (restrictions.empty()) 
    {
      return true;
    }

  bool user_supplied_depth = (depth != -1);

  // a path that normalizes to "." means that the restriction has been
  // essentially cleared (all files are included). rather than be
  // careful about what goes in to the restricted path set we just
  // check for this special case here.

  if ((!user_supplied_depth) && restrictions.find(dot) != restrictions.end())
    {
      return true;
    }

  fs::path test = mkpath(path());
  long branch_depth = 0;
  long max_depth = depth + 1;

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

      if (user_supplied_depth && (max_depth == branch_depth)) return false;
      test = test.branch_path();
      ++branch_depth;
    }

  if (user_supplied_depth && (restrictions.find(dot) != restrictions.end()))
    {
      return (branch_depth <= max_depth);
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
}

void
app_state::make_branch_sticky()
{
  options[branch_option] = branch_name();
}

void 
app_state::set_signing_key(utf8 const & key)
{
  internalize_rsa_keypair_id(key, signing_key);

  options[key_option] = key;
}

void 
app_state::set_root(utf8 const & path)
{
  search_root = absolutify(path());
  fs::path root = mkpath(search_root());
  N(fs::exists(root),
    F("search root '%s' does not exist\n") % search_root);
  N(fs::is_directory(root),
    F("search root '%s' is not a directory\n") % search_root);
  L(F("set search root to %s\n") % search_root);
}

void
app_state::set_message(utf8 const & m)
{
  message = m;
}

void
app_state::set_message_file(utf8 const & m)
{
  message_file = m;
}

void
app_state::set_date(utf8 const & d)
{
  date = d;
}

void
app_state::set_author(utf8 const & a)
{
  author = a;
}

void
app_state::set_depth(long d)
{
  N(d >= 0,
    F("negative depth not allowed\n"));
  depth = d;
}

void
app_state::set_last(long l)
{
  N(l > 0,
    F("negative or zero last not allowed\n"));
  last = l;
}

void
app_state::set_pidfile(utf8 const & p)
{
  pidfile = mkpath(p());
}

void
app_state::add_revision(utf8 const & selector)
{
  revision_selectors.push_back(selector);
}

void
app_state::add_exclude(utf8 const & exclude_pattern)
{
  exclude_patterns.insert(exclude_pattern);
}

void
app_state::set_diff_format(diff_type dtype)
{
  diff_format = dtype;
}

void
app_state::set_diff_args(utf8 const & args)
{
  diff_args_provided = true;
  diff_args = args;
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
app_state::set_verbose(bool b)
{
  verbose = b;
}

void
app_state::add_rcfile(utf8 const & filename)
{
  extra_rcfiles.push_back(filename);
}

// rc files are loaded after we've changed to the working copy directory so
// that MT/monotonerc can be loaded between ~/.monotone/monotonerc and other
// rcfiles

void
app_state::load_rcfiles()
{
  // built-in rc settings are defaults

  if (stdhooks)
    lua.add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
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
      lua.load_rcfile(*i);
    }
}

void
app_state::read_options()
{
  local_path o_path;
  get_options_path(o_path);
  try
    {
      if (file_exists(o_path))
        {
          data dat;
          read_data(o_path, dat);
          read_options_map(dat, options);
        }
    }
  catch(std::exception & e)
    {
      W(F("Failed to read options file %s") % o_path);
    }
}

void 
app_state::write_options()
{
  local_path o_path;
  get_options_path(o_path);
  try
    {
      data dat;
      write_options_map(dat, options);
      write_data(o_path, dat);
    }
  catch(std::exception & e)
    {
      W(F("Failed to write options file %s") % o_path);
    }
}
