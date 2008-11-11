// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "vector.hh"
#include <utility>

#include "constants.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "hmac.hh"
#include "globish.hh"

using std::string;

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
                   cmd_code(error_cmd)
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

// note: usher_reply_cmd does not get included in the hmac.
void
netcmd::write(string & out, chained_hmac & hmac) const
{
  size_t oldlen = out.size();
  out += static_cast<char>(version);
  out += static_cast<char>(cmd_code);
  insert_variable_length_string(payload, out);

  if (hmac.is_active() && cmd_code != usher_reply_cmd)
    {
      string digest = hmac.process(out, oldlen);
      I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
      out.append(digest);
    }
}

// note: usher_cmd does not get included in the hmac.
bool
netcmd::read(string_queue & inbuf, chained_hmac & hmac)
{
  size_t pos = 0;

  if (inbuf.size() < constants::netcmd_minsz)
    return false;

  u8 extracted_ver = extract_datum_lsb<u8>(inbuf, pos, "netcmd protocol number");

  u8 cmd_byte = extract_datum_lsb<u8>(inbuf, pos, "netcmd code");
  switch (cmd_byte)
    {
    case static_cast<u8>(hello_cmd):
    case static_cast<u8>(bye_cmd):
    case static_cast<u8>(anonymous_cmd):
    case static_cast<u8>(auth_cmd):
    case static_cast<u8>(error_cmd):
    case static_cast<u8>(confirm_cmd):
    case static_cast<u8>(refine_cmd):
    case static_cast<u8>(done_cmd):
    case static_cast<u8>(data_cmd):
    case static_cast<u8>(delta_cmd):
    case static_cast<u8>(usher_cmd):
      cmd_code = static_cast<netcmd_code>(cmd_byte);
      break;
    default:
      // if the versions don't match, we will throw the more descriptive
      // error immediately after this switch.
      if (extracted_ver == version)
        throw bad_decode(F("unknown netcmd code 0x%x")
                          % widen<u32,u8>(cmd_byte));
    }
  // Ignore the version on usher_cmd packets.
  if (extracted_ver != version && cmd_code != usher_cmd)
    throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'\n"
                       "%s")
                     % widen<u32,u8>(version)
                     % widen<u32,u8>(extracted_ver)
                     % ((version < extracted_ver)
                        ? _("the remote side has a newer, incompatible version of monotone")
                        : _("the remote side has an older, incompatible version of monotone")));

  // check to see if we have even enough bytes for a complete uleb128
  size_t payload_len = 0;
  if (!try_extract_datum_uleb128<size_t>(inbuf, pos, "netcmd payload length",
      payload_len))
      return false;

  // they might have given us a bogus size
  if (payload_len > constants::netcmd_payload_limit)
    throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);

  // there might not be enough data yet in the input buffer
  unsigned int minsize;
  if (hmac.is_active() && cmd_code != usher_cmd)
    minsize = pos + payload_len + constants::netsync_hmac_value_length_in_bytes;
  else
    minsize = pos + payload_len;

  if (inbuf.size() < minsize)
    {
      return false;
    }

  string digest;
  string cmd_digest;

  if (hmac.is_active() && cmd_code != usher_cmd)
    {
      // grab it before the data gets munged
      I(hmac.hmac_length == constants::netsync_hmac_value_length_in_bytes);
      digest = hmac.process(inbuf, 0, pos + payload_len);
    }

  payload = extract_substring(inbuf, pos, payload_len, "netcmd payload");

  if (hmac.is_active() && cmd_code != usher_cmd)
    {
      // they might have given us bogus data
      cmd_digest = extract_substring(inbuf, pos,
				     constants::netsync_hmac_value_length_in_bytes,
				     "netcmd HMAC");
    }

  inbuf.pop_front(pos);

  if (hmac.is_active()
      && cmd_code != usher_cmd
      && cmd_digest != digest)
    {
      throw bad_decode(F("bad HMAC checksum (got %s, wanted %s)\n"
			 "this suggests data was corrupted in transit")
		       % encode_hexenc(cmd_digest)
		       % encode_hexenc(digest));
    }

  return true;
}

