#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sstream>

#include "ssh_agent.hh"
#include "sanity.hh"
#include "botan/bigint.h"
#include "netio.hh"

using Netxx::Stream;
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
ssh_agent::get_long(char const buf[4]) {
  return ((unsigned long)(unsigned char)(buf)[0] << 24)
    | ((unsigned long)(unsigned char)(buf)[1] << 16)
    | ((unsigned long)(unsigned char)(buf)[2] << 8)
    | ((unsigned long)(unsigned char)(buf)[3]);
}

void
ssh_agent::get_string_from_buf(std::string buf, size_t &loc, size_t &len, std::string &out) {
  len = get_long(buf.c_str() + loc);
  loc += 4;
  //L(FL("agent: get_string_from_buf %s, %i, %i" ) % buf % loc % len);
  //E(loc + 4 + len <= buf.length(), F("agent: length (%i) of string (%s) less than loc (%i) + len (%i)") % buf.length() % buf % loc % len);
  //char * buf = new char[len + 1];
  //buf.read(buf, len);
  out = buf.substr(loc, len);
  //L(FL("agent: len: %i") % len);
  //ret = new unsigned char[len + 1];
  //ret[len] = 0;
  /*
  for (unsigned int i = 0; i < len; ++i) {
    ret[i] = buf[loc + i + 4];
  }
  */
  loc += len;
  //return ret;
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

  L(FL("agent: len %i") % len);

  //L(FL("agent: ----ret: %i, len: %u, buf: %u %u %u %u") % ret % len % buf[0] % buf[1] % buf[2] % buf[3]);
  ret = stream->read(buf, 1);
  //L(FL("agent: ----ret: %i, buf: %u") % ret % buf[0]);
  E(buf[0] == 12, F("agent: !!!!return type != 12"));

  ret = stream->read(buf, 4);
  unsigned long num_keys = get_long(buf);
  size_t slen;
  //L(FL("agent: ----ret: ret %i, num_keys: %u") % ret % num_keys);

  for (unsigned long key_num = 0; key_num < num_keys; ++key_num) {
    L(FL("agent: getting key # %i") % key_num);

    ret = stream->read(buf, 4);
    unsigned long key_len = get_long(buf);
    //L(FL("agent: ----ret: ret %i, key_len: %u") % ret % key_len);

    //unsigned char * key = new unsigned char[key_len + 1];
    std::string key;
    char * read_buf = new char[key_len];

    //string key = string();
    long get = key_len;
    //buf[1] = 0;
    while (get > 0) {
      //ret = stream->read(buf, 1);
      //L(FL("agent: ----ret: %i, buf: %u") % ret % buf[0]);

      ret = stream->read(read_buf, get);
      L(FL("agent: ----ret: %i") % ret);//, buf: %u", ret, buf[0]);
      key.append(read_buf, ret);
      /*
      for (int i = 0; i < ret; ++i) {
        key[key_len - get + i] = read_buf[i];
      }
      */
      get -= ret;
    }
    //key[key_len] = 0;
    //L(FL("agent: get: %i") % get);

    delete read_buf;

    size_t loc = 0;
    std::string type;
    get_string_from_buf(key, loc, slen, type);

    L(FL("agent: type: %s") % type);

    if (type.compare("ssh-rsa") == 0) {
      L(FL("agent: RSA"));
      std::string e;
      get_string_from_buf(key, loc, slen, e);
      BigInt eb = BigInt::decode((unsigned char *)(e.c_str()), slen, BigInt::Binary);
      L(FL("agent: e: %s, len %u") % eb % slen);
      std::string n;
      get_string_from_buf(key, loc, slen, n);
      BigInt nb = BigInt::decode((unsigned char *)(n.c_str()), slen, BigInt::Binary);
      L(FL("agent: n: %s, len %u") % nb % slen);
    } else if (type.compare("ssh-dss") == 0) {
      L(FL("agent: DSA"));
      std::string p;
      get_string_from_buf(key, loc, slen, p);
      BigInt pb = BigInt::decode((unsigned char *)(p.c_str()), slen, BigInt::Binary);
      L(FL("agent: p: %s, len %u") % pb % slen);
      std::string q;
      get_string_from_buf(key, loc, slen, q);
      BigInt qb = BigInt::decode((unsigned char *)(q.c_str()), slen, BigInt::Binary);
      L(FL("agent: q: %s, len %u") % qb % slen);
      std::string g;
      get_string_from_buf(key, loc, slen, g);
      BigInt gb = BigInt::decode((unsigned char *)(g.c_str()), slen, BigInt::Binary);
      L(FL("agent: g: %s, len %u") % gb % slen);
      std::string pub_key;
      get_string_from_buf(key, loc, slen, pub_key);
      BigInt pkb = BigInt::decode((unsigned char *)(pub_key.c_str()), slen, BigInt::Binary);
      L(FL("agent: pub_key: %s, len %u") % pkb % slen);
    }

    ret = stream->read(buf, 4);
    key_len = get_long(buf);
    //L(FL("agent: ----ret: ret %i, key_len: %u") % ret % key_len);

    key = new char[key_len + 1];
    read_buf = new char[key_len];

    //string key = string();
    get = key_len;
    //buf[1] = 0;
    while (get > 0) {
      //ret = stream->read(buf, 1);
      //L(FL("agent: ----ret: %i, buf: %u") % ret % buf[0]);

      ret = stream->read(read_buf, get);
      //L(FL("agent: ----ret: %i") % ret);//, buf: %u", ret, buf[0]);
      for (long i = 0; i < ret; ++i) {
        key[key_len - get + i] = read_buf[i];
      }
      /*
        if (!buf[0]) {
        break;
        }
      */
      //key.append(buf);
      get -= ret;
    }
    key[key_len] = 0;
    L(FL("agent: get: %i, comment: %s") % get % key);
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

