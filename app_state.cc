// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstdlib>              // for strtoul()
#include <iostream>
#include <string>
#include <vector>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include "botan/pubkey.h"
#include "botan/rsa.h"

#include "app_state.hh"
#include "charset.hh"
#include "database.hh"
#include "file_io.hh"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"
#include "work.hh"

using std::exception;
using std::map;
using std::string;
using std::vector;
using std::vector;

static string const database_option("database");
static string const branch_option("branch");
static string const key_option("key");
static string const keydir_option("keydir");

app_state::app_state()
  : branch_name(""), db(system_path()),
    keys(this), recursive(false),
    stdhooks(true), rcfiles(true), diffs(false),
    no_merges(false), set_default(false),
    verbose(false), date_set(false),
    search_root(current_root_path()),
    depth(-1), last(-1), next(-1),
    diff_format(unified_diff), diff_args_provided(false),
    diff_show_encloser(true),
    execute(false), bind_address(""), bind_port(""),
    bind_stdio(false), use_transport_auth(true),
    missing(false), unknown(false), brief(false),
    confdir(get_default_confdir()),
    have_set_key_dir(false), no_files(false),
    requested_help(false),
    automate_stdio_size(1024)
{
  db.set_app(this);
  lua.set_app(this);
  keys.set_key_dir(confdir / "keys");
  set_prog_name(utf8(string("mtn")));
}

app_state::~app_state()
{
}

void
app_state::set_is_explicit_option (std::string o)
{
  explicit_options.insert(o);
}

bool
app_state::is_explicit_option(std::string o) const
{
  return explicit_options.find(o) != explicit_options.end();
}

void
app_state::allow_workspace()
{
  L(FL("initializing from directory %s") % fs::initial_path().string());
  found_workspace = find_and_go_to_workspace(search_root);

  if (found_workspace)
    {
      // We read the options, but we don't process them here.  That's
      // done with process_options().
      read_options();

      if (global_sanity.filename.empty())
        {
          bookkeeping_path dump_path;
          get_local_dump_path(dump_path);
          L(FL("setting dump path to %s") % dump_path);
          // The 'true' means that, e.g., if we're running checkout,
          // then it's okay for dumps to go into our starting working
          // dir's _MTN rather than the new workspace dir's _MTN.
          global_sanity.filename = system_path(dump_path, false).as_external();
        }
    }
  load_rcfiles();
}

void
app_state::process_options()
{
  if (found_workspace) {
    if (!options[database_option]().empty())
      {
        system_path dbname = system_path(options[database_option]);
        db.set_filename(dbname);
      }

    if (!options[keydir_option]().empty())
      {
        system_path keydir = system_path(options[keydir_option]);
        set_key_dir(keydir);
      }

    if (branch_name().empty() && !options[branch_option]().empty())
      branch_name = options[branch_option];

    L(FL("branch name is '%s'") % branch_name());

          if (!options[key_option]().empty())
                  internalize_rsa_keypair_id(options[key_option],
                                             signing_key);
  }
}

void
app_state::require_workspace(string const & explanation)
{
  N(found_workspace,
    F("workspace required but not found%s%s")
    % (explanation.empty() ? "" : "\n") % explanation);
  write_options();
}

