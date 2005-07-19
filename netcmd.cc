// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <utility>

#include "adler32.hh"
#include "constants.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "hmac.hh"

using namespace std;

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
netcmd::write(string & out, chained_hmac & hmac) const
{
  size_t oldlen = out.size();
  out += static_cast<char>(version);
  out += static_cast<char>(cmd_code);
  insert_variable_length_string(payload, out);

  string digest = hmac.process(out, oldlen);
  I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
  out.append(digest);
}

bool 
netcmd::read(string & inbuf, chained_hmac & hmac)
{
  size_t pos = 0;

  if (inbuf.size() < constants::netcmd_minsz)
    return false;

  u8 extracted_ver = extract_datum_lsb<u8>(inbuf, pos, "netcmd protocol number");
  if (extracted_ver != version)
    throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
                     % widen<u32,u8>(version)
                     % widen<u32,u8>(extracted_ver));
  version = extracted_ver;

  u8 cmd_byte = extract_datum_lsb<u8>(inbuf, pos, "netcmd code");
  switch (cmd_byte)
    {
    case static_cast<u8>(hello_cmd):
    case static_cast<u8>(anonymous_cmd):
    case static_cast<u8>(auth_cmd):
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
      cmd_code = static_cast<netcmd_code>(cmd_byte);
      break;
    default:
      throw bad_decode(F("unknown netcmd code 0x%x") % widen<u32,u8>(cmd_byte));
    }

  // check to see if we have even enough bytes for a complete uleb128
  size_t payload_len = 0;
  if (!try_extract_datum_uleb128<size_t>(inbuf, pos, "netcmd payload length",
      payload_len))
      return false;
  
  // they might have given us a bogus size
  if (payload_len > constants::netcmd_payload_limit)
    throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);
  
  // there might not be enough data yet in the input buffer
  if (inbuf.size() < pos + payload_len + constants::netsync_hmac_value_length_in_bytes)
    {
      inbuf.reserve(pos + payload_len + constants::netsync_hmac_value_length_in_bytes + constants::bufsz);
      return false;
    }

//  out.payload = extract_substring(inbuf, pos, payload_len, "netcmd payload");
  // Do this ourselves, so we can swap the strings instead of copying.
  require_bytes(inbuf, pos, payload_len, "netcmd payload");

  // grab it before the data gets munged
  I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
  string digest = hmac.process(inbuf, 0, pos + payload_len);

  payload = inbuf.substr(pos + payload_len);
  inbuf.erase(pos + payload_len, inbuf.npos);
  inbuf.swap(payload);
  size_t payload_pos = pos;
  pos = 0;

  // they might have given us bogus data
  string cmd_digest = extract_substring(inbuf, pos, 
      constants::netsync_hmac_value_length_in_bytes,
                                        "netcmd HMAC");
  inbuf.erase(0, pos);
  if (cmd_digest != digest)
    throw bad_decode(F("bad HMAC checksum (got %s, wanted %s)\n"
                       "this suggests data was corrupted in transit\n")
                     % encode_hexenc(cmd_digest)
                     % encode_hexenc(digest));
  payload.erase(0, payload_pos);

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
                           utf8 & include_pattern,
                           utf8 & exclude_pattern,
                           rsa_oaep_sha_data & hmac_key_encrypted) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <include_pattern: vstr> <exclude_pattern: vstr> <hmac_key_encrypted: vstr>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "anonymous(hmac) netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  std::string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, include_pattern");
  include_pattern = utf8(pattern_string);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, exclude_pattern");
  exclude_pattern = utf8(pattern_string);
  string hmac_key_string;
  extract_variable_length_string(payload, hmac_key_string, pos,
                                 "anonymous(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key_string);
  assert_end_of_buffer(payload, pos, "anonymous(hmac) netcmd payload");
}

void
netcmd::write_anonymous_cmd(protocol_role role,
                            utf8 const & include_pattern,
                            utf8 const & exclude_pattern,
                            rsa_oaep_sha_data const & hmac_key_encrypted)
{
  cmd_code = anonymous_cmd;
  payload = static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  insert_variable_length_string(hmac_key_encrypted(), payload);
}

void 
netcmd::read_auth_cmd(protocol_role & role, 
                      utf8 & include_pattern,
                      utf8 & exclude_pattern,
                      id & client, 
                      id & nonce1, 
                      rsa_oaep_sha_data & hmac_key_encrypted,
                      string & signature) const
{
  size_t pos = 0;
  // syntax is: <role:1 byte> <include_pattern: vstr> <exclude_pattern: vstr>
  //            <client: 20 bytes sha1> <nonce1: 20 random bytes>
  //            <hmac_key_encrypted: vstr> <signature: vstr>
  u8 role_byte = extract_datum_lsb<u8>(payload, pos, "auth netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier %d") % widen<u32,u8>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  std::string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, include_pattern");
  include_pattern = utf8(pattern_string);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, exclude_pattern");
  exclude_pattern = utf8(pattern_string);
  client = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "auth(hmac) netcmd, client identifier"));
  nonce1 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes, 
                                "auth(hmac) netcmd, nonce1"));
  string hmac_key;
  extract_variable_length_string(payload, hmac_key, pos,
                                 "auth(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key);
  extract_variable_length_string(payload, signature, pos,
                                 "auth(hmac) netcmd, signature");
  assert_end_of_buffer(payload, pos, "auth(hmac) netcmd payload");
}

