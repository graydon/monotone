#ifndef __SSH_AGENT_H__
#define __SSH_AGENT_H__

#include "netxx/stream.h"
#include "botan/rsa.h"
#include "botan/bigint.h"
#include <boost/shared_ptr.hpp>
#include <vector>

using Botan::RSA_PublicKey;
using Botan::BigInt;
using Netxx::Stream;
using boost::shared_ptr;
using std::string;
using std::vector;

class ssh_agent
{
public:
  ssh_agent();
  void connect();
  vector<RSA_PublicKey> const get_keys();
  void sign_data(RSA_PublicKey const key, string const data, string & out);

private:
  shared_ptr<Stream> stream;
  vector<RSA_PublicKey> keys;

  unsigned long get_long(char const buf[4]);
  unsigned long get_long_from_buf(string const buf, unsigned long &loc);
  void get_string_from_buf(string const buf, unsigned long &loc, unsigned long &len, string &out);
  void put_key_into_buf(RSA_PublicKey const key, string & buf);
  void put_string_into_buf(string const str, string & buf);
  void put_bigint_into_buf(BigInt bi, string & buf);
  void put_long(unsigned long l, char buf[4]);
  void put_long_into_buf(unsigned long l, string & buf);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
