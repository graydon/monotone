// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <utility>

#include "cryptopp/gzip.h"

#include "adler32.hh"
#include "constants.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace std;
using namespace boost;

static netcmd_item_type 
read_netcmd_item_type(string const & in, 
                      size_t & pos,
                      string const & name)
{
  u8 tmp = extract_datum_lsb<u8>(in, pos, name);
  switch (tmp)
    {
    case static_cast<u8>(revision_item):
      return revision_item;      
    case static_cast<u8>(manifest_item):
      return manifest_item;
    case static_cast<u8>(file_item):
      return file_item;
    case static_cast<u8>(cert_item):
      return cert_item;
    case static_cast<u8>(key_item):
      return key_item;      
    case static_cast<u8>(epoch_item):
      return epoch_item;      
    default:
      throw bad_decode(F("unknown item type 0x%x for '%s'") 
                       % static_cast<int>(tmp) % name);
    }
}

netcmd::netcmd() : version(constants::netcmd_current_protocol_version),
                   cmd_code(bye_cmd)
{}

netcmd::netcmd(u8 _version) : version(_version), cmd_code(bye_cmd)
{}

size_t netcmd::encoded_size() 
{
  string tmp;
  insert_datum_uleb128<size_t>(payload.size(), tmp);
  return 1 + 1 + tmp.size() + payload.size() + 4;
}

bool 
netcmd::operator==(netcmd const & other) const 
{
  return version == other.version &&
    cmd_code == other.cmd_code &&
    payload == other.payload;
}
  
void 
netcmd::write(string & out) const
{
  out += static_cast<char>(version);
  out += static_cast<char>(cmd_code);
  insert_variable_length_string(payload, out);
  adler32 check(reinterpret_cast<u8 const *>(payload.data()), payload.size());
  insert_datum_lsb<u32>(check.sum(), out);
}

// last should be zero (doesn't mean we're compatible with version 0).
// The nonzero elements are the historical netsync/netcmd versions we can
// interoperate with. For interoperating with newer versions, assume
// compatibility and let the remote host make the call.
static u8 const compatible_versions[] = {4, 0};

bool is_compatible(u8 version)
{
  for (u8 const *x = compatible_versions; *x; ++x)
    {
      if (*x == version)
        return true;
    }
  return false;
}
  
bool 
netcmd::read(string & inbuf)
{
  size_t pos = 0;

  if (inbuf.size() < constants::netcmd_minsz)
    return false;

  u8 ver = extract_datum_lsb<u8>(inbuf, pos, "netcmd protocol number");
  int v = version;

  u8 cmd_byte = extract_datum_lsb<u8>(inbuf, pos, "netcmd code");
  switch (cmd_byte)
    {
    // hello may be newer than expected, or one we're compatible with
    case static_cast<u8>(hello_cmd):
      if (ver < version && !is_compatible(ver))
        throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
                         % widen<u32,u8>(v) 
                         % widen<u32,u8>(ver));
      break;
    // these may be older compatible versions
    case static_cast<u8>(anonymous_cmd):
    case static_cast<u8>(auth_cmd):
      if (ver != version && (ver > version || !is_compatible(ver)))
        throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
                         % widen<u32,u8>(v) 
                         % widen<u32,u8>(ver));
      break;
    // these must match exactly what's expected
    case static_cast<u8>(error_cmd):
    case static_cast<u8>(bye_cmd):
    case static_cast<u8>(confirm_cmd):
    case static_cast<u8>(refine_cmd):
    case static_cast<u8>(done_cmd):
    case static_cast<u8>(send_data_cmd):
    case static_cast<u8>(send_delta_cmd):
    case static_cast<u8>(data_cmd):
    case static_cast<u8>(delta_cmd):
    case static_cast<u8>(nonexistant_cmd):
      if (ver != version)
        throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
                         % widen<u32,u8>(v) 
                         % widen<u32,u8>(ver));
      break;
    default:
      throw bad_decode(F("unknown netcmd code 0x%x") % widen<u32,u8>(cmd_byte));
    }
  cmd_code = static_cast<netcmd_code>(cmd_byte);
  version = ver;

  // check to see if we have even enough bytes for a complete uleb128
  size_t payload_len = 0;
  if (!try_extract_datum_uleb128<size_t>(inbuf, pos, "netcmd payload length",
      payload_len))
      return false;
  
  // they might have given us a bogus size
  if (payload_len > constants::netcmd_payload_limit)
    throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);
  
  // there might not be enough data yet in the input buffer
  if (inbuf.size() < pos + payload_len + sizeof(u32))
    {
      inbuf.reserve(pos + payload_len + sizeof(u32) + constants::bufsz);
      return false;
    }

