// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include <time.h>

#include <boost/dynamic_bitset.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "keys.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "netsync.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

#include "cryptopp/osrng.h"

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

using namespace Netxx;
using namespace boost;
using namespace std;

typedef enum 
  { 
    authentication_phase,
    refinement_phase,
    transmission_phase 
  } 
protocol_phase;

static inline void require(bool check, string const & context)
{
  if (!check) 
    throw bad_decode(F("check of '%s' failed") % context);
}

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

  protocol_phase phase;
  utf8 collection;
  string remote_peer_key_hash;
  bool authenticated;

  time_t last_io_time;
  netcmd_code previous_cmd;
  string saved_nonce;
  boost::scoped_ptr<CryptoPP::AutoSeededRandomPool> prng;

  session(protocol_role role,
	  protocol_voice voice,
	  vector<utf8> const & collections,
	  app_state & app,
	  string const & peer,
	  socket_type sock, 
	  Timeout const & to);

  string mk_nonce();
  void mark_recent_io();
  Probe::ready_type which_events() const;
  bool read_some();
  bool write_some();

  void queue_bye_cmd();
  void queue_done_cmd(u8 level);
  void queue_hello_cmd(string const & server, 
		       string const & nonce);
  void queue_auth_cmd(protocol_role role, 
		      string const & collection, 
		      string const & client, 
		      string const & nonce1, 
		      string const & nonce2, 
		      string const & signature);
  void queue_confirm_cmd(string const & signature);
  void queue_refine_cmd(merkle_node const & node);
  void queue_describe_cmd(string const & head);
  void queue_description_cmd(string const & head, 
			     u64 len, 
			     vector<string> const & predecessors);
  void queue_send_data_cmd(string const & head, 
			   vector<pair<u64, u64> > const & fragments);
  void queue_send_delta_cmd(string const & head, 
			    string const & base);
  void queue_data_cmd(string const & id, 
		      vector< pair<pair<u64,u64>,string> > const & fragments);
  void queue_delta_cmd(string const & src, 
		       string const & dst, 
		       u64 src_len, 
		       string const & del);

  bool process_bye_cmd();
  bool process_done_cmd(u8 level);
  bool process_hello_cmd(string const & server, 
			 string const & nonce);
  bool process_auth_cmd(protocol_role role, 
			string const & collection, 
			string const & client, 
			string const & nonce1, 
			string const & nonce2, 
			string const & signature);
  bool process_confirm_cmd(string const & signature);
  bool process_refine_cmd(merkle_node const & node);
  bool process_describe_cmd(string const & head);
  bool process_description_cmd(string const & head, 
			       u64 len, 
			       vector<string> const & predecessors);
  bool process_send_data_cmd(string const & head, 
			     vector<pair<u64, u64> > const & fragments);
  bool process_send_delta_cmd(string const & head, 
			      string const & base);
  bool process_data_cmd(string const & id, 
			vector< pair<pair<u64,u64>,string> > const & fragments);
  bool process_delta_cmd(string const & src, 
			 string const & dst, 
			 u64 src_len, 
			 string const & del);

  
  bool dispatch_payload(netcmd const & cmd);
  void begin_service();
  bool process();
};


  
session::session(protocol_role role,
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
  last_io_time(::time(NULL)),
  previous_cmd(bye_cmd)
{
  if (voice == client_voice)
    {
      N(collections.size() == 1,
	"client can only sync one collection at a time");
      this->collection = idx(collections, 0);
    }
  
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
  prng.reset(new CryptoPP::AutoSeededRandomPool(request_blocking_rng));
}

string session::mk_nonce()
{
  I(this->saved_nonce.size() == 0);
  char buf[constants::merkle_hash_length_in_bytes];
  prng->GenerateBlock(reinterpret_cast<byte *>(buf), constants::merkle_hash_length_in_bytes);
  this->saved_nonce = string(buf);
  I(this->saved_nonce.size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void session::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

Probe::ready_type session::which_events() const
{    
  if (outbuf.empty())
    {
      if (inbuf.size() < constants::netcmd_maxsz)
	return Probe::ready_read | Probe::ready_oobd;
      else
	return Probe::ready_oobd;
    }
  else
    {
      if (inbuf.size() < constants::netcmd_maxsz)
	return Probe::ready_write | Probe::ready_read | Probe::ready_oobd;
      else
	return Probe::ready_write | Probe::ready_oobd;
    }	    
}

bool session::read_some()
{
  I(inbuf.size() < constants::netcmd_maxsz);
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

bool session::write_some()
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

void session::queue_bye_cmd() 
{
  netcmd cmd;
  cmd.cmd_code = bye_cmd;
  write_netcmd(cmd, outbuf);
}

void session::queue_done_cmd(u8 level) 
{
  netcmd cmd;
  cmd.cmd_code = done_cmd;
  write_done_cmd_payload(level, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_hello_cmd(string const & server, 
			      string const & nonce) 
{
  netcmd cmd;
  cmd.cmd_code = hello_cmd;
  write_hello_cmd_payload(server, nonce, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_auth_cmd(protocol_role role, 
			     string const & collection, 
			     string const & client, 
			     string const & nonce1, 
			     string const & nonce2, 
			     string const & signature)
{
  netcmd cmd;
  cmd.cmd_code = auth_cmd;
  write_auth_cmd_payload(role, collection, client, 
			 nonce1, nonce2, signature, 
			 cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_confirm_cmd(string const & signature)
{
  netcmd cmd;
  cmd.cmd_code = confirm_cmd;
  write_confirm_cmd_payload(signature, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_refine_cmd(merkle_node const & node)
{
  netcmd cmd;
  cmd.cmd_code = refine_cmd;
  write_refine_cmd_payload(node, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_describe_cmd(string const & head)
{
  netcmd cmd;
  cmd.cmd_code = describe_cmd;
  write_describe_cmd_payload(head, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_description_cmd(string const & head, 
				    u64 len, 
				    vector<string> const & predecessors)
{
  netcmd cmd;
  cmd.cmd_code = description_cmd;
  write_description_cmd_payload(head, len, predecessors, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_send_data_cmd(string const & head, 
				  vector<pair<u64, u64> > const & fragments)
{
  netcmd cmd;
  cmd.cmd_code = send_data_cmd;
  write_send_data_cmd_payload(head, fragments, cmd.payload);
  write_netcmd(cmd, outbuf);
}
    
void session::queue_send_delta_cmd(string const & head, 
				   string const & base)
{
  netcmd cmd;
  cmd.cmd_code = send_delta_cmd;
  write_send_delta_cmd_payload(head, base, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_data_cmd(string const & id, 
			     vector< pair<pair<u64,u64>,string> > const & fragments)
{
  netcmd cmd;
  cmd.cmd_code = data_cmd;
  write_data_cmd_payload(id, fragments, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_delta_cmd(string const & src, 
			      string const & dst, 
			      u64 src_len, 
			      string const & del)
{
  netcmd cmd;
  cmd.cmd_code = delta_cmd;
  write_delta_cmd_payload(src, dst, src_len, del, cmd.payload);
  write_netcmd(cmd, outbuf);
}

// processors

bool session::process_bye_cmd() 
{
  L(F("received 'bye' netcmd, shutting down\n"));
  return false;
}

bool session::process_done_cmd(u8 level) 
{      
  if (previous_cmd == level || level >= 0xff)
    {
      L(F("received 'done' for level %d, which is the last level; "
	  "changing to transmission phase\n") % level);
      this->phase = transmission_phase;
    }
  else 
    {
      L(F("received 'done' level %d, replying with 'done' %d\n") % level % (level + 1));
      queue_done_cmd(level + 1);
    }
  return true;
}

bool session::process_hello_cmd(string const & server, 
				string const & nonce) 
{
  I(this->remote_peer_key_hash.size() == 0);
  I(this->saved_nonce.size() == 0);
  
  hexenc<id> hnonce;
  encode_hexenc(id(nonce), hnonce);
  hexenc<id> their_key_hash;
  encode_hexenc(id(server), their_key_hash);
  
  L(F("received 'hello' netcmd from server '%s' with nonce '%s'\n") 
    % their_key_hash % hnonce);
  
  if (app.db.public_key_exists(their_key_hash))
    {
      // save their identity 
      this->remote_peer_key_hash = server;
      
      // get our public key for its hash identifier
      base64<rsa_pub_key> our_pub;
      hexenc<id> our_key_hash;
      id our_key_hash_raw;
      app.db.get_key(app.signing_key, our_pub);
      key_hash_code(app.signing_key, our_pub, our_key_hash);
      decode_hexenc(our_key_hash, our_key_hash_raw);
      
      // get our private key and make a signature
      base64<rsa_sha1_signature> sig;
      rsa_sha1_signature sig_raw;
      base64< arc4<rsa_priv_key> > our_priv;
      app.db.get_key(app.signing_key, our_priv);
      make_signature(app.lua, app.signing_key,
		     our_priv, nonce, sig);
      decode_base64(sig, sig_raw);
      
      // make a new nonce of our own and send off the 'auth'
      queue_auth_cmd(this->role, this->collection(), our_key_hash_raw(), 
		     nonce, mk_nonce(), sig_raw());
      return true;
    }
  else
    {
      L(F("unknown server key, disconnecting\n"));
    }
  return false;
}

bool session::process_auth_cmd(protocol_role role, 
			       string const & collection, 
			       string const & client, 
			       string const & nonce1, 
			       string const & nonce2, 
			       string const & signature)
{
  I(this->remote_peer_key_hash.size() == 0);
  I(this->saved_nonce.size() == constants::merkle_hash_length_in_bytes);
  
  hexenc<id> hnonce1, hnonce2;
  encode_hexenc(id(nonce1), hnonce1);
  encode_hexenc(id(nonce2), hnonce2);
  hexenc<id> their_key_hash;
  encode_hexenc(id(client), their_key_hash);
  
  L(F("received 'auth' netcmd from client '%s' for collection '%s' "
      "in %s mode with nonce1 '%s' and nonce2 '%s'\n")
    % their_key_hash % collection % (role == source_and_sink_role ? "source and sink" :
				     (role == source_role ? "source " : "sink"))
    % hnonce1 % hnonce2);
  
  // check that they replied with the nonce we asked for
  if (nonce1 != this->saved_nonce)
    {
      L(F("detected replay attack in auth netcmd, disconnecting\n"));
      this->saved_nonce.clear();
      return false;
    }
  
  // check they're asking for a collection we're serving
  bool collection_ok = false;
  for (vector<utf8>::const_iterator i = collections.begin(); 
       i != collections.end(); ++i)
    {
      if (*i == collection)
	{
	  collection_ok = true;
	  break;
	}
    }
  if (!collection_ok)
    {
      L(F("not currently serving requested collection '%s', disconnecting\n") % collection);
      this->saved_nonce.clear();
      return false;	  
    }
  
  // nb: the "role" here is the role the *client* wants to play
  if (role == sink_role || role == source_and_sink_role)
    {
      if (! ((this->role == source_role || this->role == source_and_sink_role)
	     && app.lua.hook_get_netsync_read_permitted(collection, 
							their_key_hash())))
	{
	  L(F("read permission on '%s' denied by lua hook, disconnecting\n") % collection);
	  this->saved_nonce.clear();
	  return false;
	}
    }
  
  if (role == source_role || role == source_and_sink_role)
    {
      if (! ((this->role == sink_role || this->role == source_and_sink_role)
	     && app.lua.hook_get_netsync_write_permitted(collection, 
							 their_key_hash())))
	{
	  L(F("write permission on '%s' denied by lua hook, disconnecting\n") % collection);
	  this->saved_nonce.clear();
	  return false;
	}
    }
  
  // check their signature
  if (app.db.public_key_exists(their_key_hash))
    {
      // save their identity 
      this->remote_peer_key_hash = client;
      
      // get their public key and check the signature
      rsa_keypair_id their_id;
      base64<rsa_pub_key> their_key;
      app.db.get_pubkey(their_key_hash, their_id, their_key);
      base64<rsa_sha1_signature> sig;
      encode_base64(rsa_sha1_signature(signature), sig);
      if (check_signature(app.lua, their_id, their_key, nonce1, sig))
	{
	  // get our private key and sign back
	  L(F("client signature OK, transitioning to refinement phase\n"));
	  base64<rsa_sha1_signature> sig;
	  rsa_sha1_signature sig_raw;
	  base64< arc4<rsa_priv_key> > our_priv;
	  app.db.get_key(app.signing_key, our_priv);
	  make_signature(app.lua, app.signing_key,
			 our_priv, nonce2, sig);
	  decode_base64(sig, sig_raw);
	  queue_confirm_cmd(sig_raw());
	  this->phase = refinement_phase;
	  return true;
	}
      else
	{
	  L(F("bad client signature, disconnecting\n"));	      
	}
    }
  else
    {
      L(F("unknown client key, disconnecting\n"));
    }
  return false;
}

bool session::process_confirm_cmd(string const & signature)
{
  I(this->remote_peer_key_hash.size() == constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce.size() == constants::merkle_hash_length_in_bytes);
  
  hexenc<id> their_key_hash;
  encode_hexenc(id(remote_peer_key_hash), their_key_hash);
  
  // nb. this->role is our role, the server is in the opposite role
  L(F("received 'confirm' netcmd from server '%s' for collection '%s' in %s mode\n")
    % their_key_hash % this->collection % (this->role == source_and_sink_role ? "source and sink" :
					   (this->role == source_role ? "sink" : "source")));
  
  // check their signature
  if (app.db.public_key_exists(their_key_hash))
    {
      // get their public key and check the signature
      rsa_keypair_id their_id;
      base64<rsa_pub_key> their_key;
      app.db.get_pubkey(their_key_hash, their_id, their_key);
      base64<rsa_sha1_signature> sig;
      encode_base64(rsa_sha1_signature(signature), sig);
      if (check_signature(app.lua, their_id, their_key, this->saved_nonce, sig))
	{
	  L(F("server signature OK, transitioning to refinement phase\n"));
	  this->phase = refinement_phase;
	  merkle_node root;
	  load_merkle_node(app, "manifest", this->collection, 
			   0, hexenc<prefix>(), root);
	  queue_refine_cmd(root);
	  return true;
	}
      else
	{
	  L(F("bad server signature, disconnecting\n"));	      
	}
    }
  else
    {
      L(F("unknown server key, disconnecting\n"));
    }
  return false;
}

bool session::process_refine_cmd(merkle_node const & node)
{
  return false;
}

bool session::process_describe_cmd(string const & head)
{
  return true;
}

bool session::process_description_cmd(string const & head, 
				      u64 len, 
				      vector<string> const & predecessors)
{
  return true;
}

bool session::process_send_data_cmd(string const & head, 
				    vector<pair<u64, u64> > const & fragments)
{
  return true;
}

bool session::process_send_delta_cmd(string const & head, 
				     string const & base)
{
  return true;
}

bool session::process_data_cmd(string const & id, 
			       vector< pair<pair<u64,u64>,string> > const & fragments)
{
  return true;
}

bool session::process_delta_cmd(string const & src, 
				string const & dst, 
				u64 src_len, 
				string const & del)
{
  return true;
}



bool session::dispatch_payload(netcmd const & cmd)
{
  
  switch (cmd.cmd_code)
    {
      
    case bye_cmd:
      return process_bye_cmd();
      break;
      
    case hello_cmd:
      require(! authenticated, "hello netcmd received when not authenticated");
      require(voice == client_voice, "hello netcmd received in client voice");
      require(phase == authentication_phase, "hello netcmd received in auth phase");
      {
	string server, nonce;
	read_hello_cmd_payload(cmd.payload, server, nonce);
	return process_hello_cmd(server, nonce);
      }
      break;

    case auth_cmd:
      require(! authenticated, "auth netcmd received when not authenticated");
      require(voice == server_voice, "auth netcmd received in server voice");
      require(phase == authentication_phase, "auth netcmd received in auth phase");
      {
	protocol_role role;
	string collection, client, nonce1, nonce2, signature;
	read_auth_cmd_payload(cmd.payload, role, collection, client, nonce1, nonce2, signature);
	return process_auth_cmd(role, collection, client, nonce1, nonce2, signature);
      }
      break;

    case confirm_cmd:
      require(! authenticated, "confirm netcmd received when not authenticated");
      require(voice == client_voice, "confirm netcmd received in client voice");
      require(phase == authentication_phase, "confirm netcmd received in auth phase");
      {
	string signature;
	read_confirm_cmd_payload(cmd.payload, signature);
	return process_confirm_cmd(signature);
      }
      break;

    case refine_cmd:
      require(authenticated, "refine netcmd received when authenticated");
      require(phase == refinement_phase, "refine netcmd received in refinement phase");
      {
	merkle_node node;
	read_refine_cmd_payload(cmd.payload, node);
	return process_refine_cmd(node);
      }
      break;

    case done_cmd:
      require(authenticated, "done netcmd received when authenticated");
      require(phase == refinement_phase, "done netcmd received in refinement phase");
      {
	u8 level;
	read_done_cmd_payload(cmd.payload, level);
	return process_done_cmd(level);
      }
      break;

    case describe_cmd:
      require(authenticated, "describe netcmd received when authenticated");
      require(phase == refinement_phase, "describe netcmd received in refinement phase");
      require(role == source_role ||
	      role == source_and_sink_role, 
	      "describe netcmd received in source or source/sink role");
      {
	string id;
	read_describe_cmd_payload(cmd.payload, id);
	return process_describe_cmd(id);
      }
      break;

    case description_cmd:
      require(authenticated, "description netcmd received when authenticated");
      require(phase == refinement_phase, "description netcmd received in refinement phase");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "description netcmd received in sink or source/sink role");
      {
	string head;
	u64 len;
	vector<string> predecessors;
	read_description_cmd_payload(cmd.payload, head, len, predecessors);
	return process_description_cmd(head, len, predecessors);
      }
      break;

    case send_data_cmd:
      require(authenticated, "send_data netcmd received when authenticated");
      require(phase == transmission_phase, "send_data netcmd received in transmission phase");
      require(role == source_role ||
	      role == source_and_sink_role, 
	      "send_data netcmd received in source or source/sink role");
      {
	string head;
	vector<pair<u64, u64> > fragments;
	read_send_data_cmd_payload(cmd.payload, head, fragments);
	return process_send_data_cmd(head, fragments);
      }
      break;

    case send_delta_cmd:
      require(authenticated, "send_delta netcmd received when authenticated");
      require(phase == transmission_phase, "send_delta netcmd received in transmission phase");
      require(role == source_role ||
	      role == source_and_sink_role, 
	      "send_delta netcmd received in source or source/sink role");
      {
	string head, base;
	read_send_delta_cmd_payload(cmd.payload, head, base);
	return process_send_delta_cmd(head, base);
      }

    case data_cmd:
      require(authenticated, "data netcmd received when authenticated");
      require(phase == transmission_phase, "data netcmd received in transmission phase");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "data netcmd received in source or source/sink role");
      {
	string id;
	vector< pair<pair<u64,u64>,string> > fragments;
	read_data_cmd_payload(cmd.payload, id, fragments);
	return process_data_cmd(id, fragments);
      }
      break;

    case delta_cmd:
      require(authenticated, "delta netcmd received when authenticated");
      require(phase == transmission_phase, "delta netcmd received in transmission phase");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "delta netcmd received in source or source/sink role");
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

// this kicks off the whole cascade starting from "hello"
void session::begin_service()
{
  base64<rsa_pub_key> pub_encoded;
  app.db.get_key(app.signing_key, pub_encoded);
  hexenc<id> keyhash;
  id keyhash_raw;
  key_hash_code(app.signing_key, pub_encoded, keyhash);
  decode_hexenc(keyhash, keyhash_raw);
  queue_hello_cmd(keyhash_raw(), mk_nonce());
}
      
bool session::process()
{
  try 
    {
      netcmd cmd;
      L(F("processing %d byte input buffer from peer %s\n") % inbuf.size() % peer_id);
      if (read_netcmd(inbuf, cmd))
	{
	  inbuf.erase(0, cmd.encoded_size());
	  bool continue_processing = dispatch_payload(cmd);
	  if (continue_processing)
	    previous_cmd = cmd.cmd_code;
	  return continue_processing;
	}	  
      if (inbuf.size() >= constants::netcmd_maxsz)
	{
	  W(F("input buffer for peer %s is overfull after netcmd dispatch\n") % peer_id);
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


/////////////////////////////////////////////////
//
// layer 4: monotone interface layer
//
/////////////////////////////////////////////////

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
  for (vector<utf8>::const_iterator i = collections.begin();
       i != collections.end(); ++i)
    ensure_merkle_tree_ready(app, *i);


  if (voice == server_voice)
    {
      serve_connections(role, collections, app,
			addr, static_cast<port_type>(constants::netsync_default_port), 
			static_cast<unsigned long>(constants::netsync_timeout_seconds), 
			static_cast<unsigned long>(constants::netsync_connection_limit));
    }
  else    
    {
      I(voice == client_voice);
      call_server(role, collections, app, 
		  addr, static_cast<port_type>(constants::netsync_default_port), 
		  static_cast<unsigned long>(constants::netsync_timeout_seconds));
    }
}

