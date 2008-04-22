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

#include "vector.hh"
#include <boost/scoped_ptr.hpp>

struct keypair;

namespace Botan
{
  class RSA_PublicKey;
  class RSA_PrivateKey;
};

class ssh_agent_state;

struct ssh_agent
{
  ssh_agent();
  ~ssh_agent();
  std::vector<Botan::RSA_PublicKey> const get_keys();
  bool has_key(const keypair & key);
  void sign_data(Botan::RSA_PublicKey const & key,
                 std::string const & data,
                 std::string & out);
  void add_identity(Botan::RSA_PrivateKey const & key,
                    std::string const & comment);
  bool connected();

private:
  boost::scoped_ptr<ssh_agent_state> s;
};
  
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
