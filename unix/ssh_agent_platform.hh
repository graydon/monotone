// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>
#include "../numeric_vocab.hh"
#include "../netxx/stream.h"

class ssh_agent_platform {
private:
  boost::shared_ptr<Netxx::Stream> stream;

public:
  bool connect();
  bool disconnect();
  bool connected();
  void write_data(std::string const & data);
  void read_data(u32 const len, std::string & out);
};
