// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include "ssh_agent_platform.hh"

using std::string;

bool
ssh_agent_platform::connect() 
{
  return false;
}

bool
ssh_agent_platform::disconnect()
{
  return false;
}


bool
ssh_agent_platform::connected()
{
  return false;
}

void
ssh_agent_platform::write_data(string const & data)
{
}

void 
ssh_agent_platform::read_data(u32 const len, string & out)
{
}
