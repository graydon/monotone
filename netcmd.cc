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

netcmd::netcmd() : version(constants::netcmd_current_protocol_version),
		   cmd_code(bye_cmd)
{}

size_t netcmd::encoded_size() 
{
  return constants::netcmd_minsz + payload.size();
}

bool netcmd::operator==(netcmd const & other) const 
{
  return version == other.version &&
    cmd_code == other.cmd_code &&
    payload == other.payload;
}
  
void write_netcmd(netcmd const & in, string & out)
{
  out += static_cast<char>(in.version);
  out += static_cast<char>(in.cmd_code);
  write_datum_msb<u32>(in.payload.size(), out);
  out += in.payload;
  adler32 check(in.payload.data(), in.payload.size());
  write_datum_msb<u32>(check.sum(), out);
}
  
bool read_netcmd(string & inbuf, netcmd & out)
{
  size_t pos = 0;

  if (inbuf.size() < constants::netcmd_minsz)
    return false;

  out.version = extract_datum_msb<u8>(inbuf, pos, "netcmd protocol number");
  int v = constants::netcmd_current_protocol_version;
  if (out.version != constants::netcmd_current_protocol_version)
    throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
		     % v % static_cast<int>(out.version));

  u8 cmd_byte = extract_datum_msb<u8>(inbuf, pos, "netcmd code");
  switch (static_cast<netcmd_code>(cmd_byte))
    {
    case bye_cmd:
    case hello_cmd:
    case auth_cmd:
    case confirm_cmd:
    case refine_cmd:
    case done_cmd:
    case describe_cmd:
    case description_cmd:
    case send_data_cmd:
    case send_delta_cmd:
    case data_cmd:
    case delta_cmd:
      out.cmd_code = static_cast<netcmd_code>(cmd_byte);
      break;
    default:
      throw bad_decode(F("unknown netcmd code 0x%x") % static_cast<int>(cmd_byte));
    }
    
  u32 payload_len = extract_datum_msb<u32>(inbuf, pos, "netcmd payload length");

  // they might have given us a bogus size
  if (payload_len > constants::netcmd_payload_limit)
    throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);

  // there might not be enough data yet in the input buffer
  if (inbuf.size() < constants::netcmd_minsz + payload_len)
    return false;
    
  out.payload = extract_substring(inbuf, pos, payload_len, "netcmd payload");

  // they might have given us bogus data
  u32 checksum = extract_datum_msb<u32>(inbuf, pos, "netcmd checksum");
  adler32 check(out.payload.data(), out.payload.size());
  if (checksum != check.sum())
    throw bad_decode(F("bad checksum %d vs. %d bytes") % checksum % check.sum());

  return true;    
}

void read_hello_cmd_payload(string const & in, id & server, id & nonce)
{
  size_t pos = 0;
  // syntax is <server:20 bytes sha1> <nonce:20 random bytes>
  server = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
				"hello netcmd, server identifier"));
  nonce = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			       "hello netcmd, nonce"));
  assert_end_of_buffer(in, pos, "hello netcmd payload");
}

void write_hello_cmd_payload(id const & server, id const & nonce, string & out)
{
  I(server().size() == constants::merkle_hash_length_in_bytes);
  I(nonce().size() == constants::merkle_hash_length_in_bytes);
  out += server();
  out += nonce();
}