void
netcmd::write_auth_cmd(protocol_role role,
                       utf8 const & include_pattern,
                       utf8 const & exclude_pattern,
                       id const & client,
                       id const & nonce1,
                       rsa_oaep_sha_data const & hmac_key_encrypted,
                       string const & signature)
{
  cmd_code = auth_cmd;
  I(client().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);
  payload = static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  payload += client();
  payload += nonce1();
  insert_variable_length_string(hmac_key_encrypted(), payload);
  insert_variable_length_string(signature, payload);
}

void
netcmd::read_confirm_cmd() const
{
  size_t pos = 0;
  assert_end_of_buffer(payload, pos, "confirm netcmd payload");
}
  
void
netcmd::write_confirm_cmd()
{
  cmd_code = confirm_cmd;
  payload.clear();
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
  {
    gzip<data> zdat(dat);
    data tdat;
    decode_gzip(zdat, tdat);
    dat = tdat();
  }
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
      gzip<data> zdat;
      encode_gzip(dat, zdat);
      payload += static_cast<char>(1); // compressed flag
      insert_variable_length_string(zdat(), payload);
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
    {
      gzip<delta> zdel(tmp);
      decode_gzip(zdel, del);
    }
  else
    {
      del = tmp;
    }
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

  string tmp;

  if (tmp.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      payload += static_cast<char>(1); // compressed flag
      gzip<delta> zdel;
      encode_gzip(del, zdel);
      tmp = zdel();
    }
  else
    {
      payload += static_cast<char>(0); // compressed flag       
      tmp = del();
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
test_netcmd_mac()
{
  netcmd out_cmd, in_cmd;
  string buf;
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key);
    // mutates mac
    out_cmd.write(buf, mac);
    BOOST_CHECK_THROW(in_cmd.read(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key);
    out_cmd.write(buf, mac);
  }
  buf[0] ^= 0xff;
  {
    chained_hmac mac(key);
    BOOST_CHECK_THROW(in_cmd.read(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key);
    out_cmd.write(buf, mac);
  }
  buf[buf.size() - 1] ^= 0xff;
  {
    chained_hmac mac(key);
    BOOST_CHECK_THROW(in_cmd.read(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key);
    out_cmd.write(buf, mac);
  }
  buf += '\0';
  {
    chained_hmac mac(key);
    BOOST_CHECK_THROW(in_cmd.read(buf, mac), bad_decode);
  }
}

static void
do_netcmd_roundtrip(netcmd const & out_cmd, netcmd & in_cmd, string & buf)
{
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key);
    out_cmd.write(buf, mac);
  }
  {
    chained_hmac mac(key);
    BOOST_CHECK(in_cmd.read(buf, mac));
  }
  BOOST_CHECK(in_cmd == out_cmd);
}

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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_error_cmd(in_errmsg);
        BOOST_CHECK(in_errmsg == out_errmsg);
        L(F("errmsg_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // bye_cmd
      {
        L(F("checking i/o round trip on bye_cmd\n"));   
        netcmd out_cmd, in_cmd;
        string buf;
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_hello_cmd(in_server_keyname, in_server_key, in_nonce);
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
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        utf8 out_include_pattern("radishes galore!"), in_include_pattern;
        utf8 out_exclude_pattern("turnips galore!"), in_exclude_pattern;

        out_cmd.write_anonymous_cmd(out_role, out_include_pattern, out_exclude_pattern, out_key);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_anonymous_cmd(in_role, in_include_pattern, in_exclude_pattern, in_key);
        BOOST_CHECK(in_key == out_key);
        BOOST_CHECK(in_include_pattern == out_include_pattern);
        BOOST_CHECK(in_exclude_pattern == out_exclude_pattern);
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
          in_client, in_nonce1;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        string out_signature(raw_sha1("burble") + raw_sha1("gorby")), in_signature;
        utf8 out_include_pattern("radishes galore!"), in_include_pattern;
        utf8 out_exclude_pattern("turnips galore!"), in_exclude_pattern;

        out_cmd.write_auth_cmd(out_role, out_include_pattern, out_exclude_pattern
                               , out_client, out_nonce1, out_key, out_signature);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_auth_cmd(in_role, in_include_pattern, in_exclude_pattern,
                             in_client, in_nonce1, in_key, in_signature);
        BOOST_CHECK(in_client == out_client);
        BOOST_CHECK(in_nonce1 == out_nonce1);
        BOOST_CHECK(in_key == out_key);
        BOOST_CHECK(in_signature == out_signature);
        BOOST_CHECK(in_role == out_role);
        BOOST_CHECK(in_include_pattern == out_include_pattern);
        BOOST_CHECK(in_exclude_pattern == out_exclude_pattern);
        L(F("auth_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // confirm_cmd
      {
        L(F("checking i/o round trip on confirm_cmd\n"));
        netcmd out_cmd, in_cmd;
        string buf;
        out_cmd.write_confirm_cmd();
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_confirm_cmd();
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_refine_cmd(in_node);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
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
  suite->add(BOOST_TEST_CASE(&test_netcmd_mac));
}

#endif // BUILD_UNIT_TESTS