//  out.payload = extract_substring(inbuf, pos, payload_len, "netcmd payload");
  // Do this ourselves, so we can swap the strings instead of copying.
  require_bytes(inbuf, pos, payload_len, "netcmd payload");
  inbuf.erase(0, pos);
  payload = inbuf.substr(payload_len);
  inbuf.erase(payload_len, inbuf.npos);
  inbuf.swap(payload);
  pos = 0;

  // they might have given us bogus data
  u32 checksum = extract_datum_lsb<u32>(inbuf, pos, "netcmd checksum");
  inbuf.erase(0, pos);
  adler32 check(reinterpret_cast<u8 const *>(payload.data()), 
                payload.size());
  if (checksum != check.sum())
    throw bad_decode(F("bad checksum 0x%x vs. 0x%x") % checksum % check.sum());

  return true;    
}

////////////////////////////////////////////
// payload reader/writer functions follow //
////////////////////////////////////////////

void 
netcmd::read_error_cmd(std::string & errmsg) const
{
  size_t pos = 0;
  // syntax is: <errmsg:vstr>
  extract_variable_length_string(payload, errmsg, pos, "error netcmd, message");
  assert_end_of_buffer(payload, pos, "error netcmd payload");
}

void 
netcmd::write_error_cmd(std::string const & errmsg)
{
  cmd_code = error_cmd;
  payload.clear();
  insert_variable_length_string(errmsg, payload);
}


void 
netcmd::read_hello_cmd(rsa_keypair_id & server_keyname,
                       rsa_pub_key & server_key,
                       id & nonce) const
{
  size_t pos = 0;
  // syntax is: <server keyname:vstr> <server pubkey:vstr> <nonce:20 random bytes>
  string skn_str, sk_str;
  extract_variable_length_string(payload, skn_str, pos,
                                 "hello netcmd, server key name");
  server_keyname = rsa_keypair_id(skn_str);
  extract_variable_length_string(payload, sk_str, pos,
                                 "hello netcmd, server key");
  server_key = rsa_pub_key(sk_str);
  nonce = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes, 
                               "hello netcmd, nonce"));
  assert_end_of_buffer(payload, pos, "hello netcmd payload");
}

void 
netcmd::write_hello_cmd(rsa_keypair_id const & server_keyname,
                        rsa_pub_key const & server_key,
                        id const & nonce)
{
  cmd_code = hello_cmd;
  payload.clear();
  I(nonce().size() == constants::merkle_hash_length_in_bytes);
  insert_variable_length_string(server_keyname(), payload);
  insert_variable_length_string(server_key(), payload);
  payload += nonce();
}


void 
netcmd::read_anonymous_cmd(protocol_role & role, 
                           std::string & pattern,
                           id & nonce2) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <pattern: vstr> <nonce2: 20 random bytes>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "anonymous netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  extract_variable_length_string(payload, pattern, pos,
                                 "anonymous netcmd, pattern");
  nonce2 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "anonymous netcmd, nonce2"));
  assert_end_of_buffer(payload, pos, "anonymous netcmd payload");
}

