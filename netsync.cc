// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <deque>
#include <string>
#include <iostream>

#include <time.h>

#include <boost/cstdint.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>

#include "adler32.hh"
#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "keys.hh"
#include "netsync.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

#include "cryptopp/filters.h"
#include "cryptopp/gzip.h"
#include "cryptopp/hex.h"
#include "cryptopp/osrng.h"
#include "cryptopp/sha.h"

#include "Netxx/Address.h"
#include "Netxx/Peer.h"
#include "Netxx/Probe.h"
#include "Netxx/Stream.h"
#include "Netxx/StreamServer.h"
#include "Netxx/Timeout.h"
#include "Netxx/Types.h"

//
// this is the "new" network synchronization (netsync) system in
// monotone. it is based on synchronizing a pair of merkle trees over an
// interactive connection.
//
// a netsync process between peers treats each peer as either a source, a
// sink, or both. when a peer is only a source, it will not write any new
// items to its database. when a peer is only a sink, it will not send any
// items from its database. when a peer is both a source and sink, it may
// send and write items freely.
//
// the post-state of a netsync is that each sink contains a superset of the
// items in its corresponding source; when peers are behaving as both
// source and sink, this means that the post-state of the sync is for the
// peers to have identical item sets.
//
// a peer can be a sink in at most one netsync process at a time; it can
// however be a source for multiple netsyncs simultaneously.
//
//
// data structure
// --------------
//
// each node in a merkle tree contains a fixed number of slots. this number
// is derived from a global parameter of the protocol -- the tree fanout --
// such that the number of slots is 2^fanout. for now we will assume that
// fanout is 4 thus there are 16 slots in a node, because this makes
// illustration easier. the other parameter of the protocol is the size of
// a hash; we use SHA1 so the hash is 20 bytes (160 bits) long.
//
// each slot in a merkle tree node is in one of 4 states:
//
//   - empty
//   - live leaf
//   - dead leaf
//   - subtree
//   
// in addition, each live or dead leaf contains a hash code which
// identifies an element of the set being synchronized. each subtree slot
// contains a hash code of the node immediately beneath it in the merkle
// tree. empty slots contain no hash codes.
//
// each node also summarizes, for sake of statistic-gathering, the number
// of set elements and total number of bytes in all of its subtrees, each
// stored as a 64-bit unsigned integer in network (MSB) byte order.
//
// since empty slots have no hash code, they are represented implicitly by
// a bitmap at the head of each merkle tree node. as an additional
// integrity check, each merkle tree node contains a label indicating its
// prefix in the tree, and a hash of its own contents.
//
// in total, then, the byte-level representation of a <160,4> merkle tree
// node is as follows:
//
//      20 bytes       - hash of the remaining bytes in the node
//       1 byte        - level of this node in the tree (0 == "root")
//    0-20 bytes       - the prefix of this node, 4 bits * level, 
//                       rounded up to a byte
//       8 bytes       - number of leaves under this node
//       4 bytes       - slot-state bitmap of the node
//   0-320 bytes       - between 0 and 16 live slots in the node
//
// so, in the worst case such a node is 373 bytes, with these parameters.
//
//
// protocol
// --------
//
// the protocol is a simple binary command-packet system over tcp; each
// packet consists of a byte which identifies the protocol version, a byte
// which identifies the command name inside that version, 4 bytes in
// network (MSB) byte order indicating the length of the packet, and then
// that many bytes of payload, and finally 4 bytes of adler32 checksum (in
// MSB order) over the payload. decoding involves simply buffering until a
// sufficient number of bytes are received, then advancing the buffer
// pointer. any time an adler32 check fails, the protocol is assumed to
// have lost synchronization, and the connection is dropped. the parties
// are free to drop the tcp stream at any point, if too much data is
// received or too much idle time passes; no commitments or transactions
// are made.
//
// the protocol has 3 phases: authentication, refinement, and transmission.
// in all 3 phases, receipt by either peer of the command "bye" causes
// immediate disconnection.
//
// in the authentication phase, the server sends a "hello <id> <nonce>"
// command, which identifies the peer's RSA key and issues a nonce which
// must be used for a subsequent authentication.
//
// the client can then respond with an "auth (source|sink|both)
// <collection> <id> <nonce1> <nonce2> <sig>" command which identifies its
// RSA key, notes the role it wishes to play in the synchronization,
// identifies the collection it wishes to sync with, signs the previous
// nonce with its own key, and issues a nonce of its own for mutual
// authentication.
//
// the server can then respond with a "confirm <sig>" command, which is
// the signature of the second nonce sent by the client. this
// transitions the peers into refinement phase, following the roles
// negotiated during authentication phase.
//
// refinement begins with the client sending its root node to the
// server. the server then compares the root to each slot in *its* root
// node, and for each slot either sends refined subtrees to the client, or
// (if it detects a missing item in one collection or the other) sends
// either "description" or "request" commands corresponding to the role of
// the missing item (source or sink). the client then receives each refined
// subtree and compares it with its own, performing similar
// description/request behavior depending on role, and the cycle continues.
//
// detecting the end of refinement is subtle: after sending the refinement
// of the root node, the server sends a "done 0" command (queued behind all
// the other refinement traffic). when either peer receives a "done N"
// command it immediately responds with a "done N+1" command. when the
// server receives two "done" commands in succession, it completes
// refinement and moves to transmission.
//
// in transmission phase, all items queued on the peers acting as sources
// are sent to peers acting as sinks; if synchronization is bidirectional,
// the transmission happens in parallel. both sides know how many items are
// due to be sent, and return to authentication phase as soon as the last
// item is sent. the transmission of items is itself a bit subtle, however:
// either the item is sent in full (in the case of keys, certs, etc.)  or
// it is sent as a delta (often in the case of files or manifests). 
//
// delta transmission works by including, with each promise, a list of N
// reference versions in the storage system against which deltas can be
// sent. if no predecessor in this list is acceptable to the recipient, the
// recipient requests a full transmission. if a predecessor is found, the
// recipient requests the subset of deltas which lead from the target
// version to their reference version, in the storage system. recipient
// then works out which bytes are un-represented in target version by
// inverting the "copy" instructions, and mapping byte ranges, then
// requests the missing byte ranges in the target to fill in the gaps.
//
// (aside: this protocol is raw binary because coding density is actually
// important here, and each packet consists of very information-dense
// material that you wouldn't have a hope of typing in manually anyways)
//

using namespace CryptoPP;
using namespace Netxx;
using namespace boost;
using namespace std;

// this is a *raw* SHA1, not the nice friendly hex-encoded type. it is half
// as many bytes. we are currently deciding to use a raw binary protocol
// for this reason.

