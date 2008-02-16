#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "paths.hh"
#include "options.hh"
#include "lua_hooks.hh"
#include "work.hh"

// This class is supposed to hold all (or.. well, most) of the state
// of the application, barring some unfortunate static objects like
// the debugging / logging system and the command objects, for the
// time being. The vague intent being to make life easier for anyone
// who wants to embed this program as a library someday.

class app_state
{
public:
  explicit app_state();
  ~app_state();

  options opts;
  lua_hooks lua;
  workspace work;

  bool found_workspace;
  bool mtn_automate_allowed;
  bool branch_is_sticky;

  void process_options();
  void require_workspace(std::string const & explanation = "");
  void create_workspace(system_path const & dir);
  void write_options(bool branch_is_sticky);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __APP_STATE_HH__