void 
netcmd::write_anonymous_cmd(protocol_role role, 
                            std::string const & pattern,
                            id const & nonce2)
{
  cmd_code = anonymous_cmd;
  I(nonce2().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(role);
  insert_variable_length_string(pattern, payload);
  payload += nonce2();
}

void 
netcmd::read_auth_cmd(protocol_role & role, 
                      string & pattern,
                      id & client, 
                      id & nonce1, 
                      id & nonce2,
                      string & signature) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <pattern: vstr>
  //            <client: 20 bytes sha1> <nonce1: 20 random bytes> <nonce2: 20 random bytes>
  //            <signature: vstr>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "auth netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  extract_variable_length_string(payload, pattern, pos, "auth netcmd, pattern");
  client = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "auth netcmd, client identifier"));
  nonce1 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "auth netcmd, nonce1"));
  nonce2 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "auth netcmd, nonce2"));
  extract_variable_length_string(payload, signature, pos,
                                 "auth netcmd, signature");
  assert_end_of_buffer(payload, pos, "auth netcmd payload");
}

void 
netcmd::write_auth_cmd(protocol_role role, 
                       string const & pattern, 
                       id const & client,
                       id const & nonce1, 
                       id const & nonce2, 
                       string const & signature)
{
  cmd_code = auth_cmd;
  I(client().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);
  I(nonce2().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(role);
  insert_variable_length_string(pattern, payload);
  payload += client();
  payload += nonce1();
  payload += nonce2();
  insert_variable_length_string(signature, payload);
}
                         

void 
netcmd::read_confirm_cmd(string & signature) const
{
  size_t pos = 0;

  // syntax is: <signature: vstr>
  extract_variable_length_string(payload, signature, pos,
                                 "confirm netcmd, signature");
  assert_end_of_buffer(payload, pos, "confirm netcmd payload");
}
  
void 
netcmd::write_confirm_cmd(string const & signature)
{
  cmd_code = confirm_cmd;
  payload.clear();
  insert_variable_length_string(signature, payload);
}
  
void 
netcmd::read_refine_cmd(merkle_node & node) const
{
  // syntax is: <node: a merkle tree node>
  read_node(payload, node);
}

void 
netcmd::write_refine_cmd(merkle_node const & node)
{
  cmd_code = refine_cmd;
  payload.clear();
  write_node(node, payload);
}

void 
netcmd::read_done_cmd(size_t & level, netcmd_item_type & type)  const
{
  size_t pos = 0;
  // syntax is: <level: uleb128> <type: 1 byte>
  level = extract_datum_uleb128<size_t>(payload, pos,
                                        "done netcmd, level number");
  type = read_netcmd_item_type(payload, pos, "done netcmd, item type");
  assert_end_of_buffer(payload, pos, "done netcmd payload");
}

void 
netcmd::write_done_cmd(size_t level, 
                       netcmd_item_type type)
{
  cmd_code = done_cmd;
  payload.clear();
  insert_datum_uleb128<size_t>(level, payload);
  payload += static_cast<char>(type);
}

void 
netcmd::read_send_data_cmd(netcmd_item_type & type, id & item) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> 
  type = read_netcmd_item_type(payload, pos, "send_data netcmd, item type");
  item = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes, 
                              "send_data netcmd, item identifier"));
  assert_end_of_buffer(payload, pos, "send_data netcmd payload");
}

void 
netcmd::write_send_data_cmd(netcmd_item_type type, id const & item)
{
  cmd_code = send_data_cmd;
  I(item().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(type);
  payload += item();
}

void 
netcmd::read_send_delta_cmd(netcmd_item_type & type,
                            id & base,
                            id & ident) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <src: 20 bytes sha1> <dst: 20 bytes sha1>
  type = read_netcmd_item_type(payload, pos, "send_delta netcmd, item type");
  base = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes, 
                              "send_delta netcmd, base item identifier"));
  ident = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes, 
                              "send_delta netcmd, ident item identifier"));
  assert_end_of_buffer(payload, pos, "send_delta netcmd payload");
}

