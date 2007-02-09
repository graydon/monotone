#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sstream>

#include "ssh_agent.hh"
#include "sanity.hh"
#include "netio.hh"

using std::min;

ssh_agent::ssh_agent() {
}

void
ssh_agent::connect() {
  const char *authsocket;
  int sock;
  struct sockaddr_un sunaddr;

  authsocket = getenv("SSH_AUTH_SOCK");

  E(authsocket, F("agent: !authsocket"));

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
  stream = shared_ptr<Stream>(new Stream(sock));
}

u32
ssh_agent::get_long(char const * buf)
{
  L(FL("agent: get_long: %u %u %u %u")
    % (u32)((unsigned char)(buf)[0])
    % (u32)((unsigned char)(buf)[1])
    % (u32)((unsigned char)(buf)[2])
    % (u32)((unsigned char)(buf)[3]));
  return ((u32)((unsigned char)(buf)[0]) << 24)
    | ((u32)((unsigned char)(buf)[1]) << 16)
    | ((u32)((unsigned char)(buf)[2]) << 8)
    | ((u32)((unsigned char)(buf)[3]));
}

u32
ssh_agent::get_long_from_buf(string const & buf, u32 & loc)
{
  E(buf.length() >= loc + 4, F("string not long enough to get a long"));
  u32 ret = get_long(buf.data() + loc);
  E(ret <= 2048, F("long is larger than expected"));
  loc += 4;
  return ret;
}

void
ssh_agent::get_string_from_buf(string const & buf, u32 & loc, u32 & len, string & out)
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
ssh_agent::put_long(u32 l, char * buf) {
  buf[0] = (char)(unsigned char)(l >> 24);
  buf[1] = (char)(unsigned char)(l >> 16);
  buf[2] = (char)(unsigned char)(l >> 8);
  buf[3] = (char)(unsigned char)(l);
  L(FL("agent: long_to_buf: %u %u %u %u")
    % (u32)(unsigned char)buf[0]
    % (u32)(unsigned char)buf[1]
    % (u32)(unsigned char)buf[2]
    % (u32)(unsigned char)buf[3]);
}

