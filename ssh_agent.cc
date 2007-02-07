#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sstream>

#include "ssh_agent.hh"
#include "sanity.hh"
#include "botan/bigint.h"
#include "netio.hh"

using Botan::BigInt;

ssh_agent::ssh_agent() {
}

void
ssh_agent::connect() {
  const char *authsocket;
  int sock;
  struct sockaddr_un sunaddr;

  unsigned int cmd = 11;

  authsocket = getenv("SSH_AUTH_SOCK");

  if (!authsocket) {
    E(authsocket, F("agent: !authsocket"));
    return;
  }

  sunaddr.sun_family = AF_UNIX;
  strncpy(sunaddr.sun_path, authsocket, sizeof(sunaddr.sun_path));

  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  E(sock >= 0, F("agent: sock < 0"));

  int ret = fcntl(sock, F_SETFD, 1);
  if (ret == -1) {
    close(sock);
    E(ret != -1, F("agent: fcntl == -1"));
    return;
  }
  ret = ::connect(sock, (struct sockaddr *)&sunaddr, sizeof sunaddr);
  if (ret < 0) {
    close(sock);
    E(ret >= 0, F("agent: connect < 0"));
  }
  //L(FL("agent: ----"));
  stream = shared_ptr<Stream>(new Stream(sock));
}

unsigned long
ssh_agent::get_long(char const buf[4])
{
  L(FL("agent: get_long: %u %u %u %u")
    % (unsigned long)((unsigned char)(buf)[0])
    % (unsigned long)((unsigned char)(buf)[1])
    % (unsigned long)((unsigned char)(buf)[2])
    % (unsigned long)((unsigned char)(buf)[3]));
  return ((unsigned long)((unsigned char)(buf)[0]) << 24)
    | ((unsigned long)((unsigned char)(buf)[1]) << 16)
    | ((unsigned long)((unsigned char)(buf)[2]) << 8)
    | ((unsigned long)((unsigned char)(buf)[3]));
}

unsigned long
ssh_agent::get_long_from_buf(string const buf, unsigned long &loc)
{
  get_long(buf.c_str() + loc);
  loc += 4;
}

void
ssh_agent::get_string_from_buf(string const buf, unsigned long &loc, unsigned long &len, string &out)
{
  L(FL("agent: get_string_from_buf: buf length: %u, loc: %u" ) % buf.length() % loc);
  len = get_long_from_buf(buf, loc);
  L(FL("agent: get_string_from_buf: len: %u" ) % len);
  E(loc + len <= buf.length(), F("agent: length (%i) of buf less than loc (%u) + len (%u)") % buf.length() % loc % len);
  out = buf.substr(loc, len);
  L(FL("agent: get_string_from_buf: out length: %u") % out.length());
  loc += len;
}

void
ssh_agent::get_keys() {
  unsigned int len;
  unsigned long ret;
  char buf[4];

  unsigned int ch;
  void * v = (void *)&ch;
  ch = 0;
  stream->write(v, 1);
  stream->write(v, 1);
  stream->write(v, 1);
  ch = 1;
  stream->write(v, 1);
  ch = 11;
  stream->write(v, 1);

  ret = stream->read(buf, 4);
  len = get_long(buf);

  L(FL("agent: len %u") % len);

  string packet;
  char * read_buf = new char[len];
  long get = len;
  while (get > 0) {
    ret = stream->read(read_buf, get);
    //L(FL("agent: ----ret: %i") % ret);
    packet.append(read_buf, ret);
    get -= ret;
  }
  L(FL("agent: get: %u") % get);
  delete read_buf;
  L(FL("agent: packet length %u") % packet.length());

  //L(FL("agent: ----ret: %i, len: %u, buf: %u %u %u %u") % ret % len % buf[0] % buf[1] % buf[2] % buf[3]);
  //ret = stream->read(buf, 1);
  //L(FL("agent: ----ret: %i, buf: %u") % ret % buf[0]);

  //first byte is packet type
  unsigned long packet_loc = 0;
  E(packet.at(0) == 12, F("agent: packet type != 12"));
  packet_loc += 1;

  unsigned long num_keys = get_long_from_buf(packet, packet_loc);
  //L(FL("agent: ----ret: ret %i, num_keys: %u") % ret % num_keys);

  for (unsigned long key_num = 0; key_num < num_keys; ++key_num) {
    L(FL("agent: getting key # %u") % key_num);

    //L(FL("agent: ----ret: ret %i, key_len: %u") % ret % key_len);

    unsigned long key_len;
    string key;
    get_string_from_buf(packet, packet_loc, key_len, key);

    unsigned long key_loc = 0, slen;
    string type;
    get_string_from_buf(key, key_loc, slen, type);

    L(FL("agent: type: %s") % type);

    if (type.compare("ssh-rsa") == 0) {
      L(FL("agent: RSA"));
      string e;
      get_string_from_buf(key, key_loc, slen, e);
      BigInt eb = BigInt::decode((unsigned char *)(e.c_str()), slen, BigInt::Binary);
      L(FL("agent: e: %s, len %u") % eb % slen);
      string n;
      get_string_from_buf(key, key_loc, slen, n);
      BigInt nb = BigInt::decode((unsigned char *)(n.c_str()), slen, BigInt::Binary);
      L(FL("agent: n: %s, len %u") % nb % slen);
    } else if (type.compare("ssh-dss") == 0) {
      L(FL("agent: DSA"));
      string p;
      get_string_from_buf(key, key_loc, slen, p);
      BigInt pb = BigInt::decode((unsigned char *)(p.c_str()), slen, BigInt::Binary);
      L(FL("agent: p: %s, len %u") % pb % slen);
      string q;
      get_string_from_buf(key, key_loc, slen, q);
      BigInt qb = BigInt::decode((unsigned char *)(q.c_str()), slen, BigInt::Binary);
      L(FL("agent: q: %s, len %u") % qb % slen);
      string g;
      get_string_from_buf(key, key_loc, slen, g);
      BigInt gb = BigInt::decode((unsigned char *)(g.c_str()), slen, BigInt::Binary);
      L(FL("agent: g: %s, len %u") % gb % slen);
      string pub_key;
      get_string_from_buf(key, key_loc, slen, pub_key);
      BigInt pkb = BigInt::decode((unsigned char *)(pub_key.c_str()), slen, BigInt::Binary);
      L(FL("agent: pub_key: %s, len %u") % pkb % slen);
    }

    L(FL("agent: packet length %u, packet loc %u, key length %u, key loc, %u")
      % packet.length()
      % packet_loc
      % key.length()
      % key_loc);

    string comment;
    unsigned long comment_len;
    get_string_from_buf(packet, packet_loc, comment_len, comment);
    L(FL("agent: comment_len: %u, comment: %s") % comment_len % comment);
    //L(FL("agent: \n\nkey:\n%s") % key.c_str());
    //L(FL("agent: %i left") % len - i);
    /*
      }
    */
  }
  exit(0);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

