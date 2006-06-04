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

#endif // __NETSYNC_H__