void read_auth_cmd_payload(string const & in, 
			   protocol_role & role, 
			   string & collection,
			   id & client, 
			   id & nonce1, 
			   id & nonce2,
			   string & signature)
{
  size_t pos = 0;
  // syntax is <role:1 byte> <len1: 4 bytes> <collection: len1 bytes>
  //           <client: 20 bytes sha1> <nonce1: 20 random bytes> <nonce2: 20 random bytes>
  //           <len2: 4 bytes> <signature: len2 bytes>
  u8 role_byte = extract_datum_msb<u8>(in, pos, "auth netcmd, role");
  if (role_byte != static_cast<u8>(source_role)
      && role_byte != static_cast<u8>(sink_role)
      && role_byte != static_cast<u8>(source_and_sink_role))
    throw bad_decode(F("unknown role specifier 0x%x") % static_cast<int>(role_byte));
  role = static_cast<protocol_role>(role_byte);
  u32 coll_len = extract_datum_msb<u32>(in, pos, "auth netcmd, collection name length");
  collection = extract_substring(in, pos, coll_len, "auth netcmd, collection name");
  client = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
				"auth netcmd, client identifier"));
  nonce1 = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
				"auth netcmd, nonce1"));
  nonce2 = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
				"auth netcmd, nonce2"));
  u32 sig_len = extract_datum_msb<u32>(in, pos, "auth netcmd, signature length");
  signature = extract_substring(in, pos, sig_len, "auth netcmd, signature");
  assert_end_of_buffer(in, pos, "auth netcmd payload");
}

void write_auth_cmd_payload(protocol_role role, 
			    string const & collection, 
			    id const & client,
			    id const & nonce1, 
			    id const & nonce2, 
			    string const & signature, 
			    string & out)
{
  I(client().size() == constants::merkle_hash_length_in_bytes);
  I(nonce1().size() == constants::merkle_hash_length_in_bytes);
  I(nonce2().size() == constants::merkle_hash_length_in_bytes);
  out += static_cast<char>(role);
  write_datum_msb<u32>(collection.size(), out);
  out += collection;
  out += client();
  out += nonce1();
  out += nonce2();
  write_datum_msb<u32>(signature.size(), out);
  out += signature;
}
			 

void read_confirm_cmd_payload(string const & in, string & signature)
{
  size_t pos = 0;
  // syntax is <len: 4 bytes> <signature: len bytes>
  u32 sig_len = extract_datum_msb<u32>(in, pos, "confirm netcmd, signature length");
  signature = extract_substring(in, pos, sig_len, "confirm netcmd, signature");
  assert_end_of_buffer(in, pos, "confirm netcmd payload");
}
  
void write_confirm_cmd_payload(string const & signature, string & out)
{
  write_datum_msb<u32>(signature.size(), out);
  out += signature;
}
  
void read_refine_cmd_payload(string const & in, merkle_node & node)
{
  // syntax is <node: a merkle tree node>
  read_node(in, node);
}

void write_refine_cmd_payload(merkle_node const & node, string & out)
{
  write_node(node, out);
}

void read_done_cmd_payload(string const & in, u8 & level, netcmd_item_type & type)
{
  size_t pos = 0;
  // syntax is: <level: 1 byte> <type: 1 byte>
  level = extract_datum_msb<u8>(in, pos, "done netcmd, level number");
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "done netcmd, item type"));
  assert_end_of_buffer(in, pos, "done netcmd payload");
}

void write_done_cmd_payload(u8 level, netcmd_item_type type, string & out)
{
  out += static_cast<char>(level);
  out += static_cast<char>(type);
}

void read_describe_cmd_payload(string const & in, netcmd_item_type & type, id & item)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1>
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "describe netcmd, item type"));
  item = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "describe netcmd, item identifier"));
  assert_end_of_buffer(in, pos, "describe netcmd payload");
}

void write_describe_cmd_payload(netcmd_item_type type, id const & item, string & out)
{
  I(item().size() == constants::merkle_hash_length_in_bytes);
  out += static_cast<char>(type);
  out += item();
}

void read_description_cmd_payload(string const & in, 
				  netcmd_item_type & type,
				  id & item, 
				  u64 & len,
				  vector<id> & predecessors)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> <len: 8 bytes> 
  //            <npred: 1 byte> <pred1: 20 bytes sha1> ... <predN>
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "description netcmd, item type"));
  item = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "description netcmd, item identifier"));
  len = extract_datum_msb<u64>(in, pos, "description netcmd, data length");
  u8 npred = extract_datum_msb<u8>(in, pos, "description netcmd, number of predecessors");
  predecessors.clear();
  predecessors.reserve(npred);
  for (u8 i = 0; i < npred; ++i)
    {
      string tmp = extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
				     "description netcmd, predecessor identifier");
      predecessors.push_back(id(tmp));
    }
  assert_end_of_buffer(in, pos, "description netcmd payload");
}

