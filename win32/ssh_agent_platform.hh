// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../numeric_vocab.hh"

class ssh_agent_platform {
public:
  bool connect();
  bool disconnect();
  bool connected();
  void write_data(std::string const & data);
  void read_data(u32 const len, std::string & out);
};
