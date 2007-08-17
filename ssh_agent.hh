// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SSH_AGENT_H__
#define __SSH_AGENT_H__

#include "numeric_vocab.hh"
#include "netxx/stream.h"
#include "botan/rsa.h"
#include "botan/bigint.h"
#include <boost/shared_ptr.hpp>
#include "vector.hh"
#include "platform.hh"

class ssh_agent : ssh_agent_platform
{
public:
  ssh_agent();
  ~ssh_agent();
  std::vector<Botan::RSA_PublicKey> const get_keys();
  void sign_data(Botan::RSA_PublicKey const & key,
                 std::string const & data,
                 std::string & out);
  void add_identity(Botan::RSA_PrivateKey const & key, std::string const & comment);
  bool connected();

private:
  std::vector<Botan::RSA_PublicKey> keys;

  //helper functions for reading and unpacking data from ssh-agent
  void fetch_packet(std::string & packet);
  u32 get_long(char const * buf);
  u32 get_long_from_buf(std::string const & buf, u32 & loc);
  void get_string_from_buf(std::string const & buf,
                           u32 & loc,
                           u32 & len,
                           std::string & out);

  //helper functions for packing data to send to ssh-agent
  void put_long(u32 l, char * buf);
  void put_long_into_buf(u32 l, std::string & buf);
  void put_string_into_buf(std::string const & str, std::string & buf);
  void put_bigint_into_buf(Botan::BigInt const & bi, std::string & buf);
  void put_public_key_into_buf(Botan::RSA_PublicKey const & key, std::string & buf);
  void put_private_key_into_buf(Botan::RSA_PrivateKey const & key, std::string & buf);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