void write_description_cmd_payload(netcmd_item_type type,
				   id const & item, 
				   u64 len,
				   vector<id> const & predecessors,
				   string & out)
{
  I(item().size() == constants::merkle_hash_length_in_bytes);
  I(predecessors.size() <= 0xff);
  out += static_cast<char>(type);
  out += item();
  write_datum_msb<u64>(len, out);
  out += static_cast<char>(predecessors.size());
  for (vector<id>::const_iterator i = predecessors.begin();
       i != predecessors.end(); ++i)
    {
      I((*i)().size() == constants::merkle_hash_length_in_bytes);
      out += (*i)();
    }
}

void read_send_data_cmd_payload(string const & in, 
				netcmd_item_type & type,
				id & item,
				vector<pair<u64, u64> > & fragments)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> <nfrag: 1 byte> 
  //            <pos1: 8 bytes> <len1: 8 bytes> ... <posN: 8 bytes> <lenN: 8 bytes>
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "send_data netcmd, item type"));
  item = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "send_data netcmd, item identifier"));
  u8 nfrag = extract_datum_msb<u8>(in, pos, "send_data netcmd, fragment count");
  fragments.reserve(nfrag);
  for (u8 i = 0; i < nfrag; ++i)
    {
      u64 fpos = extract_datum_msb<u64>(in, pos, "send_data netcmd, fragment position");
      u64 flen = extract_datum_msb<u64>(in, pos, "send_data netcmd, fragment length");
      fragments.push_back(make_pair(fpos, flen));
    }
  assert_end_of_buffer(in, pos, "send_data netcmd payload");
}

void write_send_data_cmd_payload(netcmd_item_type type,
				 id const & item,
				 vector<pair<u64, u64> > const & fragments,
				 string & out)
{
  I(item().size() == constants::merkle_hash_length_in_bytes);
  I(fragments.size() <= 0xff);
  out += static_cast<char>(type);
  out += item();
  out += static_cast<char>(fragments.size());
  for(vector<pair<u64, u64> >::const_iterator i = fragments.begin();
      i != fragments.end(); ++i)
    {
      write_datum_msb<u64>(i->first, out);
      write_datum_msb<u64>(i->second, out);
    }    
}

void read_send_delta_cmd_payload(string const & in, 
				 netcmd_item_type & type,
				 id & head,
				 id & base)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <src: 20 bytes sha1> <dst: 20 bytes sha1>
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "send_delta netcmd, item type"));
  head = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "send_delta netcmd, head item identifier"));
  base = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "send_delta netcmd, base item identifier"));
  assert_end_of_buffer(in, pos, "send_delta netcmd payload");
}

void write_send_delta_cmd_payload(netcmd_item_type type,
				  id const & head,
				  id const & base,
				  string & out)
{
  I(head().size() == constants::merkle_hash_length_in_bytes);
  I(base().size() == constants::merkle_hash_length_in_bytes);
  out += static_cast<char>(type);
  out += head();
  out += base();
}

void read_data_cmd_payload(string const & in,
			   netcmd_item_type & type,
			   id & item,
			   vector< pair<pair<u64,u64>,string> > & fragments)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <id: 20 bytes sha1> <nfrag: 1 byte> 
  //            <pos1: 8 bytes> <len1: 8 bytes> 
  //            <compressed_p1: 1 byte> <clen1? 4 bytes> <dat1: len1 or clen1 bytes>
  //            ...
  //            <posN: 8 bytes> <lenN: 8 bytes> 
  //            <compressed_pN: 1 byte> <clenN? 4 bytes if compressed> <datN: lenN or clenN bytes>

  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "data netcmd, item type"));    
  item = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			      "data netcmd, item identifier"));
  u8 nfrag = extract_datum_msb<u8>(in, pos, "data netcmd, fragment count");
    
  fragments.reserve(nfrag);
  for (u8 i = 0; i < nfrag; ++i)
    {
      u64 fpos = extract_datum_msb<u64>(in, pos, "data netcmd, fragment position");
      u64 flen = extract_datum_msb<u64>(in, pos, "data netcmd, fragment length");
      u8 compressed_p = extract_datum_msb<u8>(in, pos, "data netcmd, compression flag");
      string txt;
      if (compressed_p == 1)
	{
	  u32 clen = extract_datum_msb<u32>(in, pos, "data netcmd, compressed fragment length");
	  txt = extract_substring(in, pos, clen, "data netcmd, compressed fragment");
	  txt = xform<CryptoPP::Gunzip>(txt);
	}
      else
	{
	  txt = extract_substring(in, pos, flen, "data netcmd, non-compressed fragment");
	}
      if (txt.size() != flen)
	throw bad_decode(F("data fragment size mismatch, %d vs. %d") % txt.size() % flen);
      fragments.push_back(make_pair(make_pair(fpos, flen), txt));
    }
  assert_end_of_buffer(in, pos, "data netcmd payload");
}

