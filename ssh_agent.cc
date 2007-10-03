// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <stdlib.h>

#include "ssh_agent.hh"
#include "sanity.hh"
#include "netio.hh"
#include "keys.hh"
#include "botan/numthry.h"

using Botan::RSA_PublicKey;
using Botan::RSA_PrivateKey;
using Botan::BigInt;
using Botan::SecureVector;
using Netxx::Stream;
using boost::shared_ptr;
using std::string;
using std::vector;

/*
 * The ssh-agent network format is essentially based on a u32 which
 * is the length of the packet followed by that number of bytes.
 *
 * u32 encoding is big-endian
 *
 * The packet to ask for the keys that ssh-agent has is in this format:
 * u32     = 1
 * command = 11
 *
 * The response packet:
 * u32 = length
 * data
 *  byte = packet type (12)
 *  u32  = number of keys
 *   u32 = length of key
 *   data
 *    u32  = length of type
 *    data = string, the type of key (ssh-rsa, ssh-dss)
 *    if(rsa)
 *     u32  = length of 'e'
 *     data = binary encoded BigInt, 'e'
 *     u32  = length of 'n'
 *     data = binary encoded BigInt, 'n'
 *    if(dss)
 *     u32  = length of 'p'
 *     data = binary encoded BigInt, 'p'
 *     u32  = length of 'q'
 *     data = binary encoded BigInt, 'q'
 *     u32  = length of 'g'
 *     data = binary encoded BigInt, 'g'
 *     u32  = length of 'pub_key'
 *     data = binary encoded BigInt, 'pub_key'
 *   u32  = length of comment
 *   data = comment (path to key file)
 *  (repeat for number of keys)
 *
 * To ask for ssh-agent to sign data use this packet format:
 * byte = packet type (13)
 * u32  = length of data
 * data
 *  u32  = length of key data
 *  key data
 *   (rsa)
 *    u32  = length of type
 *    data = type (ssh-rsa)
 *    u32  = length of 'e'
 *    data = binary encoded BigInt, 'e'
 *    u32  = length of 'n'
 *    data = binary encoded BigInt, 'n'
 *   (dss)
 *    NOT IMPLEMENTED, should be same as above
 *  u32  = length of data to sign
 *  data to sign
 *  u32  = flags (0)
 *
 * Response packet for signing request is:
 * u32  = length of packet
 * data
 *  byte = packet type (14)
 *  u32  = signature length
 *  data = signature
 *   u32  = type length
 *   data = type (ssh-rsa)
 *   u32  = signed data length
 *   data = signed data
 */

ssh_agent::ssh_agent()
{
  connect();
}

ssh_agent::~ssh_agent()
{
  disconnect();
}

u32
ssh_agent::get_long(char const * buf)
{
 L((FL("ssh_agent: get_long: %u %u %u %u")
    % widen<u32,char>(buf[0])
    % widen<u32,char>(buf[1])
    % widen<u32,char>(buf[2])
    % widen<u32,char>(buf[3])));
 return ((widen<u32,char>(buf[0]) << 24)
         | (widen<u32,char>(buf[1]) << 16)
         | (widen<u32,char>(buf[2]) << 8)
         | widen<u32,char>(buf[3]));
}

u32
ssh_agent::get_long_from_buf(string const & buf, u32 & loc)
{
  E(buf.length() >= loc + 4, F("string not long enough to get a long"));
  u32 ret = get_long(buf.data() + loc);
  //E(ret <= 2048, F("long is larger than expected"));
  loc += 4;
  return ret;
}

void
ssh_agent::get_string_from_buf(string const & buf,
                               u32 & loc,
                               u32 & len,
                               string & out)
{
  L(FL("ssh_agent: get_string_from_buf: buf length: %u, loc: %u" )
    % buf.length()
    % loc);
  len = get_long_from_buf(buf, loc);
  L(FL("ssh_agent: get_string_from_buf: len: %u" ) % len);
  E(loc + len <= buf.length(),
    F("ssh_agent: length (%i) of buf less than loc (%u) + len (%u)")
    % buf.length()
    % loc
    % len);
  out = buf.substr(loc, len);
  L(FL("ssh_agent: get_string_from_buf: out length: %u") % out.length());
  loc += len;
}