string raw_sha1(string const & in)
{
  SHA hash;
  hash.Update(reinterpret_cast<byte const *>(in.data()), 
	      static_cast<unsigned int>(in.size()));
  char digest[SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  string out(digest, SHA::DIGESTSIZE);
  return out;
}


typedef boost::uint8_t u8;
typedef boost::uint32_t u32;
typedef boost::uint64_t u64;

typedef enum
  {
    empty_state,
    live_leaf_state,
    dead_leaf_state,
    subtree_state
  }
slot_state;

typedef enum 
  { 
    authentication_phase,
    refinement_phase,
    transmission_phase 
  } 
protocol_phase;

typedef enum 
  { 
    // bye is valid in all phases
    bye_cmd = 1,

    // authentication-phase commands
    hello_cmd = 2,
    auth_cmd = 3,
    confirm_cmd = 4,
      
    // refinement-phase commands
    refine_cmd = 5,
    done_cmd = 6,
    describe_cmd = 7,
    description_cmd = 8,
      
    // transmission-phase commands
    send_data_cmd = 9,
    send_delta_cmd = 10,
    data_cmd = 11,
    delta_cmd = 12,
  }
command_code;


template <typename T>
static inline T read_datum_msb(char const * in)
{
  //   L(F("reading datum of %d bytes...\n") % sizeof(T));
  size_t const nbytes = sizeof(T);
  T out = 0;
  for (size_t i = 0; i < nbytes; ++i)
    {
      //       L(F("tmp[%d] = 0x%x\n") % (i) % (0xff & static_cast<int>(in[i])));
      out <<= 8;
      out |= (0xff & static_cast<T>(in[i]));
    }
  //   L(F("OK\n"));
  return out;
}

template <typename T>
static inline void write_datum_msb(T in, string & out)
{
  //   L(F("writing datum of %d bytes...\n") % sizeof(T));
  size_t const nbytes = sizeof(T);
  char tmp[nbytes];
  for (size_t i = nbytes; i > 0; --i)
    {
      //       L(F("tmp[%d] = 0x%x\n") % (i-1) % static_cast<int>(in & 0xff));
      tmp[i-1] = static_cast<char>(in & 0xff);
      in >>= 8;
    }
  out.append(string(tmp, tmp+nbytes));
  //   L(F("OK\n"));
}

struct bad_decode {
  bad_decode(boost::format const & fmt) : what(fmt.str()) {}
  std::string what;
};

static inline void 
require_bytes(string const & str, 
	      size_t pos, 
	      size_t len, 
	      string const & name)
{
  L(F("checking availability of %d bytes at %d for '%s'\n") % len % pos % name);
  // if you've gone past the end of the buffer, there's a logic error,
  // and this program is not safe to keep running. shut down.
  I(pos < str.size());
  // otherwise make sure there's room for this decode operation, but
  // use a recoverable exception type.
  if (str.size() < pos + len)
    throw bad_decode(F("need %d bytes to decode %s at %d, only have %d") 
		     % len % name % pos % (str.size() - pos));
}

static inline string extract_substring(string const & str, 
				       size_t & pos,
				       size_t len, 
				       string const & name)
{
  require_bytes(str, pos, len, name);
  string tmp = str.substr(pos, len);
  pos += len;
  return tmp;
}

template <typename T>
static inline T extract_datum_msb(string const & str, 
				  size_t & pos, 
				  string const & name)
{
  require_bytes(str, pos, sizeof(T), name);
  T tmp = read_datum_msb<T>(str.data() + pos);
  pos += sizeof(T);
  return tmp;  
}

static inline void assert_end_of_buffer(string const & str, 
					size_t pos, 
					string const & name)
{
  if (str.size() != pos)
    throw bad_decode(F("expected %s to end at %d, have %d bytes") 
		     % name % pos % str.size());
}

struct netsync_protocol
{

  BOOST_STATIC_CONSTANT(size_t, hash_length_in_bytes = SHA::DIGESTSIZE);
  BOOST_STATIC_CONSTANT(size_t, fanout_bits = 4);
  BOOST_STATIC_CONSTANT(size_t, minimum_bytes_to_bother_with_gzip = 64);

  // all other constants are derived
  BOOST_STATIC_CONSTANT(size_t, hash_length_in_bits = hash_length_in_bytes * 8);
  BOOST_STATIC_CONSTANT(size_t, num_tree_levels = hash_length_in_bits / fanout_bits);
  BOOST_STATIC_CONSTANT(size_t, num_slots = 1 << fanout_bits);
  BOOST_STATIC_CONSTANT(size_t, bitmap_length_in_bits = num_slots * 2);
  BOOST_STATIC_CONSTANT(size_t, bitmap_length_in_bytes = bitmap_length_in_bits / 8);

  BOOST_STATIC_ASSERT(sizeof(char) == 1);
  BOOST_STATIC_ASSERT(CHAR_BIT == 8);
  BOOST_STATIC_ASSERT(num_tree_levels > 0);
  BOOST_STATIC_ASSERT(num_tree_levels < 256);
  BOOST_STATIC_ASSERT(fanout_bits > 0);
  BOOST_STATIC_ASSERT(fanout_bits < 32);
  BOOST_STATIC_ASSERT(hash_length_in_bits > 0);
  BOOST_STATIC_ASSERT((hash_length_in_bits % fanout_bits) == 0);
  BOOST_STATIC_ASSERT(bitmap_length_in_bits > 0);
  BOOST_STATIC_ASSERT((bitmap_length_in_bits % 8) == 0);

  /////////////////////////////////////////////////
  //
  // layer 0: merkle node stuff
  //
  /////////////////////////////////////////////////
  
  struct merkle_node
  {    
    u8 level;
    dynamic_bitset<char> prefix;
    u64 total_num_leaves;
    dynamic_bitset<char> bitmap;
    map<size_t, string> slots;

    merkle_node() : level(0), prefix(0), 
		    total_num_leaves(0), 
		    bitmap(bitmap_length_in_bits) {}

    bool operator==(merkle_node const & other) const
    {
      return (level == other.level
	      && prefix == other.prefix
	      && total_num_leaves == other.total_num_leaves
	      && bitmap == other.bitmap
	      && slots == other.slots);
    }

    string node_identifier() const
    {
      ostringstream oss;
      oss.put(level);
      to_block_range(prefix, ostream_iterator<char>(oss));
      return oss.str();
    }

    dynamic_bitset<char> extended_prefix(size_t subtree) const
    {
      I(subtree < num_slots);
      dynamic_bitset<char> new_prefix = prefix;
      for (size_t i = fanout_bits; i > 0; --i)
	new_prefix.push_back(subtree & (1 << (i-1)));
      return new_prefix;
    }

    slot_state get_slot_state(size_t n) const
    {
      I(n < num_slots);
      I(2*n + 1 < bitmap.size());
      if (bitmap[2*n])
	{
	  if (bitmap[2*n+1])
	    return subtree_state;
	  else
	    return live_leaf_state;
	}
      else
	{
	  if (bitmap[2*n+1])
	    return dead_leaf_state;
	  else
	    return empty_state;
	}      
    }

    void set_slot_state(size_t n, slot_state st)
    {
      I(n < num_slots);
      I(2*n + 1 < bitmap.size());
      bitmap.reset(2*n);
      bitmap.reset(2*n+1);
      if (st == subtree_state || st == live_leaf_state)
	bitmap.set(2*n);
      if (st == subtree_state || st == dead_leaf_state)
	bitmap.set(2*n+1);
    }    
  };

  static size_t prefix_length_in_bits(size_t level)
  {
    return level * fanout_bits;
  }

  static size_t prefix_length_in_bytes(size_t level)
  {
    // level is the number of levels in tree this prefix has.
    // the prefix's binary *length* is the number of bytes used
    // to represent it, rounded up to a byte.
    size_t num_bits = prefix_length_in_bits(level);
    if (num_bits % 8 == 0)
      return num_bits / 8;
    else
      return (num_bits / 8) + 1;
  }

  static void write_node(merkle_node const & in, string & outbuf)
  {      
    ostringstream oss;
    oss.put(in.level);
    to_block_range(in.prefix, ostream_iterator<char>(oss));

    string tmp;
    write_datum_msb<u64>(in.total_num_leaves, tmp);
    oss.write(tmp.data(), tmp.size());

    to_block_range(in.bitmap, ostream_iterator<char>(oss));

    for (size_t slot = 0; slot < num_slots; ++slot)
      {
	if (in.get_slot_state(slot) != empty_state)
	  {
	    I(in.slots.find(slot) != in.slots.end());
	    string slot_val = in.slots.find(slot)->second;
	    oss.write(slot_val.data(), slot_val.size());
	  }
      }
    string hash = raw_sha1(oss.str());
    I(hash.size() == hash_length_in_bytes);
    outbuf = hash + oss.str();
  }
    
  static void read_node(string const & inbuf, merkle_node & out)
  {
    size_t pos = 0;
    string hash = extract_substring(inbuf, pos, hash_length_in_bytes, "node hash");
    out.level = extract_datum_msb<u8>(inbuf, pos, "node level");

    if (out.level >= num_tree_levels)
      throw bad_decode(F("node level is %d, exceeds maximum %d") 
		       % static_cast<int>(out.level)
		       % static_cast<int>(num_tree_levels));

    size_t prefixsz = prefix_length_in_bytes(out.level);
    require_bytes(inbuf, pos, prefixsz, "node prefix");   
    out.prefix.resize(prefix_length_in_bits(out.level));
    from_block_range(inbuf.begin() + pos, 
		     inbuf.begin() + pos + prefixsz,
		     out.prefix);
    pos += prefixsz;

    out.total_num_leaves = extract_datum_msb<u64>(inbuf, pos, "number of leaves");

    require_bytes(inbuf, pos, bitmap_length_in_bytes, "bitmap");
    out.bitmap.resize(bitmap_length_in_bits);
    from_block_range(inbuf.begin() + pos, 
		     inbuf.begin() + pos + bitmap_length_in_bytes,
		     out.bitmap);
    pos += bitmap_length_in_bytes;

    for (size_t slot = 0; slot < num_slots; ++slot)
      {
	if (out.get_slot_state(slot) != empty_state)
	  {
	    string slot_val = extract_substring(inbuf, pos, hash_length_in_bytes, "slot value");
	    out.slots.insert(make_pair(slot, slot_val));
	  }
      }
    
    assert_end_of_buffer(inbuf, pos, "node");
    string checkhash = raw_sha1(inbuf.substr(hash_length_in_bytes));
    if (hash != checkhash)
      throw bad_decode(F("mismatched node hash value %s, expected %s") 
		       % xform<HexEncoder>(checkhash) % xform<HexEncoder>(hash));
  }



  /////////////////////////////////////////////////
  //
  // layer 1: command packet stuff
  //
  /////////////////////////////////////////////////

  static u8 const current_protocol_version = 1;
  struct command
  {
    static size_t const minsz = 
    1    // version
    + 1  // cmd code
    + 4  // length
    + 4; // adler32    

    BOOST_STATIC_CONSTANT(size_t, payload_limit = 0xffffff);
    BOOST_STATIC_CONSTANT(size_t, maxsz = minsz + payload_limit);

    u8 version;
    command_code cmd_code;
    string payload;

    command() : version(current_protocol_version),
		cmd_code(bye_cmd)
    {}

    size_t encoded_size() 
    {
      return minsz + payload.size();
    }
    bool operator==(command const & other) const 
    {
      return version == other.version &&
 	cmd_code == other.cmd_code &&
 	payload == other.payload;
    }
  };

  static void write_command(command const & in, string & out)
  {
    out += static_cast<char>(in.version);
    out += static_cast<char>(in.cmd_code);
    write_datum_msb<u32>(in.payload.size(), out);
    out += in.payload;
    adler32 check(in.payload.data(), in.payload.size());
    write_datum_msb<u32>(check.sum(), out);
  }
  
  static bool read_command(string & inbuf, command & out)
  {
    size_t pos = 0;

    if (inbuf.size() < command::minsz)
      return false;

    out.version = extract_datum_msb<u8>(inbuf, pos, "command protocol number");
    int v = current_protocol_version;
    if (out.version != current_protocol_version)
      throw bad_decode(F("protocol version mismatch: wanted '%d' got '%d'") 
		       % v % static_cast<int>(out.version));

    u8 cmd_byte = extract_datum_msb<u8>(inbuf, pos, "command code");
    switch (static_cast<command_code>(cmd_byte))
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
	out.cmd_code = static_cast<command_code>(cmd_byte);
	break;
      default:
	throw bad_decode(F("unknown command code 0x%x") % static_cast<int>(cmd_byte));
      }
    
    u32 payload_len = extract_datum_msb<u32>(inbuf, pos, "command payload length");

    // they might have given us a bogus size
    if (payload_len > command::payload_limit)
      throw bad_decode(F("oversized payload of '%d' bytes") % payload_len);

    // there might not be enough data yet in the input buffer
    if (inbuf.size() < command::minsz + payload_len)
      return false;
    
    out.payload = extract_substring(inbuf, pos, payload_len, "command payload");

    // they might have given us bogus data
    u32 checksum = extract_datum_msb<u32>(inbuf, pos, "command checksum");
    adler32 check(out.payload.data(), out.payload.size());
    if (checksum != check.sum())
      throw bad_decode(F("bad checksum %d vs. %d bytes") % checksum % check.sum());

    return true;    
  }

  static void read_hello_cmd_payload(string const & in, string & server, string & nonce)
  {
    size_t pos = 0;
    // syntax is <server:20 bytes sha1> <nonce:20 random bytes>
    server = extract_substring(in, pos, hash_length_in_bytes, "hello command, server identifier");
    nonce = extract_substring(in, pos, hash_length_in_bytes, "hello command, nonce");
    assert_end_of_buffer(in, pos, "hello command payload");
  }

  static void write_hello_cmd_payload(string const & server, string const & nonce, string & out)
  {
    I(server.size() == hash_length_in_bytes);
    I(nonce.size() == hash_length_in_bytes);
    out += server;
    out += nonce;
  }

  static void read_auth_cmd_payload(string const & in, 
				    protocol_role & role, 
				    string & collection,
				    string & client, 
				    string & nonce1, 
				    string & nonce2,
				    string & signature)
  {
    size_t pos = 0;
    // syntax is <role:1 byte> <len1: 4 bytes> <collection: len1 bytes>
    //           <client: 20 bytes sha1> <nonce1: 20 random bytes> <nonce2: 20 random bytes>
    //           <len2: 4 bytes> <signature: len2 bytes>
    u8 role_byte = extract_datum_msb<u8>(in, pos, "auth command, role");
    if (role_byte != static_cast<u8>(source_role)
	&& role_byte != static_cast<u8>(sink_role)
	&& role_byte != static_cast<u8>(source_and_sink_role))
      throw bad_decode(F("unknown role specifier 0x%x") % static_cast<int>(role_byte));
    role = static_cast<protocol_role>(role_byte);
    u32 coll_len = extract_datum_msb<u32>(in, pos, "auth command, collection name length");
    collection = extract_substring(in, pos, coll_len, "auth command, collection name");
    client = extract_substring(in, pos, hash_length_in_bytes, "auth command, client identifier");
    nonce1 = extract_substring(in, pos, hash_length_in_bytes, "auth command, nonce1");
    nonce2 = extract_substring(in, pos, hash_length_in_bytes, "auth command, nonce2");
    u32 sig_len = extract_datum_msb<u32>(in, pos, "auth command, signature length");
    signature = extract_substring(in, pos, sig_len, "auth command, signature");
    assert_end_of_buffer(in, pos, "auth command payload");
  }

  static void write_auth_cmd_payload(protocol_role role, 
				     string const & collection, 
				     string const & client,
				     string const & nonce1, 
				     string const & nonce2, 
				     string const & signature, 
				     string & out)
  {
    I(client.size() == hash_length_in_bytes);
    I(nonce1.size() == hash_length_in_bytes);
    I(nonce2.size() == hash_length_in_bytes);
    out += static_cast<char>(role);
    write_datum_msb<u32>(collection.size(), out);
    out += collection;
    out += client;
    out += nonce1;
    out += nonce2;
    write_datum_msb<u32>(signature.size(), out);
    out += signature;
  }
			 

  static void read_confirm_cmd_payload(string const & in, string & signature)
  {
    size_t pos = 0;
    // syntax is <len: 4 bytes> <signature: len bytes>
    u32 sig_len = extract_datum_msb<u32>(in, pos, "confirm command, signature length");
    signature = extract_substring(in, pos, sig_len, "confirm command, signature");
    assert_end_of_buffer(in, pos, "confirm command payload");
  }
  
  static void write_confirm_cmd_payload(string const & signature, string & out)
  {
    write_datum_msb<u32>(signature.size(), out);
    out += signature;
  }
  
  static void read_refine_cmd_payload(string const & in, merkle_node & node)
  {
    // syntax is <node: a merkle tree node>
    read_node(in, node);
  }

  static void write_refine_cmd_payload(merkle_node const & node, string & out)
  {
    write_node(node, out);
  }

  static void read_done_cmd_payload(string const & in, u8 & level)
  {
    size_t pos = 0;
    // syntax is: <level: 1 byte>
    level = extract_datum_msb<u8>(in, pos, "done command, level number");
    assert_end_of_buffer(in, pos, "done command payload");
  }

  static void write_done_cmd_payload(u8 level, string & out)
  {
    out += static_cast<char>(level);
  }

  static void read_describe_cmd_payload(string const & in, string & id)
  {
    size_t pos = 0;
    // syntax is: <id: 20 bytes sha1>
    id = extract_substring(in, pos, hash_length_in_bytes, "describe command, item identifier");
    assert_end_of_buffer(in, pos, "describe command payload");
  }

  static void write_describe_cmd_payload(string const & id, string & out)
  {
    I(id.size() == hash_length_in_bytes);
    out += id;
  }

  static void read_description_cmd_payload(string const & in, 
					   string & head, 
					   u64 & len,
					   vector<string> & predecessors)
  {
    size_t pos = 0;
    // syntax is: <id: 20 bytes sha1> <len: 8 bytes> 
    //            <npred: 1 byte> <pred1: 20 bytes sha1> ... <predN>
    head = extract_substring(in, pos, hash_length_in_bytes, "description command, item identifier");
    len = extract_datum_msb<u64>(in, pos, "description command, data length");
    u8 npred = extract_datum_msb<u8>(in, pos, "description command, number of predecessors");
    predecessors.reserve(npred);
    for (u8 i = 0; i < npred; ++i)
      {
	string tmp = extract_substring(in, pos, hash_length_in_bytes, "description command, predecessor identifier");
	predecessors.push_back(tmp);
      }
    assert_end_of_buffer(in, pos, "description command payload");
  }

  static void write_description_cmd_payload(string const & head, 
					    u64 len,
					    vector<string> const & predecessors,
					    string & out)
  {
    I(head.size() == hash_length_in_bytes);
    I(predecessors.size() <= 0xff);
    out += head;
    write_datum_msb<u64>(len, out);
    out += static_cast<char>(predecessors.size());
    for (vector<string>::const_iterator i = predecessors.begin();
	 i != predecessors.end(); ++i)
      {
	I(i->size() == hash_length_in_bytes);
	out += *i;
      }
  }

  static void read_send_data_cmd_payload(string const & in, 
					 string & head,
					 vector<pair<u64, u64> > & fragments)
  {
    size_t pos = 0;
    // syntax is: <id: 20 bytes sha1> <nfrag: 1 byte> 
    //            <pos1: 8 bytes> <len1: 8 bytes> ... <posN: 8 bytes> <lenN: 8 bytes>
    head = extract_substring(in, pos, hash_length_in_bytes, "send_data command, item identifier");
    u8 nfrag = extract_datum_msb<u8>(in, pos, "send_data command, fragment count");
    fragments.reserve(nfrag);
    for (u8 i = 0; i < nfrag; ++i)
      {
	u64 fpos = extract_datum_msb<u64>(in, pos, "send_data command, fragment position");
	u64 flen = extract_datum_msb<u64>(in, pos, "send_data command, fragment length");
	fragments.push_back(make_pair(fpos, flen));
      }
    assert_end_of_buffer(in, pos, "send_data command payload");
  }

  static void write_send_data_cmd_payload(string const & head,
					  vector<pair<u64, u64> > const & fragments,
					  string & out)
  {
    I(head.size() == hash_length_in_bytes);
    I(fragments.size() <= 0xff);
    out += head;
    out += static_cast<char>(fragments.size());
    for(vector<pair<u64, u64> >::const_iterator i = fragments.begin();
	i != fragments.end(); ++i)
      {
	write_datum_msb<u64>(i->first, out);
	write_datum_msb<u64>(i->second, out);
      }    
  }

  static void read_send_delta_cmd_payload(string const & in, 
					  string & head,
					  string & base)
  {
    size_t pos = 0;
    // syntax is: <src: 20 bytes sha1> <dst: 20 bytes sha1>
    head = extract_substring(in, pos, hash_length_in_bytes, "send_delta command, head item identifier");
    base = extract_substring(in, pos, hash_length_in_bytes, "send_delta command, base item identifier");
    assert_end_of_buffer(in, pos, "send_delta command payload");
  }

  static void write_send_delta_cmd_payload(string const & head,
					   string const & base,
					   string & out)
  {
    I(head.size() == hash_length_in_bytes);
    I(base.size() == hash_length_in_bytes);
    out += head;
    out += base;
  }

  static void read_data_cmd_payload(string const & in,
				    string & id,
				    vector< pair<pair<u64,u64>,string> > & fragments)
  {
    size_t pos = 0;
    // syntax is: <id: 20 bytes sha1> <nfrag: 1 byte> 
    //            <pos1: 8 bytes> <len1: 8 bytes> 
    //            <compressed_p1: 1 byte> <clen1? 4 bytes> <dat1: len1 or clen1 bytes>
    //            ...
    //            <posN: 8 bytes> <lenN: 8 bytes> 
    //            <compressed_pN: 1 byte> <clenN? 4 bytes if compressed> <datN: lenN or clenN bytes>
    
    id = extract_substring(in, pos, hash_length_in_bytes, "data command, item identifier");
    u8 nfrag = extract_datum_msb<u8>(in, pos, "data command, fragment count");
    
    fragments.reserve(nfrag);
    for (u8 i = 0; i < nfrag; ++i)
      {
	u64 fpos = extract_datum_msb<u64>(in, pos, "data command, fragment position");
	u64 flen = extract_datum_msb<u64>(in, pos, "data command, fragment length");
	u8 compressed_p = extract_datum_msb<u8>(in, pos, "data command, compression flag");
	string txt;
	if (compressed_p == 1)
	  {
	    u32 clen = extract_datum_msb<u32>(in, pos, "data command, compressed fragment length");
	    txt = extract_substring(in, pos, clen, "data command, compressed fragment");
	    txt = xform<Gunzip>(txt);
	  }
	else
	  {
	    txt = extract_substring(in, pos, flen, "data command, non-compressed fragment");
	  }
	if (txt.size() != flen)
	  throw bad_decode(F("data fragment size mismatch, %d vs. %d") % txt.size() % flen);
	fragments.push_back(make_pair(make_pair(fpos, flen), txt));
      }
    assert_end_of_buffer(in, pos, "data command payload");
  }

  static void write_data_cmd_payload(string const & id,
				     vector< pair<pair<u64,u64>,string> > const & fragments,
				     string & out)
  {
    I(id.size() == hash_length_in_bytes);
    I(fragments.size() <= 0xff);
    out += id;
    out += static_cast<char>(fragments.size());
    for (vector< pair<pair<u64,u64>,string> >::const_iterator i = fragments.begin();
	 i != fragments.end(); ++i)
      {
	I(i->first.second == i->second.size());
	write_datum_msb<u64>(i->first.first, out);
	write_datum_msb<u64>(i->first.second, out);
	string tmp = i->second;

	if (tmp.size() > minimum_bytes_to_bother_with_gzip)
	  {
	    tmp = xform<Gzip>(tmp);
	    out += static_cast<char>(1); // compressed flag
	    write_datum_msb<u32>(tmp.size(), out);
	  }
	else
	  {
	    out += static_cast<char>(0); // compressed flag	    
	  }

	I(tmp.size() <= i->first.second);
	I(tmp.size() <= command::payload_limit);
	out += tmp;
      }
  }


  static void read_delta_cmd_payload(string const & in, 
				     string & src, string & dst, 
				     u64 & src_len, string & del)
  {
    size_t pos = 0;
    // syntax is: <src: 20 bytes sha1> <dst: 20 bytes sha1> <src_len: 8 bytes> 
    //            <compressed_p: 1 byte> <clen: 4 bytes> <dat: clen bytes>    
    src = extract_substring(in, pos, hash_length_in_bytes, "delta command, source identifier");
    dst = extract_substring(in, pos, hash_length_in_bytes, "delta command, destination identifier");
    src_len = extract_datum_msb<u64>(in, pos, "delta command, source length");
    u8 compressed_p = extract_datum_msb<u8>(in, pos, "delta command, compression flag");
    u32 clen = extract_datum_msb<u32>(in, pos, "delta command, compressed delta length");
    string tmp_del = extract_substring(in, pos, clen, "delta command, delta content");
    if (compressed_p == 0)
      del = tmp_del;
    else
      del = xform<Gunzip>(tmp_del);
    assert_end_of_buffer(in, pos, "delta command payload");
  }

  static void write_delta_cmd_payload(string const & src, string const & dst, 
				      u64 src_len, string const & del,
				      string & out)
  {
    I(src.size() == hash_length_in_bytes);
    I(dst.size() == hash_length_in_bytes);
    out += src;
    out += dst;
    write_datum_msb<u64>(src_len, out);

    string tmp = del;

    if (tmp.size() > minimum_bytes_to_bother_with_gzip)
      {
	tmp = xform<Gzip>(tmp);
	out += static_cast<char>(1); // compressed flag
      }
    else
      {
	out += static_cast<char>(0); // compressed flag	    
      }
    I(tmp.size() <= command::payload_limit);
    write_datum_msb<u32>(tmp.size(), out);
    out += tmp;
  }

  /////////////////////////////////////////////////
  //
  // layer 2: protocol session stuff
  //
  /////////////////////////////////////////////////
  
  struct session
  {
    protocol_role const role;
    protocol_voice const voice;
    vector<utf8> const & collections;
    app_state & app;

    string peer_id;
    socket_type fd;
    Stream stream;  

    string inbuf; 
    string outbuf;
    deque<command> inq;

    protocol_phase phase;
    utf8 collection;
    string remote_peer_id;
    bool authenticated;

    time_t last_io_time;
    boost::scoped_ptr<AutoSeededRandomPool> prng;
  
    session(protocol_role role,
	    protocol_voice voice,
	    vector<utf8> const & collections,
	    app_state & app,
	    string const & peer,
	    socket_type sock, 
	    Timeout const & to) : 
      role(role),
      voice(voice),
      collections(collections),
      app(app),
      peer_id(peer),
      fd(sock),
      stream(sock, to),
      phase(authentication_phase),
      last_io_time(::time(NULL))
    {
      // we will panic here if the user doesn't like urandom and we can't give
      // them a real entropy-driven random.  
      bool request_blocking_rng = false;
      if (!app.lua.hook_non_blocking_rng_ok())
	{
#ifndef BLOCKING_RNG_AVAILABLE 
	  throw oops("no blocking RNG available and non-blocking RNG rejected");
#else
	  request_blocking_rng = true;
#endif
	}  
      prng.reset(new AutoSeededRandomPool(request_blocking_rng));
    }

    void mark_recent_io()
    {
      last_io_time = ::time(NULL);
    }
  
    Probe::ready_type which_events() const
    {    
      if (outbuf.empty())
	{
	  if (inbuf.size() < command::maxsz)
	    return Probe::ready_read | Probe::ready_oobd;
	  else
	    return Probe::ready_oobd;
	}
      else
	{
	  if (inbuf.size() < command::maxsz)
	    return Probe::ready_write | Probe::ready_read | Probe::ready_oobd;
	  else
	    return Probe::ready_write | Probe::ready_oobd;
	}	    
    }

    bool read_some()
    {
      I(inbuf.size() < command::maxsz);
      char tmp[constants::bufsz];
      signed_size_type count = stream.read(tmp, sizeof(tmp));
      if(count > 0)
	{
	  L(F("read %d bytes from fd %d (peer %s)\n") % count % fd % peer_id);
	  inbuf.append(string(tmp, tmp + count));
	  mark_recent_io();
	  return true;
	}
      else
	return false;
    }

    bool write_some()
    {
      I(!outbuf.empty());    
      signed_size_type count = stream.write(outbuf.data(), outbuf.size());
      if(count > 0)
	{
	  L(F("wrote %d bytes to fd %d (peer %s)\n") % count % fd % peer_id);
	  outbuf.erase(0, count);
	  mark_recent_io();
	  return true;
	}
      else
	return false;
    }

    // senders

    void queue_bye_cmd() 
    {
      command cmd;
      cmd.cmd_code = bye_cmd;
      write_command(cmd, outbuf);
    }

    void queue_done_cmd(u8 level) 
    {
      command cmd;
      cmd.cmd_code = done_cmd;
      write_done_cmd_payload(level, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_hello_cmd(string const & server, 
			 string const & nonce) 
    {
      command cmd;
      cmd.cmd_code = hello_cmd;
      write_hello_cmd_payload(server, nonce, cmd.payload);
      write_command(cmd, outbuf);
    }
    
    void queue_auth_cmd(protocol_role role, 
			  string const & collection, 
			  string const & client, 
			  string const & nonce1, 
			  string const & nonce2, 
			  string const & signature)
    {
      command cmd;
      cmd.cmd_code = auth_cmd;
      write_auth_cmd_payload(role, collection, client, 
			     nonce1, nonce2, signature, 
			     cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_confirm_cmd(string const & signature)
    {
      command cmd;
      cmd.cmd_code = confirm_cmd;
      write_confirm_cmd_payload(signature, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_refine_cmd(merkle_node const & node)
    {
      command cmd;
      cmd.cmd_code = refine_cmd;
      write_refine_cmd_payload(node, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_describe_cmd(string const & head)
    {
      command cmd;
      cmd.cmd_code = describe_cmd;
      write_describe_cmd_payload(head, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_description_cmd(string const & head, 
			       u64 len, 
			       vector<string> const & predecessors)
    {
      command cmd;
      cmd.cmd_code = description_cmd;
      write_description_cmd_payload(head, len, predecessors, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_send_data_cmd(string const & head, 
			     vector<pair<u64, u64> > const & fragments)
    {
      command cmd;
      cmd.cmd_code = send_data_cmd;
      write_send_data_cmd_payload(head, fragments, cmd.payload);
      write_command(cmd, outbuf);
    }
    
    void queue_send_delta_cmd(string const & head, 
			      string const & base)
    {
      command cmd;
      cmd.cmd_code = send_delta_cmd;
      write_send_delta_cmd_payload(head, base, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_data_cmd(string const & id, 
			vector< pair<pair<u64,u64>,string> > const & fragments)
    {
      command cmd;
      cmd.cmd_code = data_cmd;
      write_data_cmd_payload(id, fragments, cmd.payload);
      write_command(cmd, outbuf);
    }

    void queue_delta_cmd(string const & src, 
			 string const & dst, 
			 u64 src_len, 
			 string const & del)
    {
      command cmd;
      cmd.cmd_code = delta_cmd;
      write_delta_cmd_payload(src, dst, src_len, del, cmd.payload);
      write_command(cmd, outbuf);
    }

    // processors

    bool process_bye_cmd() 
    {
      return false;
    }

    bool process_done_cmd(u8 level) 
    {
      return true;
    }

    bool process_hello_cmd(string const & server, 
			   string const & nonce) 
    {
      queue_bye_cmd();
      return false;
    }
    
    bool process_auth_cmd(protocol_role role, 
			  string const & collection, 
			  string const & client, 
			  string const & nonce1, 
			  string const & nonce2, 
			  string const & signature)
    {
      return true;
    }

    bool process_confirm_cmd(string const & signature)
    {
      return true;
    }

    bool process_refine_cmd(merkle_node const & node)
    {
      return true;
    }

    bool process_describe_cmd(string const & head)
    {
      return true;
    }

    bool process_description_cmd(string const & head, 
				 u64 len, 
				 vector<string> const & predecessors)
    {
      return true;
    }

    bool process_send_data_cmd(string const & head, 
			       vector<pair<u64, u64> > const & fragments)
    {
      return true;
    }

    bool process_send_delta_cmd(string const & head, 
				string const & base)
    {
      return true;
    }

    bool process_data_cmd(string const & id, 
			  vector< pair<pair<u64,u64>,string> > const & fragments)
    {
      return true;
    }

    bool process_delta_cmd(string const & src, 
			   string const & dst, 
			   u64 src_len, 
			   string const & del)
    {
      return true;
    }


    static inline void require(bool check, string const & context)
    {
      if (!check) 
	throw bad_decode(F("check of '%s' failed") % context);
    }

    bool dispatch_payload(command const & cmd)
    {

      switch (cmd.cmd_code)
	{

	case bye_cmd:
	  return process_bye_cmd();
	  break;
	  
	case hello_cmd:
	  require(! authenticated, "hello command received when not authenticated");
	  require(voice == client_voice, "hello command received in client voice");
	  require(phase == authentication_phase, "hello command received in auth phase");
	  {
	    string server, nonce;
	    read_hello_cmd_payload(cmd.payload, server, nonce);
	    return process_hello_cmd(server, nonce);
	  }
	  break;

	case auth_cmd:
	  require(! authenticated, "auth command received when not authenticated");
	  require(voice == server_voice, "auth command received in server voice");
	  require(phase == authentication_phase, "auth command received in auth phase");
	  {
	    protocol_role role;
	    string collection, client, nonce1, nonce2, signature;
	    read_auth_cmd_payload(cmd.payload, role, collection, client, nonce1, nonce2, signature);
	    return process_auth_cmd(role, collection, client, nonce1, nonce2, signature);
	  }
	  break;

	case confirm_cmd:
	  require(! authenticated, "confirm command received when not authenticated");
	  require(voice == client_voice, "confirm command received in client voice");
	  require(phase == authentication_phase, "confirm command received in auth phase");
	  {
	    string signature;
	    read_confirm_cmd_payload(cmd.payload, signature);
	    return process_confirm_cmd(signature);
	  }
	  break;

	case refine_cmd:
	  require(authenticated, "refine command received when authenticated");
	  require(phase == refinement_phase, "refine command received in refinement phase");
	  {
	    merkle_node node;
	    read_refine_cmd_payload(cmd.payload, node);
	    return process_refine_cmd(node);
	  }
	  break;

	case done_cmd:
	  require(authenticated, "done command received when authenticated");
	  require(phase == refinement_phase, "done command received in refinement phase");
	  {
	    u8 level;
	    read_done_cmd_payload(cmd.payload, level);
	    return process_done_cmd(level);
	  }
	  break;

	case describe_cmd:
	  require(authenticated, "describe command received when authenticated");
	  require(phase == refinement_phase, "describe command received in refinement phase");
	  require(role == source_role ||
		  role == source_and_sink_role, 
		  "describe command received in source or source/sink role");
	  {
	    string id;
	    read_describe_cmd_payload(cmd.payload, id);
	    return process_describe_cmd(id);
	  }
	  break;

	case description_cmd:
	  require(authenticated, "description command received when authenticated");
	  require(phase == refinement_phase, "description command received in refinement phase");
	  require(role == sink_role ||
		  role == source_and_sink_role, 
		  "description command received in sink or source/sink role");
	  {
	    string head;
	    u64 len;
	    vector<string> predecessors;
	    read_description_cmd_payload(cmd.payload, head, len, predecessors);
	    return process_description_cmd(head, len, predecessors);
	  }
	  break;

	case send_data_cmd:
	  require(authenticated, "send_data command received when authenticated");
	  require(phase == transmission_phase, "send_data command received in transmission phase");
	  require(role == source_role ||
		  role == source_and_sink_role, 
		  "send_data command received in source or source/sink role");
	  {
	    string head;
	    vector<pair<u64, u64> > fragments;
	    read_send_data_cmd_payload(cmd.payload, head, fragments);
	    return process_send_data_cmd(head, fragments);
	  }
	  break;

	case send_delta_cmd:
	  require(authenticated, "send_delta command received when authenticated");
	  require(phase == transmission_phase, "send_delta command received in transmission phase");
	  require(role == source_role ||
		  role == source_and_sink_role, 
		  "send_delta command received in source or source/sink role");
	  {
	    string head, base;
	    read_send_delta_cmd_payload(cmd.payload, head, base);
	    return process_send_delta_cmd(head, base);
	  }

	case data_cmd:
	  require(authenticated, "data command received when authenticated");
	  require(phase == transmission_phase, "data command received in transmission phase");
	  require(role == sink_role ||
		  role == source_and_sink_role, 
		  "data command received in source or source/sink role");
	  {
	    string id;
	    vector< pair<pair<u64,u64>,string> > fragments;
	    read_data_cmd_payload(cmd.payload, id, fragments);
	    return process_data_cmd(id, fragments);
	  }
	  break;

	case delta_cmd:
	  require(authenticated, "delta command received when authenticated");
	  require(phase == transmission_phase, "delta command received in transmission phase");
	  require(role == sink_role ||
		  role == source_and_sink_role, 
		  "delta command received in source or source/sink role");
	  {
	    string src, dst, del;
	    u64 src_len;
	    read_delta_cmd_payload(cmd.payload, src, dst, src_len, del);
	    return process_delta_cmd(src, dst, src_len, del);
	  }
	  break;	  
	}
      return false;
    }

    string mk_nonce()
    {
      char buf[constants::bufsz];
      prng->GenerateBlock(reinterpret_cast<byte *>(buf), constants::bufsz);
      return raw_sha1(string(buf, constants::bufsz));
    }

    // this ticks off the whole cascade starting from "hello"
    void begin_service()
    {      
      queue_hello_cmd(raw_sha1("myself"), mk_nonce());
    }
      
    bool process()
    {
      try 
	{
	  command cmd;
	  L(F("processing %d byte input buffer from peer %s\n") % inbuf.size() % peer_id);
	  if (read_command(inbuf, cmd))
	    {
	      inbuf.erase(0, cmd.encoded_size());
	      return dispatch_payload(cmd);
	    }	  
	  if (inbuf.size() >= command::maxsz)
	    {
	      W(F("input buffer for peer %s is overfull after command dispatch\n") % peer_id);
	      return false;
	    }
	  return true;
	}
      catch (bad_decode & bd)
	{
	  W(F("caught bad_decode exception processing peer %s: '%s'\n") % peer_id % bd.what);
	  return false;
	}
    }
  };

  

  /////////////////////////////////////////////////
  //
  // layer 3: i/o buffer <-> network loops
  //
  /////////////////////////////////////////////////

  static void call_server(protocol_role role,
			  vector<utf8> const & collections,
			  app_state & app,
			  utf8 const & address,
			  port_type default_port,
			  unsigned long timeout_seconds)
  {
    Probe probe;
    Timeout timeout(static_cast<long>(timeout_seconds));

    // FIXME: split into labels and convert to ace here.

    P(F("connecting to %s\n") % address());
    Stream server(address().c_str(), default_port, timeout); 
    session sess(role, client_voice, collections, app, 
		 address(), server.get_socketfd(), timeout);
    
    while (true)
      { 
	probe.clear();
	probe.add(sess.stream, sess.which_events());

	Probe::result_type res = probe.ready(timeout);
	Probe::ready_type event = res.second;
	socket_type fd = res.first;
      
	if (fd == -1) 
	  {
	    P(F("timed out waiting for I/O with peer %s, disconnecting\n") % sess.peer_id);
	    return;
	  }

	if (event & Probe::ready_read)
	  {
	    if (sess.read_some())
	      {
		if (!sess.process())
		  {
		    P(F("processing on fd %d (peer %s) finished, disconnecting\n") % fd % sess.peer_id);
		    return;
		  }
	      }
	    else
	      {
		P(F("read from fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
		return;
	      }
	  }
      
	if (event & Probe::ready_write)
	  {
	    if (! sess.write_some())
	      {
		P(F("write on fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
		return;
	      }
	  }
      
	if (event & Probe::ready_oobd)
	  {
	    P(F("got OOB data on fd %d (peer %s), disconnecting\n") % fd % sess.peer_id);
	    return;
	  }      
      }  
  }

  static void serve_connections(protocol_role role,
				vector<utf8> const & collections,
				app_state & app,
				utf8 const & address,
				port_type default_port,
				unsigned long timeout_seconds,
				unsigned long session_limit)
  {
    Probe probe;
    Timeout forever, timeout(static_cast<long>(timeout_seconds));
    Address addr(address().c_str(), default_port, true);
    StreamServer server(addr, timeout);
  
    map<socket_type, shared_ptr<session> > sessions;

    P(F("beginning service on %s : %d\n") 
      % addr.get_name() % addr.get_port());
  
    while (true)
      {      
	probe.clear();

	if (sessions.size() >= session_limit)
	  {
	    W(F("session limit %d reached, some connections will be refused\n") % session_limit);
	  }
	else
	  {
	    probe.add(server);
	  }

	for (map<socket_type, shared_ptr<session> >::const_iterator i = sessions.begin();
	     i != sessions.end(); ++i)
	  probe.add(i->second->stream, i->second->which_events());
      
	Probe::result_type res = probe.ready(sessions.empty() ? forever : timeout);
	Probe::ready_type event = res.second;
	socket_type fd = res.first;
      
	if (fd == -1) 
	  {
	    L(F("timed out waiting for I/O (listening on %s : %d)\n") 
	      % addr.get_name() % addr.get_port());
	  }
      
	// we either got a new connection
	else if (fd == server)
	  {
	    L(F("accepting new connection on %s : %d\n") 
	      % addr.get_name() % addr.get_port());
	    Peer client = server.accept_connection();
	    
	    if (!client) 
	      {
		L(F("accept() returned a dead client\n"));
	      }
	    else
	      {
		P(F("accepted new client connection from %s\n") % client);
		
		shared_ptr<session> sess(new session(role, server_voice, collections, app,
						     lexical_cast<string>(client), 
						     client.get_socketfd(), timeout));
		sess->begin_service();
		sessions.insert(make_pair(client.get_socketfd(), sess));
	      }
	  }
      
	// or an existing session woke up
	else
	  {
	    map<socket_type, shared_ptr<session> >::iterator i;
	    i = sessions.find(fd);
	    if (i == sessions.end())
	      {
		L(F("got woken up for action on unknown fd %d\n") % fd);
	      }
	    else
	      {
		shared_ptr<session> sess = i->second;		
		if (event & Probe::ready_read)
		  {
		    if (sess->read_some())
		      {
			if (!sess->process())
			  {
			    P(F("fd %d (peer %s) processing finished, disconnecting\n") 
			      % fd % sess->peer_id);
			    sessions.erase(i);
			  }
		      }
		    else
		      {
			P(F("fd %d (peer %s) read failed, disconnecting\n") 
			  % fd % sess->peer_id);
			sessions.erase(i);
		      }
		  }
		
		if (event & Probe::ready_write)
		  {
		    if (! sess->write_some())
		      {
			P(F("fd %d (peer %s) write failed, disconnecting\n") 
			  % fd % sess->peer_id);
			sessions.erase(i);
		      }
		  }
		
		if (event & Probe::ready_oobd)
		  {
		    P(F("got some OOB data on fd %d (peer %s), disconnecting\n") % fd % sess->peer_id);
		    sessions.erase(i);
		  }
	      }
	  }
	
	// kill any clients which haven't done any i/o inside the timeout period
	set<socket_type> dead_clients;
	time_t now = ::time(NULL);
	for (map<socket_type, shared_ptr<session> >::const_iterator i = sessions.begin();
	     i != sessions.end(); ++i)
	  {
	    if (static_cast<unsigned long>(i->second->last_io_time + timeout_seconds) 
		< static_cast<unsigned long>(now))
	      {
		P(F("fd %d (peer %s) has been idle too long, disconnecting\n") 
		  % i->first % i->second->peer_id);
		dead_clients.insert(i->first);
	      }
	  }
	for (set<socket_type>::const_iterator i = dead_clients.begin();
	     i != dead_clients.end(); ++i)
	  {
	    sessions.erase(*i);
	  }
      }
  }
};


/////////////////////////////////////////////////
//
// layer 4: monotone interface layer
//
/////////////////////////////////////////////////

static void load_merkle_node(app_state & app,
			     string const & type,
			     utf8 const & collection,			
			     size_t level,
			     hexenc<prefix> const & hpref,
			     netsync_protocol::merkle_node & node)
{
  base64<merkle> emerk;
  merkle merk;  
  app.db.get_merkle_node(type, collection, level, hpref, emerk);
  decode_base64(emerk, merk);
  netsync_protocol::read_node(merk(), node);
}

// returns the first hashsz bytes of the serialized node, which is 
// the hash of its contents.

static string store_merkle_node(app_state & app,
				string const & type,
				utf8 const & collection,
				netsync_protocol::merkle_node & node)
{
  string out;
  ostringstream oss;
  to_block_range(node.prefix, ostream_iterator<char>(oss));
  hexenc<prefix> hpref;
  encode_hexenc(prefix(oss.str()), hpref);
  netsync_protocol::write_node(node, out);
  base64<merkle> emerk;
  encode_base64(merkle(out), emerk);
  app.db.put_merkle_node(type, collection, node.level, hpref, emerk);
  I(out.size() >= netsync_protocol::hash_length_in_bytes);
  return out.substr(0, netsync_protocol::hash_length_in_bytes);
}

static string insert_into_merkle_tree(app_state & app,
				      bool live_p,
				      string const & type,
				      utf8 const & collection,
				      string const & leaf,
				      size_t level)
{
  I(netsync_protocol::hash_length_in_bytes == leaf.size());
  I(netsync_protocol::fanout_bits * (level + 1) <= netsync_protocol::hash_length_in_bits);

  hexenc<id> hleaf;
  encode_hexenc(id(leaf), hleaf);
  
  dynamic_bitset<char> pref;
  pref.resize(leaf.size() * 8);
  from_block_range(leaf.begin(), leaf.end(), pref);

  size_t slotnum = 0;
  for (size_t i = netsync_protocol::fanout_bits; i > 0; --i)
    {
      slotnum <<= 1;
      if (pref[level * netsync_protocol::fanout_bits + (i-1)])
	slotnum |= static_cast<size_t>(1);
      else
	slotnum &= static_cast<size_t>(~1);
    }

  pref.resize(level * netsync_protocol::fanout_bits);
  ostringstream oss;
  to_block_range(pref, ostream_iterator<char>(oss));
  hexenc<prefix> hpref;
  encode_hexenc(prefix(oss.str()), hpref);

  L(F("inserting %s leaf %s into slot 0x%x at %s node with prefix %s, level %d\n") 
    % (live_p ? "live" : "dead") % hleaf % slotnum % type % hpref % level);
  
  netsync_protocol::merkle_node node;
  if (app.db.merkle_node_exists(type, collection, level, hpref))
    {
      load_merkle_node(app, type, collection, level, hpref, node);
      slot_state st = node.get_slot_state(slotnum);
      switch (st)
	{
	case live_leaf_state:
	case dead_leaf_state:
	  if (node.slots[slotnum] == leaf)
	    {
	      L(F("found existing entry for %s at slot 0x%x of %s node %s, level %d\n") 
		% hleaf % slotnum % type % hpref % level);
	      if (st == dead_leaf_state && live_p)
		{
		  L(F("changing setting from dead to live, for %s at slot 0x%x of %s node %s, level %d\n") 
		    % hleaf % slotnum % type % hpref % level);
		  node.set_slot_state(slotnum, live_leaf_state);
		}
	      else if (st == live_leaf_state && !live_p)
		{
		  L(F("changing setting from live to dead, for %s at slot 0x%x of %s node %s, level %d\n") 
		    % hleaf % slotnum % type % hpref % level);
		  node.set_slot_state(slotnum, dead_leaf_state);
		}
	    }
	  else
	    {
	      L(F("pushing existing leaf %s in slot 0x%x of %s node %s, level %d into subtree\n")
		% hleaf % slotnum % type % hpref % level);
	      insert_into_merkle_tree(app, (st == live_leaf_state ? true : false),
				      type, collection, node.slots[slotnum], level+1);
	      string subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
	      hexenc<id> hsub;
	      encode_hexenc(id(subtree_hash), hsub);
	      L(F("changing setting to subtree, with %s at slot 0x%x of node %s, level %d\n") 
		% hsub % slotnum % hpref % level);
	      node.slots[slotnum] = subtree_hash;
	      node.set_slot_state(slotnum, subtree_state);      
	    }
	  break;

	case empty_state:
	  L(F("placing leaf %s in previously empty slot 0x%x of %s node %s, level %d\n")
	    % hleaf % slotnum % type % hpref % level);
	  node.total_num_leaves++;
	  node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
	  node.slots[slotnum] = leaf;
	  break;

	case subtree_state:
	  {
	    L(F("placing leaf %s in previously empty slot 0x%x of %s node %s, level %d\n")
	      % hleaf % slotnum % type % hpref % level);
	    string subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
	    hexenc<id> hsub;
	    encode_hexenc(id(subtree_hash), hsub);
	    L(F("updating subtree setting to %s at slot 0x%x of node %s, level %d\n") 
	      % hsub % slotnum % hpref % level);
	    node.slots[slotnum] = subtree_hash;
	    node.set_slot_state(slotnum, subtree_state);
	  }
	  break;
	}
    }
  else
    {
      L(F("creating new %s node with prefix %s, level %d, holding %s at slot 0x%x\n")
	% type % hpref % level % hleaf % slotnum);
      node.level = level;
      node.prefix = pref;
      node.total_num_leaves = 1;
      node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
      node.slots[slotnum] = leaf;
    }
  return store_merkle_node(app, type, collection, node);
}

static void rebuild_merkle_trees(app_state & app,
				 utf8 const & collection)
{
  transaction_guard guard(app.db);

  P(F("rebuilding merkle trees for collection %s\n") % collection);
  app.db.erase_merkle_nodes("mcert", collection);
  app.db.erase_merkle_nodes("fcert", collection);
  app.db.erase_merkle_nodes("manifest", collection);
  app.db.erase_merkle_nodes("key", collection);

  // FIXME: do fcerts later 
  // ticker fcerts("fcerts");

  ticker mcerts("mcerts");
  ticker keys("keys");
  ticker manifests("manifests");

  set<manifest_id> manifest_ids;
  set<rsa_keypair_id> inserted_keys;

  {
    // get all matching branch names
    vector< manifest<cert> > certs;
    app.db.get_manifest_certs(branch_cert_name, certs);
    for (size_t i = 0; i < certs.size(); ++i)
      {
	cert_value name;
	decode_base64(idx(certs, i).inner().value, name);
	if (name().find(collection()) == 0)
	  {
	    manifest_ids.insert(manifest_id(idx(certs, i).inner().ident));
	  }
      }

    // insert the manifests into the merkle tree for manifests
    for (set<manifest_id>::const_iterator man = manifest_ids.begin();
	 man != manifest_ids.end(); ++man)
      {
	id raw_id;
	decode_hexenc(man->inner(), raw_id);
	insert_into_merkle_tree(app, true, "manifest", collection, raw_id(), 0);
	++manifests;
	app.db.get_manifest_certs(*man, certs);
	for (size_t i = 0; i < certs.size(); ++i)
	  {
	    hexenc<id> certhash;
	    cert_hash_code(idx(certs, i).inner(), certhash);
	    decode_hexenc(certhash, raw_id);
	    insert_into_merkle_tree(app, true, "mcert", collection, raw_id(), 0);
	    ++mcerts;
	    rsa_keypair_id const & k = idx(certs, i).inner().key;
	    if (inserted_keys.find(k) == inserted_keys.end())
	      {
		if (app.db.public_key_exists(k))
		  {
		    base64<rsa_pub_key> pub_encoded;
		    app.db.get_key(k, pub_encoded);
		    hexenc<id> keyhash;
		    key_hash_code(k, pub_encoded, keyhash);
		    decode_hexenc(keyhash, raw_id);
		    insert_into_merkle_tree(app, true, "key", collection, raw_id(), 0);
		    ++keys;
		  }
		inserted_keys.insert(k);
	      }
	  }
      }
  }  
  guard.commit();
}
			
static void ensure_merkle_tree_ready(app_state & app,
				     utf8 const & collection)
{
  if (! (app.db.merkle_node_exists("mcert", collection, 0, hexenc<prefix>())
	 // FIXME: support fcerts, later
	 // && app.db.merkle_node_exists("fcert", collection, 0, hexenc<prefix>())
	 && app.db.merkle_node_exists("manifest", collection, 0, hexenc<prefix>())
	 && app.db.merkle_node_exists("key", collection, 0, hexenc<prefix>())))
    {
      rebuild_merkle_trees(app, collection);
    }
}

void run_netsync_protocol(protocol_voice voice, 
			  protocol_role role, 
			  utf8 const & addr, 
			  vector<utf8> collections,
			  app_state & app)
{
  port_type default_port = 5253;
  unsigned long connection_limit = 100;
  unsigned long timeout_seconds = 10;
  
  for (vector<utf8>::const_iterator i = collections.begin();
       i != collections.end(); ++i)
    ensure_merkle_tree_ready(app, *i);


  if (voice == server_voice)
    {
      netsync_protocol::serve_connections(role, collections, app,
					  addr, default_port, 
					  timeout_seconds, connection_limit);
    }
  else    
    {
      I(voice == client_voice);
      netsync_protocol::call_server(role, collections, app, 
				    addr, default_port, timeout_seconds);
    }
}

#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
#include "transforms.hh"
#include <boost/lexical_cast.hpp>

void test_netsync_io_functions()
{
  
  typedef netsync_protocol proto;

  try 
    {

      // bye_cmd
      {
	L(F("checking i/o round trip on bye_cmd\n"));
	proto::command out_cmd, in_cmd;
	string buf;
	out_cmd.cmd_code = bye_cmd;
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	BOOST_CHECK(in_cmd == out_cmd);
	L(F("bye_cmd test done, buffer was %d bytes\n") % buf.size());
      }
      
      // hello_cmd
      {
	L(F("checking i/o round trip on hello_cmd\n"));
	proto::command out_cmd, in_cmd;
	string buf;
	string out_server(raw_sha1("happy server day")), out_nonce(raw_sha1("nonce it up")), in_server, in_nonce;
	out_cmd.cmd_code = hello_cmd;
	proto::write_hello_cmd_payload(out_server, out_nonce, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_hello_cmd_payload(in_cmd.payload, in_server, in_nonce);
	BOOST_CHECK(in_cmd == out_cmd);
	BOOST_CHECK(in_server == out_server);
	BOOST_CHECK(in_nonce == out_nonce);
	L(F("hello_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // auth_cmd
      {
	L(F("checking i/o round trip on auth_cmd\n"));
	proto::command out_cmd, in_cmd;
	protocol_role out_role = source_and_sink_role, in_role;
	string buf;
	string out_client(raw_sha1("happy client day")), out_nonce1(raw_sha1("nonce me amadeus")), 
	  out_nonce2(raw_sha1("nonce start my heart")), out_collection("radishes galore!"), 
	  out_signature(raw_sha1("burble") + raw_sha1("gorby")),
	  in_client, in_nonce1, in_nonce2, in_collection, in_signature;

	out_cmd.cmd_code = auth_cmd;
	proto::write_auth_cmd_payload(out_role, out_collection, out_client, out_nonce1, 
				      out_nonce2, out_signature, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_auth_cmd_payload(in_cmd.payload, in_role, in_collection, in_client,
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
	proto::command out_cmd, in_cmd;
	string buf;
	string out_signature(raw_sha1("egg") + raw_sha1("tomago")), in_signature;

	out_cmd.cmd_code = confirm_cmd;
	proto::write_confirm_cmd_payload(out_signature, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_confirm_cmd_payload(in_cmd.payload, in_signature);
	BOOST_CHECK(in_cmd == out_cmd);
	BOOST_CHECK(in_signature == out_signature);
	L(F("confirm_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // refine_cmd
      {
	L(F("checking i/o round trip on refine_cmd\n"));
	proto::command out_cmd, in_cmd;
	string buf;
	proto::merkle_node out_node, in_node;

	out_node.slots[0] = raw_sha1("The police pulled Kris Kringle over");
	out_node.slots[3] = raw_sha1("Kris Kringle tried to escape from the police");
	out_node.slots[8] = raw_sha1("He was arrested for auto theft");
	out_node.slots[15] = raw_sha1("He was whisked away to jail");
	out_node.set_slot_state(0, subtree_state);
	out_node.set_slot_state(3, live_leaf_state);
	out_node.set_slot_state(8, dead_leaf_state);
	out_node.set_slot_state(15, subtree_state);

	out_cmd.cmd_code = refine_cmd;
	proto::write_refine_cmd_payload(out_node, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_refine_cmd_payload(in_cmd.payload, in_node);
	BOOST_CHECK(in_cmd == out_cmd);
	BOOST_CHECK(in_node == out_node);
	L(F("refine_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // done_cmd
      {
	L(F("checking i/o round trip on done_cmd\n"));
	proto::command out_cmd, in_cmd;
	u8 out_level(12), in_level;
	string buf;

	out_cmd.cmd_code = done_cmd;
	proto::write_done_cmd_payload(out_level, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_done_cmd_payload(in_cmd.payload, in_level);
	BOOST_CHECK(in_level == out_level);
	L(F("done_cmd test done, buffer was %d bytes\n") % buf.size());	
      }

      // describe_cmd
      {
	L(F("checking i/o round trip on describe_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_id(raw_sha1("pickles are yummy")), in_id;
	string buf;

	out_cmd.cmd_code = describe_cmd;
	proto::write_describe_cmd_payload(out_id, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_describe_cmd_payload(in_cmd.payload, in_id);
	BOOST_CHECK(in_id == out_id);
	L(F("describe_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // description_cmd
      {
	L(F("checking i/o round trip on description_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_id(raw_sha1("tuna is not yummy")), in_id;
	u64 out_len(8273423), in_len;
	vector<string> out_preds, in_preds;
	string buf;

	out_preds.push_back(raw_sha1("question"));
	out_preds.push_back(raw_sha1("what is ankh?"));
	out_preds.push_back(raw_sha1("*ding*"));
	out_preds.push_back(raw_sha1("that is the name"));
	out_preds.push_back(raw_sha1("of the item"));

	out_cmd.cmd_code = description_cmd;
	proto::write_description_cmd_payload(out_id, out_len, out_preds, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_description_cmd_payload(in_cmd.payload, in_id, in_len, in_preds);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_len == out_len);
	BOOST_CHECK(in_preds == out_preds);
	L(F("description_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // send_data_cmd
      {
	L(F("checking i/o round trip on send_data_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_id(raw_sha1("avocado is the yummiest")), in_id;
	vector< pair<u64,u64> > out_frags, in_frags;
	string buf;

	out_frags.push_back(make_pair(123,456));
	out_frags.push_back(make_pair(0xffff,0xfeedf00d));
	out_frags.push_back(make_pair(1,1));
	out_frags.push_back(make_pair(0,0xffffffffffffffffULL));

	out_cmd.cmd_code = send_data_cmd;
	proto::write_send_data_cmd_payload(out_id, out_frags, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_send_data_cmd_payload(in_cmd.payload, in_id, in_frags);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_frags == out_frags);
	L(F("send_data_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // send_delta_cmd
      {
	L(F("checking i/o round trip on send_delta_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_head(raw_sha1("when you board an airplane")), in_head;
	string out_base(raw_sha1("always check the exit locations")), in_base;
	string buf;

	out_cmd.cmd_code = send_delta_cmd;
	proto::write_send_delta_cmd_payload(out_head, out_base, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_send_delta_cmd_payload(in_cmd.payload, in_head, in_base);
	BOOST_CHECK(in_head == out_head);
	BOOST_CHECK(in_base == out_base);
	L(F("send_delta_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // data_cmd
      {
	L(F("checking i/o round trip on data_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_id(raw_sha1("tuna is not yummy")), in_id;
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
	proto::write_data_cmd_payload(out_id, out_frags, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_data_cmd_payload(in_cmd.payload, in_id, in_frags);
	BOOST_CHECK(in_id == out_id);
	BOOST_CHECK(in_frags == out_frags);
	L(F("data_cmd test done, buffer was %d bytes\n") % buf.size());
      }

      // delta_cmd
      {
	L(F("checking i/o round trip on delta_cmd\n"));
	proto::command out_cmd, in_cmd;
	string out_head(raw_sha1("your seat cusion can be reused")), in_head;
	string out_base(raw_sha1("as a floatation device")), in_base;
	u64 out_src_len(0xffff), in_src_len;
	string out_delta("goodness, this is not an xdelta"), in_delta;
	string buf;

	out_cmd.cmd_code = delta_cmd;
	proto::write_delta_cmd_payload(out_head, out_base, out_src_len, out_delta, out_cmd.payload);
	proto::write_command(out_cmd, buf);
	proto::read_command(buf, in_cmd);
	proto::read_delta_cmd_payload(in_cmd.payload, in_head, in_base, in_src_len, in_delta);
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

void add_netsync_tests(test_suite * suite)
{
  suite->add(BOOST_TEST_CASE(&test_netsync_io_functions));
}

#endif // BUILD_UNIT_TESTS
