// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "app_state.hh"
#include "sanity.hh"

using std::string;

app_state::app_state()
  : lua(this), work(lua),
    found_workspace(false),
    mtn_automate_allowed(false),
    branch_is_sticky(false)
{}

app_state::~app_state()
{}

void
app_state::process_options()
{
  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;

  if (!found_workspace)
    return;

  work.check_ws_format();
  work.get_ws_options(database_option, branch_option,
                      key_option, keydir_option);

  // Workspace options are not to override the command line.
  if (!opts.dbname_given)
    {
      I(opts.dbname.empty());
      opts.dbname = database_option;
    }

  if (!opts.key_dir_given && !opts.conf_dir_given)
    {
      I(opts.key_dir.empty());
      opts.key_dir = keydir_option;
    }

  if (opts.branchname().empty() && !branch_option().empty())
    {
      opts.branchname = branch_option;
      branch_is_sticky = true;
    }

  L(FL("branch name is '%s'") % opts.branchname);

  if (!opts.key_given)
    opts.signing_key = key_option;
}

void
app_state::write_options(bool branch_is_sticky)
{
  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;

  database_option = opts.dbname;
  keydir_option = opts.key_dir;

  if (branch_is_sticky || this->branch_is_sticky)
    branch_option = opts.branchname;

  if (opts.key_given)
    key_option = opts.signing_key;

  work.set_ws_options(database_option, branch_option,
                      key_option, keydir_option);
}

void
app_state::require_workspace(string const & explanation)
{
  N(found_workspace,
    F("workspace required but not found%s%s")
    % (explanation.empty() ? "" : "\n") % explanation);
  write_options(false);
}

void
app_state::create_workspace(system_path const & new_dir)
{
  N(!new_dir.empty(), F("invalid directory ''"));

  L(FL("creating workspace in %s") % new_dir);

  mkdir_p(new_dir);
  go_to_workspace(new_dir);
  mark_std_paths_used();

  N(!directory_exists(bookkeeping_root),
    F("monotone bookkeeping directory '%s' already exists in '%s'")
    % bookkeeping_root % new_dir);

  L(FL("creating bookkeeping directory '%s' for workspace in '%s'")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  write_options(true);

  work.write_ws_format();
  work.blank_user_log();

  if (lua.hook_use_inodeprints())
    work.enable_inodeprints();

  found_workspace = true;

  bookkeeping_path dump_path;
  work.get_local_dump_path(dump_path);
  // The 'false' means that, e.g., if we're running checkout,
  // then it's okay for dumps to go into our starting working
  // dir's _MTN rather than the new workspace dir's _MTN.
  global_sanity.set_dump_path(system_path(dump_path, false).as_external());

  lua.load_rcfiles(opts);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