void
app_state::create_workspace(system_path const & new_dir)
{
  N(!new_dir.empty(), F("invalid directory ''"));

  L(FL("creating workspace in %s") % new_dir);

  mkdir_p(new_dir);
  go_to_workspace(new_dir);

  N(!directory_exists(bookkeeping_root),
    F("monotone bookkeeping directory '%s' already exists in '%s'")
    % bookkeeping_root % new_dir);

  L(FL("creating bookkeeping directory '%s' for workspace in '%s'")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  make_branch_sticky();

  write_options();

  blank_user_log();

  if (lua.hook_use_inodeprints())
    enable_inodeprints();

  load_rcfiles();
}

void
app_state::set_database(system_path const & filename)
{
  if (!filename.empty()) db.set_filename(filename);

  options[database_option] = filename.as_internal();
}

void
app_state::set_key_dir(system_path const & filename)
{
  if (!filename.empty())
    {
      keys.set_key_dir(filename);
      have_set_key_dir = true;
    }

  options[keydir_option] = filename.as_internal();
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
  if (found_workspace)
    {
      // Already have a workspace, can (must) write options directly,
      // because no-one else will do so. If we don't have a workspace
      // yet, then require_workspace (for instance) will call
      // write_options when it finds one.
      write_options();
    }
}

void
app_state::set_signing_key(utf8 const & key)
{
  internalize_rsa_keypair_id(key, signing_key);

  options[key_option] = key;
}

void
app_state::add_key_to_push(utf8 const & key)
{
  rsa_keypair_id k;
  internalize_rsa_keypair_id(key, k);
  keys_to_push.push_back(k);
}

void
app_state::set_root(system_path const & path)
{
  require_path_is_directory
    (path,
     F("search root '%s' does not exist") % path,
     F("search root '%s' is not a directory\n") % path);
  search_root = path;
  L(FL("set search root to %s") % search_root);
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
  try
    {
      // boost::posix_time can parse "basic" ISO times, of the form
      // 20000101T120000, but not "extended" ISO times, of the form
      // 2000-01-01T12:00:00. So convert one to the other.
      string tmp = d();
      string::size_type pos = 0;
      while ((pos = tmp.find_first_of("-:")) != string::npos)
        tmp.erase(pos, 1);
      date = boost::posix_time::from_iso_string(tmp);
      date_set = true;
    }
  catch (exception &e)
    {
      N(false, F("failed to parse date string '%s': %s")
        % d % e.what());
    }
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
    F("illegal argument to --last: cannot be zero or negative\n"));
  last = l;
}

void
app_state::set_next(long l)
{
  N(l > 0,
    F("illegal argument to --next: cannot be zero or negative\n"));
  next = l;
}

void
app_state::set_pidfile(system_path const & p)
{
  pidfile = p;
}

void
app_state::add_revision(utf8 const & selector)
{
  revision_selectors.push_back(selector);
}

void
app_state::add_exclude(utf8 const & exclude_pattern)
{
  exclude_patterns.push_back(exclude_pattern);
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
app_state::set_recursive(bool r)
{
  recursive = r;
}

void
app_state::set_prog_name(utf8 const & name)
{
  prog_name = name;
  ui.set_prog_name(name());
}

void
app_state::add_rcfile(utf8 const & filename)
{
  extra_rcfiles.push_back(filename);
}

void
app_state::set_confdir(system_path const & cd)
{
  confdir = cd;
  if (!have_set_key_dir)
    keys.set_key_dir(cd / "keys");
}

void
app_state::set_automate_stdio_size(long size)
{
  N(size > 0,
    F("illegal argument to --automate-stdio-size: cannot be zero or negative\n"));
  automate_stdio_size = (size_t)size;
}

system_path
app_state::get_confdir()
{
  return confdir;
}

// rc files are loaded after we've changed to the workspace so that
// _MTN/monotonerc can be loaded between ~/.monotone/monotonerc and other
// rcfiles.

void
app_state::load_rcfiles()
{
  // Built-in rc settings are defaults.

  if (stdhooks)
    lua.add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
  // _MTN/monotonerc overrides *that*.

  if (rcfiles)
    {
      system_path default_rcfile;
      bookkeeping_path workspace_rcfile;
      lua.default_rcfilename(default_rcfile);
      lua.workspace_rcfilename(workspace_rcfile);
      lua.load_rcfile(default_rcfile, false);
      lua.load_rcfile(workspace_rcfile, false);
    }

  // Command-line rcfiles override even that.

  for (vector<utf8>::const_iterator i = extra_rcfiles.begin();
       i != extra_rcfiles.end(); ++i)
    {
      lua.load_rcfile(*i);
    }
}

void
app_state::read_options()
{
  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      if (path_exists(o_path))
        {
          data dat;
          read_data(o_path, dat);
          read_options_map(dat, options);
        }
    }
  catch(exception &)
    {
      W(F("Failed to read options file %s") % o_path);
    }
}

void
app_state::write_options()
{
  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      data dat;
      write_options_map(dat, options);
      write_data(o_path, dat);
    }
  catch(exception &)
    {
      W(F("Failed to write options file %s") % o_path);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