////////////////////////////////////////////
// payload reader/writer functions follow //
////////////////////////////////////////////

void
netcmd::read_error_cmd(string & errmsg) const
{
  size_t pos = 0;
  // syntax is: <errmsg:vstr>
  extract_variable_length_string(payload, errmsg, pos, "error netcmd, message");
  assert_end_of_buffer(payload, pos, "error netcmd payload");
}

void
netcmd::write_error_cmd(string const & errmsg)
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
  server_keyname = rsa_keypair_id(skn_str, made_from_network);
  extract_variable_length_string(payload, sk_str, pos,
                                 "hello netcmd, server key");
  server_key = rsa_pub_key(sk_str, made_from_network);
  nonce = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes,
                               "hello netcmd, nonce"), made_from_network);
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
netcmd::read_bye_cmd(u8 & phase) const
{
  size_t pos = 0;
  // syntax is: <phase: 1 byte>
  phase = extract_datum_lsb<u8>(payload, pos, "bye netcmd, phase number");
  assert_end_of_buffer(payload, pos, "bye netcmd payload");
}


void
netcmd::write_bye_cmd(u8 phase)
{
  cmd_code = bye_cmd;
  payload.clear();
  payload += phase;
}


void
netcmd::read_anonymous_cmd(protocol_role & role,
                           globish & include_pattern,
                           globish & exclude_pattern,
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
  string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, include_pattern");
  include_pattern = globish(pattern_string, made_from_network);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "anonymous(hmac) netcmd, exclude_pattern");
  exclude_pattern = globish(pattern_string, made_from_network);
  string hmac_key_string;
  extract_variable_length_string(payload, hmac_key_string, pos,
                                 "anonymous(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key_string, made_from_network);
  assert_end_of_buffer(payload, pos, "anonymous(hmac) netcmd payload");
}

void
netcmd::write_anonymous_cmd(protocol_role role,
                            globish const & include_pattern,
                            globish const & exclude_pattern,
                            rsa_oaep_sha_data const & hmac_key_encrypted)
{
  cmd_code = anonymous_cmd;
  payload += static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  insert_variable_length_string(hmac_key_encrypted(), payload);
}

void
netcmd::read_auth_cmd(protocol_role & role,
                      globish & include_pattern,
                      globish & exclude_pattern,
                      id & client,
                      id & nonce1,
                      rsa_oaep_sha_data & hmac_key_encrypted,
                      rsa_sha1_signature & signature) const
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
  string pattern_string;
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, include_pattern");
  include_pattern = globish(pattern_string, made_from_network);
  extract_variable_length_string(payload, pattern_string, pos,
                                 "auth(hmac) netcmd, exclude_pattern");
  exclude_pattern = globish(pattern_string, made_from_network);
  client = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes,
                                "auth(hmac) netcmd, client identifier"),
              made_from_network);
  nonce1 = id(extract_substring(payload, pos,
                                constants::merkle_hash_length_in_bytes,
                                "auth(hmac) netcmd, nonce1"),
              made_from_network);
  string hmac_key;
  extract_variable_length_string(payload, hmac_key, pos,
                                 "auth(hmac) netcmd, hmac_key_encrypted");
  hmac_key_encrypted = rsa_oaep_sha_data(hmac_key, made_from_network);
  string sig_string;
  extract_variable_length_string(payload, sig_string, pos,
                                 "auth(hmac) netcmd, signature");
  signature = rsa_sha1_signature(sig_string, made_from_network);
  assert_end_of_buffer(payload, pos, "auth(hmac) netcmd payload");
}