void write_data_cmd_payload(netcmd_item_type type,
			    id const & item,
			    vector< pair<pair<u64,u64>,string> > const & fragments,
			    string & out)
{
  I(item().size() == constants::merkle_hash_length_in_bytes);
  I(fragments.size() <= 0xff);
  out += static_cast<char>(type);
  out += item();
  out += static_cast<char>(fragments.size());
  for (vector< pair<pair<u64,u64>,string> >::const_iterator i = fragments.begin();
       i != fragments.end(); ++i)
    {
      I(i->first.second == i->second.size());
      write_datum_msb<u64>(i->first.first, out);
      write_datum_msb<u64>(i->first.second, out);
      string tmp = i->second;

      if (tmp.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
	{
	  tmp = xform<CryptoPP::Gzip>(tmp);
	  out += static_cast<char>(1); // compressed flag
	  write_datum_msb<u32>(tmp.size(), out);
	}
      else
	{
	  out += static_cast<char>(0); // compressed flag	    
	}

      I(tmp.size() <= i->first.second);
      I(tmp.size() <= constants::netcmd_payload_limit);
      out += tmp;
    }
}


void read_delta_cmd_payload(string const & in, 
			    netcmd_item_type & type,
			    id & src, id & dst, 
			    u64 & src_len, delta & del)
{
  size_t pos = 0;
  // syntax is: <type: 1 byte> <src: 20 bytes sha1> <dst: 20 bytes sha1> <src_len: 8 bytes> 
  //            <compressed_p: 1 byte> <clen: 4 bytes> <dat: clen bytes>    
  type = static_cast<netcmd_item_type>(extract_datum_msb<u8>(in, pos, 
							     "delta netcmd, item type"));    
  src = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			     "delta netcmd, source identifier"));
  dst = id(extract_substring(in, pos, constants::merkle_hash_length_in_bytes, 
			     "delta netcmd, destination identifier"));
  src_len = extract_datum_msb<u64>(in, pos, "delta netcmd, source length");
  u8 compressed_p = extract_datum_msb<u8>(in, pos, "delta netcmd, compression flag");
  u32 clen = extract_datum_msb<u32>(in, pos, "delta netcmd, compressed delta length");
  string tmp_del = extract_substring(in, pos, clen, "delta netcmd, delta content");
  if (compressed_p == 0)
    del = delta(tmp_del);
  else
    del = delta(xform<CryptoPP::Gunzip>(tmp_del));
  assert_end_of_buffer(in, pos, "delta netcmd payload");
}

void write_delta_cmd_payload(netcmd_item_type & type,
			     id const & src, id const & dst, 
			     u64 src_len, delta const & del,
			     string & out)
{
  I(src().size() == constants::merkle_hash_length_in_bytes);
  I(dst().size() == constants::merkle_hash_length_in_bytes);
  out += static_cast<char>(type);
  out += src();
  out += dst();
  write_datum_msb<u64>(src_len, out);

  string tmp = del();

  if (tmp.size() > constants::netcmd_minimum_bytes_to_bother_with_gzip)
    {
      tmp = xform<CryptoPP::Gzip>(tmp);
      out += static_cast<char>(1); // compressed flag
    }
  else
    {
      out += static_cast<char>(0); // compressed flag	    
    }
  I(tmp.size() <= constants::netcmd_payload_limit);
  write_datum_msb<u32>(tmp.size(), out);
  out += tmp;
}


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
#include "transforms.hh"
#include <boost/lexical_cast.hpp>

