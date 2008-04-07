// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../numeric_vocab.hh"
#include "../netxx/stream.h"

class ssh_agent_platform {
private:
  Netxx::Stream stream;
  Netxx::socket_type connect();

public:
  // We rely on Netxx::Stream not blowing up if constructed from an
  // invalid file descriptor, as long as no one actually tries to write()
  // or read() on it.
  ssh_agent_platform() : stream(connect()) {}
  bool connected() { return stream.get_socketfd() != -1; }

  void write_data(std::string const & data);
  void read_data(u32 const len, std::string & out);
};
