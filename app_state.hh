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

#include "options.hh"
#include "lua_hooks.hh"

// This class used to hold most of the state of the application (hence the
// name) but now it's just a wrapper around the options and lua_hooks
// objects, plus one bit of state needed by the Lua extension interfaces.

class app_state
{
public:
  explicit app_state();
  ~app_state();

  options opts;
  lua_hooks lua;
  bool mtn_automate_allowed;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __APP_STATE_HH__