void test_netcmd_functions()
{
  
  try 
    {

      // bye_cmd
      {
	L(F("checking i/o round trip on bye_cmd\n"));	
	netcmd out_cmd, in_cmd;
	string buf;
	out_cmd.cmd_code = bye_cmd;
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	BOOST_CHECK(in_cmd == out_cmd);
	L(F("bye_cmd test done, buffer was %d bytes\n") % buf.size());
      }
      
      // hello_cmd
      {
	L(F("checking i/o round trip on hello_cmd\n"));
	netcmd out_cmd, in_cmd;
	string buf;
	id out_server(raw_sha1("happy server day")), out_nonce(raw_sha1("nonce it up")), in_server, in_nonce;
	out_cmd.cmd_code = hello_cmd;
	write_hello_cmd_payload(out_server, out_nonce, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_hello_cmd_payload(in_cmd.payload, in_server, in_nonce);
	BOOST_CHECK(in_cmd == out_cmd);
	BOOST_CHECK(in_server == out_server);
	BOOST_CHECK(in_nonce == out_nonce);
	L(F("hello_cmd test done, buffer was %d bytes\n") % buf.size());
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
	string out_signature(raw_sha1("burble") + raw_sha1("gorby")), out_collection("radishes galore!"), 
	  in_signature, in_collection;

	out_cmd.cmd_code = auth_cmd;
	write_auth_cmd_payload(out_role, out_collection, out_client, out_nonce1, 
				      out_nonce2, out_signature, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_auth_cmd_payload(in_cmd.payload, in_role, in_collection, in_client,
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

	out_cmd.cmd_code = confirm_cmd;
	write_confirm_cmd_payload(out_signature, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_confirm_cmd_payload(in_cmd.payload, in_signature);
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

	out_cmd.cmd_code = refine_cmd;
	write_refine_cmd_payload(out_node, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_refine_cmd_payload(in_cmd.payload, in_node);
	BOOST_CHECK(in_cmd == out_cmd);
	BOOST_CHECK(in_node == out_node);
	L(F("refine_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // done_cmd
      {
	L(F("checking i/o round trip on done_cmd\n"));
	netcmd out_cmd, in_cmd;
	u8 out_level(12), in_level;
	netcmd_item_type out_type(key_item), in_type(manifest_item);
	string buf;

	out_cmd.cmd_code = done_cmd;
	write_done_cmd_payload(out_level, out_type, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_done_cmd_payload(in_cmd.payload, in_level, in_type);
	BOOST_CHECK(in_level == out_level);
	BOOST_CHECK(in_type == out_type);
	L(F("done_cmd test done, buffer was %d bytes\n") % buf.size());	
      }

      // describe_cmd
      {
	L(F("checking i/o round trip on describe_cmd\n"));
	netcmd out_cmd, in_cmd;
	id out_id(raw_sha1("pickles are yummy")), in_id;
	netcmd_item_type out_type(key_item), in_type(manifest_item);
	string buf;

	out_cmd.cmd_code = describe_cmd;
	write_describe_cmd_payload(out_type, out_id, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_describe_cmd_payload(in_cmd.payload, in_type, in_id);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_type == out_type);
	L(F("describe_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // description_cmd
      {
	L(F("checking i/o round trip on description_cmd\n"));
	netcmd out_cmd, in_cmd;
	id out_id(raw_sha1("tuna is not yummy")), in_id;
	netcmd_item_type out_type(file_item), in_type(key_item);
	u64 out_len(8273423), in_len;
	vector<id> out_preds, in_preds;
	string buf;

	out_preds.push_back(id(raw_sha1("question")));
	out_preds.push_back(id(raw_sha1("what is ankh?")));
	out_preds.push_back(id(raw_sha1("*ding*")));
	out_preds.push_back(id(raw_sha1("that is the name")));
	out_preds.push_back(id(raw_sha1("of the item")));

	out_cmd.cmd_code = description_cmd;
	write_description_cmd_payload(out_type, out_id, out_len, out_preds, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_description_cmd_payload(in_cmd.payload, in_type, in_id, in_len, in_preds);
	BOOST_CHECK(in_type == out_type);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_len == out_len);
	BOOST_CHECK(in_preds == out_preds);
	L(F("description_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // send_data_cmd
      {
	L(F("checking i/o round trip on send_data_cmd\n"));
	netcmd out_cmd, in_cmd;
	netcmd_item_type out_type(file_item), in_type(key_item);
	id out_id(raw_sha1("avocado is the yummiest")), in_id;
	vector< pair<u64,u64> > out_frags, in_frags;
	string buf;

	out_frags.push_back(make_pair(123,456));
	out_frags.push_back(make_pair(0xffff,0xfeedf00d));
	out_frags.push_back(make_pair(1,1));
	out_frags.push_back(make_pair(0,0xffffffffffffffffULL));

	out_cmd.cmd_code = send_data_cmd;
	write_send_data_cmd_payload(out_type, out_id, out_frags, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_send_data_cmd_payload(in_cmd.payload, in_type, in_id, in_frags);
	BOOST_CHECK(in_type == out_type);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_frags == out_frags);
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

	out_cmd.cmd_code = send_delta_cmd;
	write_send_delta_cmd_payload(out_type, out_head, out_base, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_send_delta_cmd_payload(in_cmd.payload, in_type, in_head, in_base);
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
	vector< pair<pair<u64,u64>,string> > out_frags, in_frags;
	string buf;

	out_frags.push_back(make_pair(make_pair(444,20), "thank you for flying"));
	out_frags.push_back(make_pair(make_pair(123, 9), "northwest"));
	out_frags.push_back(make_pair(make_pair(193,19), "a friendly reminder"));
	out_frags.push_back(make_pair(make_pair(983,19), "please do not smoke"));
	out_frags.push_back(make_pair(make_pair(222,17), "in the lavatories"));
	out_frags.push_back(make_pair(make_pair(342,22), "also they are equipped"));
	out_frags.push_back(make_pair(make_pair(999,20), "with smoke detectors"));

	out_cmd.cmd_code = data_cmd;
	write_data_cmd_payload(out_type, out_id, out_frags, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_data_cmd_payload(in_cmd.payload, in_type, in_id, in_frags);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_frags == out_frags);
	L(F("data_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // delta_cmd
      {
	L(F("checking i/o round trip on delta_cmd\n"));
	netcmd out_cmd, in_cmd;
	netcmd_item_type out_type(file_item), in_type(key_item);
	id out_head(raw_sha1("your seat cusion can be reused")), in_head;
	id out_base(raw_sha1("as a floatation device")), in_base;
	u64 out_src_len(0xffff), in_src_len;
	delta out_delta("goodness, this is not an xdelta"), in_delta;
	string buf;

	out_cmd.cmd_code = delta_cmd;
	write_delta_cmd_payload(out_type, out_head, out_base, out_src_len, out_delta, out_cmd.payload);
	write_netcmd(out_cmd, buf);
	read_netcmd(buf, in_cmd);
	read_delta_cmd_payload(in_cmd.payload, in_type, in_head, in_base, in_src_len, in_delta);
	BOOST_CHECK(in_type == out_type);
	BOOST_CHECK(in_head == out_head);
	BOOST_CHECK(in_base == out_base);
	BOOST_CHECK(in_src_len == out_src_len);
	BOOST_CHECK(in_delta == out_delta);
	L(F("delta_cmd test done, buffer was %d bytes\n") % buf.size());
      }

    }
  catch (bad_decode & d)
    {
      L(F("bad decode exception: '%s'\n") % d.what);
      throw;
    }
}

void add_netcmd_tests(test_suite * suite)
{
  suite->add(BOOST_TEST_CASE(&test_netcmd_functions));
}

#endif // BUILD_UNIT_TESTS
