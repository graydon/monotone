#ifndef __SSH_AGENT_H__
#define __SSH_AGENT_H__

#include "netxx/stream.h"
#include <boost/shared_ptr.hpp>

using Netxx::Stream;
using boost::shared_ptr;
using std::string;

class ssh_agent
{
public:
  ssh_agent();
  void connect();
  void get_keys();

  unsigned long get_long(char const buf[4]);
  unsigned long get_long_from_buf(string const buf, unsigned long &loc);
  void get_string_from_buf(string const buf, unsigned long &loc, unsigned long &len, string &out);

private:
  shared_ptr<Stream> stream;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