void
ssh_agent::put_long_into_buf(u32 l, string & buf) {
  char lb[4];
  L(FL("agent: put_long_into_buf: long: %u, buf len: %i") % l % buf.length());
  put_long(l, lb);
  buf.append(lb, 4);
  L(FL("agent: put_long_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_bigint_into_buf(BigInt const & bi, string & buf) {
  int bytes = bi.bytes() + 1;
  Botan::byte bi_buf[bytes];
  L(FL("agent: put_bigint_into_buf: bigint.bytes(): %u, bigint: %s") % bi.bytes() % bi);
  bi_buf[0] = 0x00;
  BigInt::encode(bi_buf + 1, bi);
  int hasnohigh = (bi_buf[1] & 0x80) ? 0 : 1;
  string bi_str;
  bi_str.append((char *)(bi_buf + hasnohigh), bytes - hasnohigh);
  put_string_into_buf(bi_str, buf);
  bi_str[0] = bytes;
  L(FL("agent: put_bigint_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_key_into_buf(RSA_PublicKey const & key, string & buf) {
  L(FL("agent: put_key_into_buf: key e: %s, n: %s") % key.get_e() % key.get_n());
  put_string_into_buf("ssh-rsa", buf);
  put_bigint_into_buf(key.get_e(), buf);
  put_bigint_into_buf(key.get_n(), buf);
  L(FL("agent: put_key_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_string_into_buf(string const & str, string & buf) {
  L(FL("agent: put_string_into_buf: str len %i, buf len %i") % str.length() % buf.length());
  put_long_into_buf(str.length(), buf);
  buf.append(str.c_str(), str.length());
  L(FL("agent: put_string_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::read_num_bytes(u32 const len, string & out)
{
  int ret;
  const u32 bufsize = 4096;
  char read_buf[bufsize];
  u32 get = len;
  while (get > 0) {
    ret = stream->read(read_buf, min(get, bufsize));
    E(ret >= 0, F("stream read failed (%i)") % ret);
    if (ret > 0) {
      L(FL("agent: read_num_bytes: read %i bytes") % ret);
    }
    out.append(read_buf, ret);
    get -= ret;
  }
  L(FL("agent: read_num_bytes: get: %u") % get);
  L(FL("agent: read_num_bytes: length %u") % out.length());
}

u32
ssh_agent::fetch_packet(string & packet)
{
  u32 len;
  string len_buf;
  read_num_bytes(4, len_buf);
  u32 l = 0;
  len = get_long_from_buf(len_buf, l);
  E(len > 0, F("zero-length packet from ssh-agent"));

  L(FL("agent: get_keys response len %u") % len);

  read_num_bytes(len, packet);
  return len;
}

vector<RSA_PublicKey> const
ssh_agent::get_keys() {
  u32 len;

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

  string packet;
  len = fetch_packet(packet);

  //first byte is packet type
  u32 packet_loc = 0;
  E(packet.at(0) == 12, F("agent: packet type (%u) != 12") % (u32)packet.at(0));
  packet_loc += 1;

  u32 num_keys = get_long_from_buf(packet, packet_loc);
  L(FL("agent: %u keys") % num_keys);

  for (u32 key_num = 0; key_num < num_keys; ++key_num) {
    L(FL("agent: getting key # %u") % key_num);

    u32 key_len;
    string key;
    get_string_from_buf(packet, packet_loc, key_len, key);

    u32 key_loc = 0, slen;
    string type;
    get_string_from_buf(key, key_loc, slen, type);

    L(FL("agent: type: %s") % type);

    if (type == "ssh-rsa") {
      L(FL("agent: RSA"));
      string e_str;
      get_string_from_buf(key, key_loc, slen, e_str);
      BigInt e = BigInt::decode((unsigned char *)(e_str.c_str()), e_str.length(), BigInt::Binary);
      L(FL("agent: e: %s, len %u") % e % slen);
      string n_str;
      get_string_from_buf(key, key_loc, slen, n_str);
      BigInt n = BigInt::decode((unsigned char *)(n_str.c_str()), n_str.length(), BigInt::Binary);
      L(FL("agent: n: %s, len %u") % n % slen);

      RSA_PublicKey key(n, e);
      keys.push_back(key);

    } else
      L(FL("agent: ignoring key of type '%s'") % type);
      
//  if (type == "ssh-dss") {
//       L(FL("agent: DSA (ignoring)"));
//       string p;
//       get_string_from_buf(key, key_loc, slen, p);
//       //BigInt pb = BigInt::decode((unsigned char *)(p.c_str()), slen, BigInt::Binary);
//       //L(FL("agent: p: %s, len %u") % pb % slen);
//       string q;
//       get_string_from_buf(key, key_loc, slen, q);
//       //BigInt qb = BigInt::decode((unsigned char *)(q.c_str()), slen, BigInt::Binary);
//       //L(FL("agent: q: %s, len %u") % qb % slen);
//       string g;
//       get_string_from_buf(key, key_loc, slen, g);
//       //BigInt gb = BigInt::decode((unsigned char *)(g.c_str()), slen, BigInt::Binary);
//       //L(FL("agent: g: %s, len %u") % gb % slen);
//       string pub_key;
//       get_string_from_buf(key, key_loc, slen, pub_key);
//       //BigInt pkb = BigInt::decode((unsigned char *)(pub_key.c_str()), slen, BigInt::Binary);
//       //L(FL("agent: pub_key: %s, len %u") % pkb % slen);
//     } else {
//       E(false, F("key type '%s' not recognized by ssh-agent code") % type);
//     }

    L(FL("agent: packet length %u, packet loc %u, key length %u, key loc, %u")
      % packet.length()
      % packet_loc
      % key.length()
      % key_loc);

    string comment;
    u32 comment_len;
    get_string_from_buf(packet, packet_loc, comment_len, comment);
    L(FL("agent: comment_len: %u, comment: %s") % comment_len % comment);
  }
  return keys;
}

void
ssh_agent::sign_data(RSA_PublicKey const & key, string const & data, string & out) {
  L(FL("agent: sign_data: key e: %s, n: %s, data len: %i") % key.get_e() % key.get_n() % data.length());
  string data_out;
  string key_buf;
  unsigned char cmd[1];
  cmd[0] = 13;
  data_out.append((char *)cmd, 1);
  put_key_into_buf(key, key_buf);
  put_string_into_buf(key_buf, data_out);
  put_string_into_buf(data, data_out);
  u32 flags = 0;
  put_long_into_buf(flags, data_out);

  L(FL("agent: sign_data: data_out length: %u") % data_out.length());

  string packet_out;
  put_string_into_buf(data_out, packet_out);

  stream->write(packet_out.c_str(), packet_out.length());

  string packet_in;
  u32 len = fetch_packet(packet_in);

  u32 packet_in_loc = 0;
  E(packet_in.at(0) == 14, F("agent: packet_in type (%u) != 14") % (u32)packet_in.at(0));
  packet_in_loc += 1;

  u32 out_len;
  get_string_from_buf(packet_in, packet_in_loc, out_len, out);
  L(FL("agent: signed data length: %u (%u)") % out_len % out.length());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

