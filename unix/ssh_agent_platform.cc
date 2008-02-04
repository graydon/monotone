// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../sanity.hh"

#include "ssh_agent_platform.hh"

using Netxx::Stream;
using Netxx::socket_type;
using std::min;
using std::string;

// helper function for constructor
socket_type
ssh_agent_platform::connect() 
{
  const char *authsocket;
  struct sockaddr_un sunaddr;
  socket_type sock;

  authsocket = getenv("SSH_AUTH_SOCK");
  
  if (!authsocket || !strlen(authsocket))
    {
      L(FL("ssh_agent: connect: ssh-agent socket not found"));
      return -1;
    }

  sunaddr.sun_family = AF_UNIX;
  strncpy(sunaddr.sun_path, authsocket, sizeof(sunaddr.sun_path));

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    {
      W(F("ssh_agent: connect: could not open socket to ssh-agent"));
      return -1;
    }

  int ret = fcntl(sock, F_SETFD, FD_CLOEXEC);
  if (ret == -1)
    {
      close(sock);
      W(F("ssh_agent: connect: could not set up socket for ssh-agent"));
      return -1;
    }
  ret = ::connect(sock, (struct sockaddr *)&sunaddr, sizeof sunaddr);
  if (ret < 0)
    {
      close(sock);
      W(F("ssh_agent: connect: could not connect to socket for ssh-agent"));
      return -1;
    }

  return sock;
}

void
ssh_agent_platform::write_data(string const & data)
{
  L(FL("ssh_agent_platform::write_data: asked to write %u bytes")
    % data.length());
  I(connected());
  stream.write(data.c_str(), data.length());
}

void 
ssh_agent_platform::read_data(u32 const len, string & out)
{
  int ret;
  const u32 bufsize = 4096;
  char read_buf[bufsize];
  u32 get = len;
  L(FL("ssh_agent: read_data: asked to read %u bytes") % len);
  I(connected());

  while (get > 0)
    {
      ret = stream.read(read_buf, min(get, bufsize));
      E(ret >= 0, F("stream read failed (%i)") % ret);
      out.append(read_buf, ret);
      get -= ret;
    }
  E(get == 0, F("%u extra bytes from ssh-agent") % -get);
}