void 
netcmd::write_send_delta_cmd(netcmd_item_type type,
                             id const & base,
                             id const & ident)
{
  cmd_code = send_delta_cmd;
  I(base().size() == constants::merkle_hash_length_in_bytes);
  I(ident().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(type);
  payload += base();
  payload += ident();
}

void 
netcmd::read_data_cmd(netcmd_item_type & type,
                      id & item, string & dat) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> 
  //            <compressed_p1: 1 byte> <dat: vstr>

  type = read_netcmd_item_type(payload, pos, "data netcmd, item type");
  item = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes, 
                              "data netcmd, item identifier"));

  dat.clear();
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "data netcmd, compression flag");
  extract_variable_length_string(payload, dat, pos,
                                  "data netcmd, data payload");
  if (compressed_p == 1)
    dat = xform<CryptoPP::Gunzip>(dat);
  assert_end_of_buffer(payload, pos, "data netcmd payload");
}

void 
netcmd::write_data_cmd(netcmd_item_type type,
                       id const & item,
                       string const & dat)
{
  cmd_code = data_cmd;
  I(item().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(type);
  payload += item();
  if (dat.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      string tmp;
      tmp = xform<CryptoPP::Gzip>(dat);
      payload += static_cast<char>(1); // compressed flag
      insert_variable_length_string(tmp, payload);
    }
  else
    {
      payload += static_cast<char>(0); // compressed flag       
      insert_variable_length_string(dat, payload);
    }
}


void 
netcmd::read_delta_cmd(netcmd_item_type & type,
                       id & base, id & ident, delta & del) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <src: 20 bytes sha1> <dst: 20 bytes sha1>
  //            <compressed_p: 1 byte> <del: vstr>    
  type = read_netcmd_item_type(payload, pos, "delta netcmd, item type");
  base = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes, 
                              "delta netcmd, base identifier"));
  ident = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes, 
                               "delta netcmd, ident identifier"));
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "delta netcmd, compression flag");
  string tmp;
  extract_variable_length_string(payload, tmp, pos,
                                 "delta netcmd, delta payload");
  if (compressed_p == 1)
    tmp = xform<CryptoPP::Gunzip>(tmp);
  del = delta(tmp);
  assert_end_of_buffer(payload, pos, "delta netcmd payload");
}

void 
netcmd::write_delta_cmd(netcmd_item_type & type,
                        id const & base, id const & ident, 
                        delta const & del)
{
  cmd_code = delta_cmd;
  I(base().size() == constants::merkle_hash_length_in_bytes);
  I(ident().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(type);
  payload += base();
  payload += ident();

  string tmp = del();

  if (tmp.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      payload += static_cast<char>(1); // compressed flag
      tmp = xform<CryptoPP::Gzip>(tmp);
    }
  else
    {
      payload += static_cast<char>(0); // compressed flag       
    }
  I(tmp.size() <= constants::netcmd_payload_limit);
  insert_variable_length_string(tmp, payload);
}


void 
netcmd::read_nonexistant_cmd(netcmd_item_type & type, id & item) const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> 
  type = read_netcmd_item_type(payload, pos, "nonexistant netcmd, item type");
  item = id(extract_substring(payload, pos,
                              constants::merkle_hash_length_in_bytes, 
                              "nonexistant netcmd, item identifier"));
  assert_end_of_buffer(payload, pos, "nonexistant netcmd payload");
}