void
ssh_agent::put_long(u32 l, char * buf)
{
  buf[0] = (char)(unsigned char)(l >> 24);
  buf[1] = (char)(unsigned char)(l >> 16);
  buf[2] = (char)(unsigned char)(l >> 8);
  buf[3] = (char)(unsigned char)(l);
  L(FL("ssh_agent: long_to_buf: %u %u %u %u")
    % (u32)(unsigned char)buf[0]
    % (u32)(unsigned char)buf[1]
    % (u32)(unsigned char)buf[2]
    % (u32)(unsigned char)buf[3]);
}

void
ssh_agent::put_long_into_buf(u32 l, string & buf)
{
  char lb[4];
  L(FL("ssh_agent: put_long_into_buf: long: %u, buf len: %i")
    % l
    % buf.length());
  put_long(l, lb);
  buf.append(lb, 4);
  L(FL("ssh_agent: put_long_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_bigint_into_buf(BigInt const & bi, string & buf)
{
  L(FL("ssh_agent: put_bigint_into_buf: bigint.bytes(): %u, bigint: %s")
    % bi.bytes()
    % bi);
  SecureVector<Botan::byte> bi_buf = BigInt::encode(bi);
  string bi_str;
  if (*bi_buf.begin() & 0x80)
	  bi_str.append(1, static_cast<char>(0));
  bi_str.append((char *) bi_buf.begin(), bi_buf.size());
  put_string_into_buf(bi_str, buf);
  L(FL("ssh_agent: put_bigint_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_public_key_into_buf(RSA_PublicKey const & key, string & buf)
{
  L(FL("ssh_agent: put_public_key_into_buf: key e: %s, n: %s")
    % key.get_e()
    % key.get_n());
  put_string_into_buf("ssh-rsa", buf);
  put_bigint_into_buf(key.get_e(), buf);
  put_bigint_into_buf(key.get_n(), buf);
  L(FL("ssh_agent: put_public_key_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_private_key_into_buf(RSA_PrivateKey const & key, string & buf)
{
  L(FL("ssh_agent: put_private_key_into_buf: key e: %s, n: %s")
    % key.get_e()
    % key.get_n());
  put_string_into_buf("ssh-rsa", buf);
  put_bigint_into_buf(key.get_n(), buf);
  put_bigint_into_buf(key.get_e(), buf);
  put_bigint_into_buf(key.get_d(), buf);
  BigInt iqmp = inverse_mod(key.get_q(), key.get_p());
  put_bigint_into_buf(iqmp, buf);
  put_bigint_into_buf(key.get_p(), buf);
  put_bigint_into_buf(key.get_q(), buf);
  L(FL("ssh_agent: put_private_key_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::put_string_into_buf(string const & str, string & buf)
{
  L(FL("ssh_agent: put_string_into_buf: str len %i, buf len %i")
    % str.length()
    % buf.length());
  put_long_into_buf(str.length(), buf);
  buf.append(str.c_str(), str.length());
  L(FL("ssh_agent: put_string_into_buf: buf len now %i") % buf.length());
}

void
ssh_agent::fetch_packet(string & packet)
{
  u32 len;
  string len_buf;
  read_data(4, len_buf);
  u32 l = 0;
  len = get_long_from_buf(len_buf, l);
  E(len > 0, F("ssh_agent: fetch_packet: zero-length packet from ssh-agent"));

  L(FL("ssh_agent: fetch_packet: response len %u") % len);

  read_data(len, packet);
}

vector<RSA_PublicKey> const
ssh_agent::get_keys()
{
  if (!connected())
    {
      L(FL("ssh_agent: get_keys: stream not initialized, no agent"));
      return keys;
    }

  string out("\0\0\0\1", 4);
  char ch[1];
  ch[0] = 11;
  out.append(ch, 1);
  write_data(out);
  string packet;
  fetch_packet(packet);

  //first byte is packet type
  u32 packet_loc = 0;
  E(packet.at(0) == 12, F("ssh_agent: packet type (%u) != 12")
    % (u32)packet.at(0));
  packet_loc += 1;

  u32 num_keys = get_long_from_buf(packet, packet_loc);
  L(FL("ssh_agent: %u keys") % num_keys);

  for (u32 key_num = 0; key_num < num_keys; ++key_num)
    {
      L(FL("ssh_agent: getting key # %u") % key_num);

      u32 key_len;
      string key;
      get_string_from_buf(packet, packet_loc, key_len, key);

      u32 key_loc = 0, slen;
      string type;
      get_string_from_buf(key, key_loc, slen, type);

      L(FL("ssh_agent: type: %s") % type);

      if (type == "ssh-rsa")
        {
          L(FL("ssh_agent: RSA"));
          string e_str;
          get_string_from_buf(key, key_loc, slen, e_str);
          BigInt e = BigInt::decode((unsigned char *)(e_str.c_str()),
                                    e_str.length(),
                                    BigInt::Binary);
          L(FL("ssh_agent: e: %s, len %u") % e % slen);
          string n_str;
          get_string_from_buf(key, key_loc, slen, n_str);
          BigInt n = BigInt::decode((unsigned char *)(n_str.c_str()),
                                    n_str.length(),
                                    BigInt::Binary);
          L(FL("ssh_agent: n: %s, len %u") % n % slen);

          E(key.length() == key_loc,
            F("ssh_agent: get_keys: not all or too many key bytes consumed,"
              " location (%u), length(%i)")
            % key_loc
            % key.length());

          RSA_PublicKey rsa_key(n, e);
          keys.push_back(rsa_key);

        } else
          L(FL("ssh_agent: ignoring key of type '%s'") % type);

      //if (type == "ssh-dss")
      //  {
      //    L(FL("ssh_agent: DSA (ignoring)"));
      //    string p;
      //    get_string_from_buf(key, key_loc, slen, p);
      //    //BigInt pb = BigInt::decode((unsigned char *)(p.c_str()), slen, BigInt::Binary);
      //    //L(FL("ssh_agent: p: %s, len %u") % pb % slen);
      //    string q;
      //    get_string_from_buf(key, key_loc, slen, q);
      //    //BigInt qb = BigInt::decode((unsigned char *)(q.c_str()), slen, BigInt::Binary);
      //    //L(FL("ssh_agent: q: %s, len %u") % qb % slen);
      //    string g;
      //    get_string_from_buf(key, key_loc, slen, g);
      //    //BigInt gb = BigInt::decode((unsigned char *)(g.c_str()), slen, BigInt::Binary);
      //    //L(FL("ssh_agent: g: %s, len %u") % gb % slen);
      //    string pub_key;
      //    get_string_from_buf(key, key_loc, slen, pub_key);
      //    //BigInt pkb = BigInt::decode((unsigned char *)(pub_key.c_str()), slen, BigInt::Binary);
      //    //L(FL("ssh_agent: pub_key: %s, len %u") % pkb % slen);
      //  } else
      //    E(false, F("key type '%s' not recognized by ssh-agent code") % type);

      L(FL("ssh_agent: packet length %u, packet loc %u, key length %u,"
           " key loc, %u")
        % packet.length()
        % packet_loc
        % key.length()
        % key_loc);

      string comment;
      u32 comment_len;
      get_string_from_buf(packet, packet_loc, comment_len, comment);
      L(FL("ssh_agent: comment_len: %u, comment: %s") % comment_len % comment);
    }
  E(packet.length() == packet_loc,
    F("ssh_agent: get_keys: not all or too many packet bytes consumed,"
      " location (%u), length(%i)")
    % packet_loc
    % packet.length());
  return keys;
}

void
ssh_agent::sign_data(RSA_PublicKey const & key,
                     string const & data,
                     string & out)
{
  E(connected(),
    F("ssh_agent: get_keys: attempted to sign data when not connected"));

  L(FL("ssh_agent: sign_data: key e: %s, n: %s, data len: %i")
    % key.get_e()
    % key.get_n()
    % data.length());
  string data_out;
  string key_buf;
  string full_sig;
  unsigned char cmd[1];
  cmd[0] = 13;
  data_out.append((char *)cmd, 1);
  put_public_key_into_buf(key, key_buf);
  put_string_into_buf(key_buf, data_out);

  put_string_into_buf(data, data_out);
  u32 flags = 0;
  put_long_into_buf(flags, data_out);

  L(FL("ssh_agent: sign_data: data_out length: %u") % data_out.length());

  string packet_out;
  put_string_into_buf(data_out, packet_out);

  //stream->write(packet_out.c_str(), packet_out.length());
  write_data(packet_out);

  string packet_in;
  fetch_packet(packet_in);

  u32 packet_in_loc = 0;
  /*
  E(packet_in.at(0) == 14,
    (F("ssh_agent: sign_data: packet_in type (%u) != 14")
     % (u32)packet_in.at(0)));
  */
  if (packet_in.at(0) != 14) {
    L(FL("ssh_agent: sign_data: packet_in type (%u) != 14")
      % (u32)packet_in.at(0));
    return;
  }
  packet_in_loc += 1;

  u32 full_sig_len;
  get_string_from_buf(packet_in, packet_in_loc, full_sig_len, full_sig);
  L(FL("ssh_agent: sign_data: signed data length: %u (%u)")
    % full_sig_len
    % full_sig.length());

  string type;
  u32 full_sig_loc = 0, type_len, out_len;
  get_string_from_buf(full_sig, full_sig_loc, type_len, type);
  L(FL("ssh_agent: sign_data: type (%u), '%s'") % type_len % type);
  get_string_from_buf(full_sig, full_sig_loc, out_len, out);
  L(FL("ssh_agent: sign_data: output length %u") % out_len);
  E(full_sig.length() == full_sig_loc,
    (F("ssh_agent: sign_data: not all or too many signature bytes consumed,"
       " location (%u), length(%i)")
     % full_sig_loc
     % full_sig.length()));

  E(packet_in.length() == packet_in_loc,
    (F("ssh_agent: sign_data: not all or too many packet bytes consumed,"
       " location (%u), length(%i)")
     % packet_in_loc
     % packet_in.length()));
}

bool
ssh_agent::connected()
{
  return ssh_agent_platform::connected();
}

void
ssh_agent::add_identity(RSA_PrivateKey const & key, string const & comment)
{
  E(connected(),
    F("ssh_agent: add_identity: attempted to add a key when not connected"));

  L(FL("ssh_agent: add_identity: key e: %s, n: %s, comment len: %i")
    % key.get_e()
    % key.get_n()
    % comment.length());
  string data_out;
  string key_buf;
  unsigned char cmd[1];
  cmd[0] = 17;
  //data_out.append((char *)cmd, 1);
  key_buf.append((char *)cmd, 1);
  put_private_key_into_buf(key, key_buf);
  put_string_into_buf(comment, key_buf);
  //put_string_into_buf(key_buf, data_out);

  string packet_out;
  put_string_into_buf(key_buf, packet_out);

  //stream->write(packet_out.c_str(), packet_out.length());
  write_data(packet_out);

  string packet_in;
  fetch_packet(packet_in);
  u32 packet_in_loc = 0;
  E(packet_in.at(0) == 6, F("ssh_agent: packet type (%u) != 6")
    % (u32)packet_in.at(0));
  packet_in_loc += 1;

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

