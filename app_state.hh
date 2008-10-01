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

#include <boost/shared_ptr.hpp>
#include "botan/rng.h"

#include "options.hh"
#include "lua_hooks.hh"

// This class holds any state that needs to be persistent across multiple
// commands, or be accessible to the lua hooks (which includes anything
// needed by mtn_automate()).

class app_state_private;
class database_impl;
class app_state
{
  boost::shared_ptr<app_state_private> _hidden;
public:
  explicit app_state();
  ~app_state();

  boost::shared_ptr<database_impl> &
  lookup_db(system_path const & f);

  options opts;
  lua_hooks lua;
  bool mtn_automate_allowed;
  boost::shared_ptr<Botan::RandomNumberGenerator> rng;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __APP_STATE_HH__