void 
netcmd::write_nonexistant_cmd(netcmd_item_type type, id const & item)
{
  cmd_code = nonexistant_cmd;
  I(item().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(type);
  payload += item();
}


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
#include "transforms.hh"
#include <boost/lexical_cast.hpp>

void 
test_netcmd_functions()
{
  
  try 
    {

      // error_cmd
      {
        L(F("checking i/o round trip on error_cmd\n")); 
        netcmd out_cmd, in_cmd;
        string out_errmsg("your shoelaces are untied"), in_errmsg;
        string buf;
        out_cmd.write_error_cmd(out_errmsg);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_error_cmd(in_errmsg);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_errmsg == out_errmsg);
        L(F("errmsg_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // bye_cmd
      {
        L(F("checking i/o round trip on bye_cmd\n"));   
        netcmd out_cmd, in_cmd;
        string buf;
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        BOOST_CHECK(in_cmd == out_cmd);
        L(F("bye_cmd test done, buffer was %d bytes\n") % buf.size());
      }
      
      // hello_cmd
      {
        L(F("checking i/o round trip on hello_cmd\n"));
        netcmd out_cmd, in_cmd;
        string buf;
        rsa_keypair_id out_server_keyname("server@there"), in_server_keyname;
        rsa_pub_key out_server_key("9387938749238792874"), in_server_key;
        id out_nonce(raw_sha1("nonce it up")), in_nonce;
        out_cmd.write_hello_cmd(out_server_keyname, out_server_key, out_nonce);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_hello_cmd(in_server_keyname, in_server_key, in_nonce);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_server_keyname == out_server_keyname);
        BOOST_CHECK(in_server_key == out_server_key);
        BOOST_CHECK(in_nonce == out_nonce);
        L(F("hello_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // anonymous_cmd
      {
        L(F("checking i/o round trip on anonymous_cmd\n"));
        netcmd out_cmd, in_cmd;
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        id out_nonce2(raw_sha1("nonce start my heart")), in_nonce2;
        string out_pattern("radishes galore!"), in_pattern;

        out_cmd.write_anonymous_cmd(out_role, out_pattern, out_nonce2);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_anonymous_cmd(in_role, in_pattern, in_nonce2);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_nonce2 == out_nonce2);
        BOOST_CHECK(in_role == out_role);
        L(F("anonymous_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // auth_cmd
      {
        L(F("checking i/o round trip on auth_cmd\n"));
        netcmd out_cmd, in_cmd;
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        id out_client(raw_sha1("happy client day")), out_nonce1(raw_sha1("nonce me amadeus")), 
          out_nonce2(raw_sha1("nonce start my heart")), 
          in_client, in_nonce1, in_nonce2;
        string out_signature(raw_sha1("burble") + raw_sha1("gorby")), out_pattern("radishes galore!"), 
          in_signature, in_pattern;

        out_cmd.write_auth_cmd(out_role, out_pattern, out_client, out_nonce1, 
                               out_nonce2, out_signature);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_auth_cmd(in_role, in_pattern, in_client,
                             in_nonce1, in_nonce2, in_signature);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_client == out_client);
        BOOST_CHECK(in_nonce1 == out_nonce1);
        BOOST_CHECK(in_nonce2 == out_nonce2);
        BOOST_CHECK(in_signature == out_signature);
        BOOST_CHECK(in_role == out_role);
        L(F("auth_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // confirm_cmd
      {
        L(F("checking i/o round trip on confirm_cmd\n"));
        netcmd out_cmd, in_cmd;
        string buf;
        string out_signature(raw_sha1("egg") + raw_sha1("tomago")), in_signature;

        out_cmd.write_confirm_cmd(out_signature);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_confirm_cmd(in_signature);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_signature == out_signature);
        L(F("confirm_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // refine_cmd
      {
        L(F("checking i/o round trip on refine_cmd\n"));
        netcmd out_cmd, in_cmd;
        string buf;
        merkle_node out_node, in_node;

        out_node.set_raw_slot(0, id(raw_sha1("The police pulled Kris Kringle over")));
        out_node.set_raw_slot(3, id(raw_sha1("Kris Kringle tried to escape from the police")));
        out_node.set_raw_slot(8, id(raw_sha1("He was arrested for auto theft")));
        out_node.set_raw_slot(15, id(raw_sha1("He was whisked away to jail")));
        out_node.set_slot_state(0, subtree_state);
        out_node.set_slot_state(3, live_leaf_state);
        out_node.set_slot_state(8, dead_leaf_state);
        out_node.set_slot_state(15, subtree_state);

        out_cmd.write_refine_cmd(out_node);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_refine_cmd(in_node);
        BOOST_CHECK(in_cmd == out_cmd);
        BOOST_CHECK(in_node == out_node);
        L(F("refine_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // done_cmd
      {
        L(F("checking i/o round trip on done_cmd\n"));
        netcmd out_cmd, in_cmd;
        size_t out_level(12), in_level;
        netcmd_item_type out_type(key_item), in_type(manifest_item);
        string buf;

        out_cmd.write_done_cmd(out_level, out_type);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_done_cmd(in_level, in_type);
        BOOST_CHECK(in_level == out_level);
        BOOST_CHECK(in_type == out_type);
        L(F("done_cmd test done, buffer was %d bytes\n") % buf.size()); 
      }

      // send_data_cmd
      {
        L(F("checking i/o round trip on send_data_cmd\n"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_id(raw_sha1("avocado is the yummiest")), in_id;
        string buf;

        out_cmd.write_send_data_cmd(out_type, out_id);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_send_data_cmd(in_type, in_id);
        BOOST_CHECK(in_type == out_type);
        BOOST_CHECK(in_id == out_id);
        L(F("send_data_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // send_delta_cmd
      {
        L(F("checking i/o round trip on send_delta_cmd\n"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_head(raw_sha1("when you board an airplane")), in_head;
        id out_base(raw_sha1("always check the exit locations")), in_base;
        string buf;

        out_cmd.write_send_delta_cmd(out_type, out_head, out_base);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_send_delta_cmd(in_type, in_head, in_base);
        BOOST_CHECK(in_type == out_type);
        BOOST_CHECK(in_head == out_head);
        BOOST_CHECK(in_base == out_base);
        L(F("send_delta_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // data_cmd
      {
        L(F("checking i/o round trip on data_cmd\n"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_id(raw_sha1("tuna is not yummy")), in_id;
        string out_dat("thank you for flying northwest"), in_dat;
        string buf;
        out_cmd.write_data_cmd(out_type, out_id, out_dat);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_data_cmd(in_type, in_id, in_dat);
        BOOST_CHECK(in_id == out_id);
        BOOST_CHECK(in_dat == out_dat);
        L(F("data_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // delta_cmd
      {
        L(F("checking i/o round trip on delta_cmd\n"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_head(raw_sha1("your seat cusion can be reused")), in_head;
        id out_base(raw_sha1("as a floatation device")), in_base;
        delta out_delta("goodness, this is not an xdelta"), in_delta;
        string buf;

        out_cmd.write_delta_cmd(out_type, out_head, out_base, out_delta);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_delta_cmd(in_type, in_head, in_base, in_delta);
        BOOST_CHECK(in_type == out_type);
        BOOST_CHECK(in_head == out_head);
        BOOST_CHECK(in_base == out_base);
        BOOST_CHECK(in_delta == out_delta);
        L(F("delta_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // nonexistant_cmd
      {
        L(F("checking i/o round trip on nonexistant_cmd\n"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_id(raw_sha1("avocado is the yummiest")), in_id;
        string buf;

        out_cmd.write_nonexistant_cmd(out_type, out_id);
        out_cmd.write(buf);
        BOOST_CHECK(in_cmd.read(buf));
        in_cmd.read_nonexistant_cmd(in_type, in_id);
        BOOST_CHECK(in_type == out_type);
        BOOST_CHECK(in_id == out_id);
        L(F("nonexistant_cmd test done, buffer was %d bytes\n") % buf.size());
      }

    }
  catch (bad_decode & d)
    {
      L(F("bad decode exception: '%s'\n") % d.what);
      throw;
    }
}

void 
add_netcmd_tests(test_suite * suite)
{
  suite->add(BOOST_TEST_CASE(&test_netcmd_functions));
}

#endif // BUILD_UNIT_TESTS