void
netcmd::write_auth_cmd(protocol_role role,
                       globish const & include_pattern,
                       globish const & exclude_pattern,
                       id const & client,
                       id const & nonce1,
                       rsa_oaep_sha_data const & hmac_key_encrypted,
                       rsa_sha1_signature const & signature)
{
  cmd_code = auth_cmd;
  I(client().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);
  payload += static_cast<char>(role);
  insert_variable_length_string(include_pattern(), payload);
  insert_variable_length_string(exclude_pattern(), payload);
  payload += client();
  payload += nonce1();
  insert_variable_length_string(hmac_key_encrypted(), payload);
  insert_variable_length_string(signature(), payload);
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
netcmd::read_refine_cmd(refinement_type & ty, merkle_node & node) const
{
  // syntax is: <u8: refinement type> <node: a merkle tree node>
  size_t pos = 0;
  ty = static_cast<refinement_type>
    (extract_datum_lsb<u8>
     (payload, pos,
      "refine netcmd, refinement type"));
  read_node(payload, pos, node);
  assert_end_of_buffer(payload, pos, "refine cmd");
}

void
netcmd::write_refine_cmd(refinement_type ty, merkle_node const & node)
{
  cmd_code = refine_cmd;
  payload.clear();
  payload += static_cast<char>(ty);
  write_node(node, payload);
}

void
netcmd::read_done_cmd(netcmd_item_type & type, size_t & n_items)  const
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <n_items: uleb128>
  type = read_netcmd_item_type(payload, pos, "done netcmd, item type");
  n_items = extract_datum_uleb128<size_t>(payload, pos,
                                          "done netcmd, item-to-send count");
  assert_end_of_buffer(payload, pos, "done netcmd payload");
}

void
netcmd::write_done_cmd(netcmd_item_type type,
                       size_t n_items)
{
  cmd_code = done_cmd;
  payload.clear();
  payload += static_cast<char>(type);
  insert_datum_uleb128<size_t>(n_items, payload);
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
                              "data netcmd, item identifier"),
            made_from_network);

  dat.clear();
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "data netcmd, compression flag");
  extract_variable_length_string(payload, dat, pos,
                                  "data netcmd, data payload");
  if (compressed_p == 1)
  {
    gzip<data> zdat(dat, made_from_network);
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
  payload += static_cast<char>(type);
  payload += item();
  if (dat.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      gzip<data> zdat;
      encode_gzip(data(dat), zdat);
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
                              "delta netcmd, base identifier"),
            made_from_network);
  ident = id(extract_substring(payload, pos,
                               constants::merkle_hash_length_in_bytes,
                               "delta netcmd, ident identifier"),
             made_from_network);
  u8 compressed_p = extract_datum_lsb<u8>(payload, pos,
                                          "delta netcmd, compression flag");
  string tmp;
  extract_variable_length_string(payload, tmp, pos,
                                 "delta netcmd, delta payload");
  if (compressed_p == 1)
    {
      gzip<delta> zdel(tmp, made_from_network);
      decode_gzip(zdel, del);
    }
  else
    {
      del = delta(tmp, made_from_network);
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
  payload += static_cast<char>(type);
  payload += base();
  payload += ident();

  string tmp;

  if (del().size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
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
netcmd::read_usher_cmd(utf8 & greeting) const
{
  size_t pos = 0;
  string str;
  extract_variable_length_string(payload, str, pos, "error netcmd, message");
  greeting = utf8(str, made_from_network);
  assert_end_of_buffer(payload, pos, "error netcmd payload");
}

void
netcmd::write_usher_reply_cmd(utf8 const & server, globish const & pattern)
{
  cmd_code = usher_reply_cmd;
  payload.clear();
  insert_variable_length_string(server(), payload);
  insert_variable_length_string(pattern(), payload);
}


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
#include "transforms.hh"
#include "lexical_cast.hh"

UNIT_TEST(netcmd, mac)
{
  netcmd out_cmd, in_cmd;
  string buf;
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key, true);
    // mutates mac
    out_cmd.write(buf, mac);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf[0] ^= 0xff;
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf[buf.size() - 1] ^= 0xff;
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf += '\0';
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }
}

