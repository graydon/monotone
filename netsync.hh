#ifndef __NETSYNC_H__
#define __NETSYNC_H__

#include <vector>

#include "app_state.hh"
#include "vocab.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

typedef enum 
  { 
    source_role = 1, 
    sink_role = 2, 
    source_and_sink_role = 3
  }
protocol_role;

typedef enum
  {
    server_voice,
    client_voice
  }
protocol_voice;

void run_netsync_protocol(protocol_voice voice, 
			  protocol_role role, 
			  utf8 const & addr, 
			  std::vector<utf8> collections,
			  app_state & app);

#endif // __NETSYNC_H__
