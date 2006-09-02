#ifndef __NETSYNC_H__
#define __NETSYNC_H__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <vector>

#include "app_state.hh"
#include "netcmd.hh"
#include "vocab.hh"

typedef enum
  {
    server_voice,
    client_voice
  }
protocol_voice;

void run_netsync_protocol(protocol_voice voice,
                          protocol_role role,
                          utf8 const & addr,
                          utf8 const & include_pattern,
                          utf8 const & exclude_pattern,
                          app_state & app);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NETSYNC_H__