static void
do_netcmd_roundtrip(netcmd const & out_cmd, netcmd & in_cmd, string & buf)
{
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK(in_cmd.read_string(buf, mac));
  }
  UNIT_TEST_CHECK(in_cmd == out_cmd);
}

UNIT_TEST(netcmd, functions)
{

  try
    {

      // error_cmd
      {
        L(FL("checking i/o round trip on error_cmd"));
        netcmd out_cmd, in_cmd;
        string out_errmsg("your shoelaces are untied"), in_errmsg;
        string buf;
        out_cmd.write_error_cmd(out_errmsg);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_error_cmd(in_errmsg);
        UNIT_TEST_CHECK(in_errmsg == out_errmsg);
        L(FL("errmsg_cmd test done, buffer was %d bytes") % buf.size());
      }

      // hello_cmd
      {
        L(FL("checking i/o round trip on hello_cmd"));
        netcmd out_cmd, in_cmd;
        string buf;
        rsa_keypair_id out_server_keyname("server@there"), in_server_keyname;
        rsa_pub_key out_server_key("9387938749238792874"), in_server_key;
        id out_nonce(raw_sha1("nonce it up")), in_nonce;
        out_cmd.write_hello_cmd(out_server_keyname, out_server_key, out_nonce);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_hello_cmd(in_server_keyname, in_server_key, in_nonce);
        UNIT_TEST_CHECK(in_server_keyname == out_server_keyname);
        UNIT_TEST_CHECK(in_server_key == out_server_key);
        UNIT_TEST_CHECK(in_nonce == out_nonce);
        L(FL("hello_cmd test done, buffer was %d bytes") % buf.size());
      }

      // bye_cmd
      {
        L(FL("checking i/o round trip on bye_cmd"));
        netcmd out_cmd, in_cmd;
        u8 out_phase(1), in_phase;
        string buf;

        out_cmd.write_bye_cmd(out_phase);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_bye_cmd(in_phase);
        UNIT_TEST_CHECK(in_phase == out_phase);
        L(FL("bye_cmd test done, buffer was %d bytes") % buf.size());
      }

      // anonymous_cmd
      {
        L(FL("checking i/o round trip on anonymous_cmd"));
        netcmd out_cmd, in_cmd;
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        globish out_include_pattern("radishes galore!"), in_include_pattern;
        globish out_exclude_pattern("turnips galore!"), in_exclude_pattern;

        out_cmd.write_anonymous_cmd(out_role, out_include_pattern, out_exclude_pattern, out_key);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_anonymous_cmd(in_role, in_include_pattern, in_exclude_pattern, in_key);
        UNIT_TEST_CHECK(in_key == out_key);
        UNIT_TEST_CHECK(in_include_pattern() == out_include_pattern());
        UNIT_TEST_CHECK(in_exclude_pattern() == out_exclude_pattern());
        UNIT_TEST_CHECK(in_role == out_role);
        L(FL("anonymous_cmd test done, buffer was %d bytes") % buf.size());
      }

      // auth_cmd
      {
        L(FL("checking i/o round trip on auth_cmd"));
        netcmd out_cmd, in_cmd;
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        id out_client(raw_sha1("happy client day")), out_nonce1(raw_sha1("nonce me amadeus")),
          in_client, in_nonce1;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        rsa_sha1_signature out_signature(raw_sha1("burble") + raw_sha1("gorby")), in_signature;
        globish out_include_pattern("radishes galore!"), in_include_pattern;
        globish out_exclude_pattern("turnips galore!"), in_exclude_pattern;

        out_cmd.write_auth_cmd(out_role, out_include_pattern, out_exclude_pattern
                               , out_client, out_nonce1, out_key, out_signature);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_auth_cmd(in_role, in_include_pattern, in_exclude_pattern,
                             in_client, in_nonce1, in_key, in_signature);
        UNIT_TEST_CHECK(in_client == out_client);
        UNIT_TEST_CHECK(in_nonce1 == out_nonce1);
        UNIT_TEST_CHECK(in_key == out_key);
        UNIT_TEST_CHECK(in_signature == out_signature);
        UNIT_TEST_CHECK(in_role == out_role);
        UNIT_TEST_CHECK(in_include_pattern() == out_include_pattern());
        UNIT_TEST_CHECK(in_exclude_pattern() == out_exclude_pattern());
        L(FL("auth_cmd test done, buffer was %d bytes") % buf.size());
      }

      // confirm_cmd
      {
        L(FL("checking i/o round trip on confirm_cmd"));
        netcmd out_cmd, in_cmd;
        string buf;
        out_cmd.write_confirm_cmd();
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_confirm_cmd();
        L(FL("confirm_cmd test done, buffer was %d bytes") % buf.size());
      }

      // refine_cmd
      {
        L(FL("checking i/o round trip on refine_cmd"));
        netcmd out_cmd, in_cmd;
        string buf;
        refinement_type out_ty (refinement_query), in_ty(refinement_response);
        merkle_node out_node, in_node;

        out_node.set_raw_slot(0, id(raw_sha1("The police pulled Kris Kringle over")));
        out_node.set_raw_slot(3, id(raw_sha1("Kris Kringle tried to escape from the police")));
        out_node.set_raw_slot(8, id(raw_sha1("He was arrested for auto theft")));
        out_node.set_raw_slot(15, id(raw_sha1("He was whisked away to jail")));
        out_node.set_slot_state(0, subtree_state);
        out_node.set_slot_state(3, leaf_state);
        out_node.set_slot_state(8, leaf_state);
        out_node.set_slot_state(15, subtree_state);

        out_cmd.write_refine_cmd(out_ty, out_node);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_refine_cmd(in_ty, in_node);
        UNIT_TEST_CHECK(in_ty == out_ty);
        UNIT_TEST_CHECK(in_node == out_node);
        L(FL("refine_cmd test done, buffer was %d bytes") % buf.size());
      }

      // done_cmd
      {
        L(FL("checking i/o round trip on done_cmd"));
        netcmd out_cmd, in_cmd;
        size_t out_n_items(12), in_n_items(0);
        netcmd_item_type out_type(key_item), in_type(revision_item);
        string buf;

        out_cmd.write_done_cmd(out_type, out_n_items);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_done_cmd(in_type, in_n_items);
        UNIT_TEST_CHECK(in_n_items == out_n_items);
        UNIT_TEST_CHECK(in_type == out_type);
        L(FL("done_cmd test done, buffer was %d bytes") % buf.size());
      }

      // data_cmd
      {
        L(FL("checking i/o round trip on data_cmd"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_id(raw_sha1("tuna is not yummy")), in_id;
        string out_dat("thank you for flying northwest"), in_dat;
        string buf;
        out_cmd.write_data_cmd(out_type, out_id, out_dat);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_data_cmd(in_type, in_id, in_dat);
        UNIT_TEST_CHECK(in_id == out_id);
        UNIT_TEST_CHECK(in_dat == out_dat);
        L(FL("data_cmd test done, buffer was %d bytes") % buf.size());
      }

      // delta_cmd
      {
        L(FL("checking i/o round trip on delta_cmd"));
        netcmd out_cmd, in_cmd;
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_head(raw_sha1("your seat cusion can be reused")), in_head;
        id out_base(raw_sha1("as a floatation device")), in_base;
        delta out_delta("goodness, this is not an xdelta"), in_delta;
        string buf;

        out_cmd.write_delta_cmd(out_type, out_head, out_base, out_delta);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_delta_cmd(in_type, in_head, in_base, in_delta);
        UNIT_TEST_CHECK(in_type == out_type);
        UNIT_TEST_CHECK(in_head == out_head);
        UNIT_TEST_CHECK(in_base == out_base);
        UNIT_TEST_CHECK(in_delta == out_delta);
        L(FL("delta_cmd test done, buffer was %d bytes") % buf.size());
      }

    }
  catch (bad_decode & d)
    {
      L(FL("bad decode exception: '%s'") % d.what);
      throw;
    }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
