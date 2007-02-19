#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>

#include "../sanity.hh"

#include "ssh_agent_platform.hh"

using boost::shared_ptr;
using Netxx::Stream;
using std::min;
using std::string;

bool
ssh_agent_platform::connect() 
{
  const char *authsocket;
  int sock;
  struct sockaddr_un sunaddr;

  authsocket = getenv("SSH_AUTH_SOCK");
  
  if (!authsocket || !strlen(authsocket))
    {
      L(FL("ssh_agent: connect: ssh-agent socket not found"));
      return false;
    }

  sunaddr.sun_family = AF_UNIX;
  strncpy(sunaddr.sun_path, authsocket, sizeof(sunaddr.sun_path));

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    {
      W(F("ssh_agent: connect: could not open socket to ssh-agent"));
      return false;
    }

  int ret = fcntl(sock, F_SETFD, FD_CLOEXEC);
  if (ret == -1)
    {
      close(sock);
      W(F("ssh_agent: connect: could not set up socket for ssh-agent"));
      return false;
    }
  ret = ::connect(sock, (struct sockaddr *)&sunaddr, sizeof sunaddr);
  if (ret < 0)
    {
      close(sock);
      W(F("ssh_agent: connect: could not connect to socket for ssh-agent"));
      return false;
    }
  stream = shared_ptr<Stream>(new Stream(sock));
  return true;
}

bool
ssh_agent_platform::disconnect()
{
  if (connected())
    stream->close();
  return true;
}

bool
ssh_agent_platform::connected()
{
  return stream != NULL;
}

void
ssh_agent_platform::write_data(string const & data)
{
  stream->write(data.c_str(), data.length());
}

void 
ssh_agent_platform::read_data(u32 const len, string & out)
{
  int ret;
  const u32 bufsize = 4096;
  char read_buf[bufsize];
  u32 get = len;
  while (get > 0)
    {
      ret = stream->read(read_buf, min(get, bufsize));
      E(ret >= 0, F("stream read failed (%i)") % ret);
      if (ret > 0)
	L(FL("ssh_agent: read_num_bytes: read %i bytes") % ret);
      out.append(read_buf, ret);
      get -= ret;
    }
  L(FL("ssh_agent: read_num_bytes: get: %u") % get);
  L(FL("ssh_agent: read_num_bytes: length %u") % out.length());
}
