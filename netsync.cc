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
#include "patch_set.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "xdelta.hh"

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
// stored as a size_t and sent as a uleb128.
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
//       1 byte        - type of this node (manifest, file, key, mcert, fcert)
//     1-N bytes       - level of this node in the tree (0 == "root", uleb128)
//    0-20 bytes       - the prefix of this node, 4 bits * level, 
//                       rounded up to a byte
//     1-N bytes       - number of leaves under this node (uleb128)
//       4 bytes       - slot-state bitmap of the node
//   0-320 bytes       - between 0 and 16 live slots in the node
//
// so, in the worst case such a node is 367 bytes, with these parameters.
//
//
// protocol
// --------
//
// the protocol is a simple binary command-packet system over tcp; each
// packet consists of a byte which identifies the protocol version, a byte
// which identifies the command name inside that version, a size_t sent as
// a uleb128 indicating the length of the packet, and then that many bytes
// of payload, and finally 4 bytes of adler32 checksum (in LSB order) over
// the payload. decoding involves simply buffering until a sufficient
// number of bytes are received, then advancing the buffer pointer. any
// time an adler32 check fails, the protocol is assumed to have lost
// synchronization, and the connection is dropped. the parties are free to
// drop the tcp stream at any point, if too much data is received or too
// much idle time passes; no commitments or transactions are made.
//
// one special command, "bye", is used to shut down a connection
// gracefully.  once each side has received all the data they want, they
// can send a "bye" command to the other side. as soon as either side has
// both sent and received a "bye" command, they drop the connection. if
// either side sees an i/o failure (dropped connection) after they have
// sent a "bye" command, they consider the shutdown successful.
//
// the exchange begins in a non-authenticated state. the server sends a
// "hello <id> <nonce>" command, which identifies the server's RSA key and
// issues a nonce which must be used for a subsequent authentication.
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
// transitions the peers into an authenticated state and begins refinement.
//
// refinement begins with the client sending its root public key and
// manifest certificate merkle nodes to the server. the server then
// compares the root to each slot in *its* root node, and for each slot
// either sends refined subtrees to the client, or (if it detects a missing
// item in one collection or the other) sends either "data" or "send_data"
// commands corresponding to the role of the missing item (source or
// sink). the client then receives each refined subtree and compares it
// with its own, performing similar description/request behavior depending
// on role, and the cycle continues.
//
// detecting the end of refinement is subtle: after sending the refinement
// of the root node, the server sends a "done 0" command (queued behind all
// the other refinement traffic). when either peer receives a "done N"
// command it immediately responds with a "done N+1" command. when two done
// commands for a given merkle tree arrive with no interveining refinements,
// the entire merkle tree is considered complete.
//
// any "send_data" command received prompts a "data" command in response,
// if the requested item exists. if an item does not exist, a "nonexistant"
// response command is sent. 
//
// once a response is received for each requested key and manifest cert
// (either data or nonexistant) the requesting party walks the graph of
// received manifest certs and transmits send_data or send_delta commands
// for all the manifests mentionned in the certs which it does not already
// have in its database.
//
// for each manifest edge it receives, the recipient builds a patch_set 
// out of the manifests and then requests all the file data or deltas
// described in that patch_set.
//
// once all requested files, manifests and certs are received (or noted as
// nonexistant), the recipient closes its connection.
//
// (aside: this protocol is raw binary because coding density is actually
// important here, and each packet consists of very information-dense
// material that you wouldn't have a hope of typing in manually anyways)
//

using namespace Netxx;
using namespace boost;
using namespace std;

static inline void require(bool check, string const & context)
{
  if (!check) 
    throw bad_decode(F("check of '%s' failed") % context);
}

struct done_marker
{
  bool current_level_had_refinements;
  bool tree_is_done;
  done_marker() : 
    current_level_had_refinements(false), 
    tree_is_done(false) 
  {}
};

struct session
{
  protocol_role const role;
  protocol_voice const voice;
  vector<utf8> const & collections;
  set<string> const & all_collections;
  app_state & app;

  string peer_id;
  socket_type fd;
  Stream stream;  

  string inbuf; 
  string outbuf;

  netcmd cmd;
  bool armed;
  bool arm();

  utf8 collection;
  id remote_peer_key_hash;
  bool authenticated;

  time_t last_io_time;

  map<netcmd_item_type, done_marker> done_refinements;
  set< pair<netcmd_item_type, id> > requested_items;
  multimap<id,id> ancestry_edges;

  id saved_nonce;
  bool received_goodbye;
  bool sent_goodbye;
  boost::scoped_ptr<CryptoPP::AutoSeededRandomPool> prng;

  session(protocol_role role,
	  protocol_voice voice,
	  vector<utf8> const & collections,
	  set<string> const & all_collections,
	  app_state & app,
	  string const & peer,
	  socket_type sock, 
	  Timeout const & to);

  id mk_nonce();
  void mark_recent_io();
  bool done_all_refinements();
  bool got_all_data();
  void maybe_say_goodbye();
  void analyze_ancestry_graph();
  void analyze_manifest(manifest_map const & man);
  void analyze_manifest_edge(manifest_map const & parent,
			     manifest_map const & child);
  void request_manifests_recursive(id const & i, set<id> & visited);

  Probe::ready_type which_events() const;
  bool read_some(ticker * t = NULL);
  bool write_some(ticker * t = NULL);
  void update_merkle_trees(netcmd_item_type type,
			   hexenc<id> const & hident,
			   bool live_p);

  void queue_bye_cmd();
  void queue_done_cmd(size_t level, netcmd_item_type type);
  void queue_hello_cmd(id const & server, 
		       id const & nonce);
  void queue_auth_cmd(protocol_role role, 
		      string const & collection, 
		      id const & client, 
		      id const & nonce1, 
		      id const & nonce2, 
		      string const & signature);
  void queue_confirm_cmd(string const & signature);
  void queue_refine_cmd(merkle_node const & node);
  void queue_send_data_cmd(netcmd_item_type type, 
			   id const & item);
  void queue_send_delta_cmd(netcmd_item_type type, 
			    id const & base, 
			    id const & ident);
  void queue_data_cmd(netcmd_item_type type, 
		      id const & item,
		      string const & dat);
  void queue_delta_cmd(netcmd_item_type type, 
		       id const & base, 
		       id const & ident, 
		       delta const & del);
  void queue_nonexistant_cmd(netcmd_item_type type, 
			     id const & item);

  bool process_bye_cmd();
  bool process_done_cmd(size_t level, netcmd_item_type type);
  bool process_hello_cmd(id const & server, 
			 id const & nonce);
  bool process_auth_cmd(protocol_role role, 
			string const & collection, 
			id const & client, 
			id const & nonce1, 
			id const & nonce2, 
			string const & signature);
  bool process_confirm_cmd(string const & signature);
  bool process_refine_cmd(merkle_node const & node);
  bool process_send_data_cmd(netcmd_item_type type,
			     id const & item);
  bool process_send_delta_cmd(netcmd_item_type type,
			      id const & base, 
			      id const & ident);
  bool process_data_cmd(netcmd_item_type type,
			id const & item, 
			string const & dat);
  bool process_delta_cmd(netcmd_item_type type,
			 id const & base, 
			 id const & ident, 
			 delta const & del);
  bool process_nonexistant_cmd(netcmd_item_type type,
			       id const & item);

  
  bool dispatch_payload(netcmd const & cmd);
  void begin_service();
  bool process();
};

static inline string tohex(string const & s)
{
  return lowercase(xform<CryptoPP::HexEncoder>(s));
}


struct root_prefix
{
  hexenc<prefix> val;
  root_prefix()
    {
      encode_hexenc(prefix(""), val);
    }
};
static root_prefix ROOT_PREFIX;

  
session::session(protocol_role role,
		 protocol_voice voice,
		 vector<utf8> const & collections,
		 set<string> const & all_coll,
		 app_state & app,
		 string const & peer,
		 socket_type sock, 
		 Timeout const & to) : 
  role(role),
  voice(voice),
  collections(collections),
  all_collections(all_coll),
  app(app),
  peer_id(peer),
  fd(sock),
  stream(sock, to),
  inbuf(""),
  outbuf(""),
  armed(false),
  collection(""),
  remote_peer_key_hash(""),
  authenticated(false),
  last_io_time(::time(NULL)),
  saved_nonce(""),
  received_goodbye(false),
  sent_goodbye(false)
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

  done_refinements.insert(make_pair(mcert_item, done_marker()));
  done_refinements.insert(make_pair(fcert_item, done_marker()));
  done_refinements.insert(make_pair(key_item, done_marker()));
}

id session::mk_nonce()
{
  I(this->saved_nonce().size() == 0);
  char buf[constants::merkle_hash_length_in_bytes];
  prng->GenerateBlock(reinterpret_cast<byte *>(buf), constants::merkle_hash_length_in_bytes);
  this->saved_nonce = string(buf, buf + constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void session::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

bool session::done_all_refinements()
{
  bool all = true;
  for(map< netcmd_item_type, done_marker>::const_iterator j = done_refinements.begin();
      j != done_refinements.end(); ++j)
    {
      if (j->second.tree_is_done == false)
	all = false;
    }
  return all;
}

bool session::got_all_data()
{
  return requested_items.empty();
}

struct always_true_version_check :
  public version_existence_check
{
  virtual bool check(file_id i)
  {
    return true;
  }
};

void session::analyze_manifest_edge(manifest_map const & parent,
				    manifest_map const & child)
{
  L(F("analyzing %d parent, %d child manifest entries\n") 
    % parent.size() % child.size());

  always_true_version_check atc;
  patch_set ps;
  manifests_to_patch_set(parent, child, this->app, atc, ps);

  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    {
      if (this->app.db.file_version_exists(i->id_new))
	{
	  L(F("file delta target '%s' already exists on our side\n"));
	}
      else
	{
	  L(F("requesting file delta from '%s' -> '%s' (path %s)\n") 
	    % i->id_old % i->id_new % i->path);
	  id tmp1, tmp2;
	  decode_hexenc(i->id_old.inner(), tmp1);
	  decode_hexenc(i->id_new.inner(), tmp2);
	  queue_send_delta_cmd(file_item, tmp1, tmp2);
	}
    }

  for (set<patch_addition>::const_iterator i = ps.f_adds.begin();
       i != ps.f_adds.end(); ++i)
    {      
      if (this->app.db.file_version_exists(i->ident))
	{
	  L(F("added file version '%s' already exists on our side\n"));
	}
      else
	{
	  L(F("requesting missing data for file '%s' (path %s)\n") 
	    % i->ident % i->path);
	  id tmp;
	  decode_hexenc(i->ident.inner(), tmp);
	  queue_send_data_cmd(file_item, tmp);
	}
    }
}

void session::analyze_manifest(manifest_map const & man)
{
  L(F("analyzing %d entries in manifest\n") % man.size());
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    {
      path_id_pair pip(i);
      if (! this->app.db.file_version_exists(pip.ident()))
	{
	  id tmp;
	  decode_hexenc(pip.ident().inner(), tmp);
	  queue_send_data_cmd(file_item, tmp);
	}
    }
}

void session::request_manifests_recursive(id const & i, set<id> & visited)
{
  if (visited.find(i) != visited.end())
    return;

  visited.insert(i);

  hexenc<id> hid;
  encode_hexenc(i, hid);

  L(F("visiting manifest '%s'\n") % hid);

  typedef multimap<id,id>::const_iterator ite;

  if (ancestry_edges.find(i) == ancestry_edges.end())
    {
      // we are at a root, request full data
      if (this->app.db.manifest_version_exists(manifest_id(hid)))
	{
	  L(F("not requesting manifest '%s' as we already have it\n") % hid);
	}
      else
	{
	  queue_send_data_cmd(manifest_item, i);
	}
    }
  else
    {
      // first make sure we've requested enough to get to here by
      // calling ourselves recursively
      pair<ite,ite> range = ancestry_edges.equal_range(i);
      for (ite p = range.first; p != range.second; ++p)
	{
	  id const & child = p->first;
	  id const & parent = p->second;
	  I(i == child);
	  request_manifests_recursive(parent, visited);
	}

      // then perhaps request the edge that leads from an arbitrary parent
      // to here. we'll pick the first parent, why not?
      id const & child = range.first->first;
      id const & parent = range.first->second;
      I(i == child);
      if (this->app.db.manifest_version_exists(manifest_id(hid)))
	{
	  L(F("not requesting manifest delta to '%s' as we already have it\n") % hid);
	}
      else
	{
	  queue_send_delta_cmd(manifest_item, parent, child);
	}      
    }
}

void session::analyze_ancestry_graph()
{
  set<id> heads;

  L(F("analyzing %d ancestry edges\n") % ancestry_edges.size());

  // each ancestry edge goes from child -> parent

  for (multimap<id,id>::const_iterator i = ancestry_edges.begin();
       i != ancestry_edges.end(); ++i)
    {
      // first we add all children we're aware of to the heads set
      heads.insert(i->first);
    }

  for (multimap<id,id>::const_iterator i = ancestry_edges.begin();
       i != ancestry_edges.end(); ++i)
    {
      // then we remove any which are also parents
      heads.erase(i->second);
    }

  L(F("isolated %d heads\n") % heads.size());

  // then we walk the graph upwards, recursively, starting from
  // each of the heads

  set<id> visited;
  for (set<id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    {
      hexenc<id> hid;
      encode_hexenc(*i, hid);
      L(F("walking upwards from '%s'\n") % hid);
      request_manifests_recursive(*i, visited);
    }
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

bool session::read_some(ticker * tick)
{
  I(inbuf.size() < constants::netcmd_maxsz);
  char tmp[constants::bufsz];
  signed_size_type count = stream.read(tmp, sizeof(tmp));
  if(count > 0)
    {
      L(F("read %d bytes from fd %d (peer %s)\n") % count % fd % peer_id);
      inbuf.append(string(tmp, tmp + count));
      mark_recent_io();
      if (tick != NULL)
	(*tick) += count;
      return true;
    }
  else
    return false;
}

bool session::write_some(ticker * tick)
{
  I(!outbuf.empty());    
  signed_size_type count = stream.write(outbuf.data(), 
					std::min(outbuf.size(), constants::bufsz));
  if(count > 0)
    {
      outbuf.erase(0, count);
      L(F("wrote %d bytes to fd %d (peer %s), %d remain in output buffer\n") 
	% count % fd % peer_id % outbuf.size());
      mark_recent_io();
      if (tick != NULL)
	(*tick) += count;
      return true;
    }
  else
    return false;
}

// senders

void session::queue_bye_cmd() 
{
  L(F("queueing 'bye' command\n"));
  netcmd cmd;
  cmd.cmd_code = bye_cmd;
  write_netcmd(cmd, outbuf);
  this->sent_goodbye = true;
}

void session::queue_done_cmd(size_t level, netcmd_item_type type) 
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing 'done' command for %s level %s\n") % typestr % level);
  netcmd cmd;
  cmd.cmd_code = done_cmd;
  write_done_cmd_payload(level, type, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_hello_cmd(id const & server, 
			      id const & nonce) 
{
  netcmd cmd;
  cmd.cmd_code = hello_cmd;
  write_hello_cmd_payload(server, nonce, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_auth_cmd(protocol_role role, 
			     string const & collection, 
			     id const & client, 
			     id const & nonce1, 
			     id const & nonce2, 
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
  string typestr;
  hexenc<prefix> hpref;
  node.get_hex_prefix(hpref);
  netcmd_item_type_to_string(node.type, typestr);
  L(F("queueing request for refinement of %s node '%s', level %d\n")
    % typestr % hpref % static_cast<int>(node.level));
  netcmd cmd;
  cmd.cmd_code = refine_cmd;
  write_refine_cmd_payload(node, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_send_data_cmd(netcmd_item_type type,
				  id const & item)
{

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  if (this->requested_items.find(make_pair(type, item)) != 
      this->requested_items.end())
    {
      hexenc<id> hid;
      encode_hexenc(item, hid);
      L(F("not queueing request for %s '%s' as we already requested it\n") 
	% typestr % hid);
      return;
    }

  L(F("queueing request for data of %s item '%s'\n")
    % typestr % tohex(item()));
  netcmd cmd;
  cmd.cmd_code = send_data_cmd;
  write_send_data_cmd_payload(type, item, cmd.payload);
  write_netcmd(cmd, outbuf);
  this->requested_items.insert(make_pair(type, item));
}
    
void session::queue_send_delta_cmd(netcmd_item_type type,
				   id const & base, 
				   id const & ident)
{

  string typestr;
  netcmd_item_type_to_string(type, typestr);
  I(type == manifest_item || type == file_item);

  if (this->requested_items.find(make_pair(type, ident)) != 
      this->requested_items.end())
    {
      hexenc<id> base_hid;
      encode_hexenc(base, base_hid);
      hexenc<id> ident_hid;
      encode_hexenc(ident, ident_hid);
      L(F("not queueing request for %s delta '%s' -> '%s' as we already requested the target\n") 
	% typestr % base_hid % ident_hid);
      return;
    }

  L(F("queueing request for contents of %s delta '%s' -> '%s'\n")
    % typestr % tohex(base()) % tohex(ident()));
  netcmd cmd;
  cmd.cmd_code = send_delta_cmd;
  write_send_delta_cmd_payload(type, base, ident, cmd.payload);
  write_netcmd(cmd, outbuf);
  this->requested_items.insert(make_pair(type, ident));
}

void session::queue_data_cmd(netcmd_item_type type,
			     id const & item, 
			     string const & dat)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing %d bytes of data for %s item '%s'\n")
    % dat.size() % typestr % tohex(item()));
  netcmd cmd;
  cmd.cmd_code = data_cmd;
  write_data_cmd_payload(type, item, dat, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_delta_cmd(netcmd_item_type type,
			      id const & base, 
			      id const & ident, 
			      delta const & del)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing %s delta '%s' -> '%s'\n")
    % typestr % tohex(base()) % tohex(ident()));
  netcmd cmd;
  cmd.cmd_code = delta_cmd;
  write_delta_cmd_payload(type, base, ident, del, cmd.payload);
  write_netcmd(cmd, outbuf);
}

void session::queue_nonexistant_cmd(netcmd_item_type type,
				    id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing note of nonexistance of %s item '%s'\n")
    % typestr % tohex(item()));
  netcmd cmd;
  cmd.cmd_code = nonexistant_cmd;
  write_nonexistant_cmd_payload(type, item, cmd.payload);
  write_netcmd(cmd, outbuf);
}

// processors

bool session::process_bye_cmd() 
{
  L(F("received 'bye' netcmd\n"));
  this->received_goodbye = true;
  return true;
}

bool session::process_done_cmd(size_t level, netcmd_item_type type) 
{

  map< netcmd_item_type, done_marker>::iterator i = done_refinements.find(type);
  I(i != done_refinements.end());

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  if ((! i->second.current_level_had_refinements) || (level >= 0xff))
    {
      // we received *no* refinements on this level -- or we ran out of
      // levels -- so refinement for this type is finished.
      L(F("received 'done' for empty %s level %d, marking as complete\n") 
	% typestr % static_cast<int>(level));

      // possibly echo it back one last time, for shutdown purposes
      if (!i->second.tree_is_done)
	queue_done_cmd(level + 1, type);

      // tombstone it
      i->second.current_level_had_refinements = false;
      i->second.tree_is_done = true;
      
      // if it's mcerts, look over the ancestry graph
      if (type == mcert_item)
	analyze_ancestry_graph();
    }

  else if (i->second.current_level_had_refinements 
      && (! i->second.tree_is_done))
    {
      // we *did* receive some refinements on this level, reset to zero and
      // queue an echo of the 'done' marker.
      L(F("received 'done' for %s level %d, which had refinements; "
	  "sending echo of done for level %d\n") 
	% typestr 
	% static_cast<int>(level) 
	% static_cast<int>(level + 1));
      i->second.current_level_had_refinements = false;
      queue_done_cmd(level + 1, type);
      return true;
    }
  return true;
}

bool session::process_hello_cmd(id const & server, 
				id const & nonce) 
{
  I(this->remote_peer_key_hash().size() == 0);
  I(this->saved_nonce().size() == 0);
  
  hexenc<id> hnonce;
  encode_hexenc(nonce, hnonce);
  hexenc<id> their_key_hash;
  encode_hexenc(server, their_key_hash);
  
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
      make_signature(app.lua, app.signing_key, our_priv, nonce(), sig);
      decode_base64(sig, sig_raw);
      
      // make a new nonce of our own and send off the 'auth'
      queue_auth_cmd(this->role, this->collection(), our_key_hash_raw, 
		     nonce, mk_nonce(), sig_raw());
      return true;
    }
  else
    {
      W(F("unknown server key\n"));
    }
  return false;
}

bool session::process_auth_cmd(protocol_role role, 
			       string const & collection, 
			       id const & client, 
			       id const & nonce1, 
			       id const & nonce2, 
			       string const & signature)
{
  I(this->remote_peer_key_hash().size() == 0);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  
  hexenc<id> hnonce1, hnonce2;
  encode_hexenc(nonce1, hnonce1);
  encode_hexenc(nonce2, hnonce2);
  hexenc<id> their_key_hash;
  encode_hexenc(client, their_key_hash);
  
  L(F("received 'auth' netcmd from client '%s' for collection '%s' "
      "in %s mode with nonce1 '%s' and nonce2 '%s'\n")
    % their_key_hash % collection % (role == source_and_sink_role ? "source and sink" :
				     (role == source_role ? "source " : "sink"))
    % hnonce1 % hnonce2);
  
  // check that they replied with the nonce we asked for
  if (!(nonce1 == this->saved_nonce))
    {
      W(F("detected replay attack in auth netcmd\n"));
      this->saved_nonce = id("");
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
      W(F("not currently serving requested collection '%s'\n") % collection);
      this->saved_nonce = id("");
      return false;	  
    }

  //
  // internally netsync thinks in terms of sources and sinks. users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // we therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: the "role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.
  //

  if (role == sink_role || role == source_and_sink_role)
    {
      if (! ((this->role == source_role || this->role == source_and_sink_role)
	     && app.lua.hook_get_netsync_read_permitted(collection, 
							their_key_hash())))
	{
	  W(F("read permission denied for '%s'\n") % collection);
	  this->saved_nonce = id("");
	  return false;
	}
    }
  
  if (role == source_role || role == source_and_sink_role)
    {
      if (! ((this->role == sink_role || this->role == source_and_sink_role)
	     && app.lua.hook_get_netsync_write_permitted(collection, 
							 their_key_hash())))
	{
	  W(F("write permission denied for '%s'\n") % collection);
	  this->saved_nonce = id("");
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
      if (check_signature(app.lua, their_id, their_key, nonce1(), sig))
	{
	  // get our private key and sign back
	  L(F("client signature OK, accepting authentication\n"));
	  base64<rsa_sha1_signature> sig;
	  rsa_sha1_signature sig_raw;
	  base64< arc4<rsa_priv_key> > our_priv;
	  app.db.get_key(app.signing_key, our_priv);
	  make_signature(app.lua, app.signing_key, our_priv, nonce2(), sig);
	  decode_base64(sig, sig_raw);
	  queue_confirm_cmd(sig_raw());
	  this->collection = collection;
	  this->authenticated = true;
	  return true;
	}
      else
	{
	  W(F("bad client signature\n"));	      
	}
    }
  else
    {
      L(F("unknown client key\n"));
    }
  return false;
}

bool session::process_confirm_cmd(string const & signature)
{
  I(this->remote_peer_key_hash().size() == constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  
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
      if (check_signature(app.lua, their_id, their_key, this->saved_nonce(), sig))
	{
	  L(F("server signature OK, accepting authentication\n"));
	  this->authenticated = true;
	  merkle_node root;
	  load_merkle_node(app, key_item, this->collection, 0, ROOT_PREFIX.val, root);
	  queue_refine_cmd(root);
	  queue_done_cmd(0, key_item);

	  load_merkle_node(app, mcert_item, this->collection, 0, ROOT_PREFIX.val, root);
	  queue_refine_cmd(root);
	  queue_done_cmd(0, mcert_item);

	  load_merkle_node(app, fcert_item, this->collection, 0, ROOT_PREFIX.val, root);
	  queue_refine_cmd(root);
	  queue_done_cmd(0, fcert_item);
	  return true;
	}
      else
	{
	  W(F("bad server signature\n"));	      
	}
    }
  else
    {
      W(F("unknown server key\n"));
    }
  return false;
}

static bool data_exists(netcmd_item_type type, 
			id const & item, 
			app_state & app)
{
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  switch (type)
    {
    case key_item:
      return app.db.public_key_exists(hitem);
    case fcert_item:
      return app.db.file_cert_exists(hitem);
    case mcert_item:
      return app.db.manifest_cert_exists(hitem);
    case manifest_item:
      return app.db.manifest_version_exists(manifest_id(hitem));
    case file_item:
      return app.db.file_version_exists(file_id(hitem));
    }
  return false;
}

static void load_data(netcmd_item_type type, 
		      id const & item, 
		      app_state & app, 
		      string & out)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  switch (type)
    {
    case key_item:
      if (app.db.public_key_exists(hitem))
	{
	  rsa_keypair_id keyid;
	  base64<rsa_pub_key> pub_encoded;
	  app.db.get_pubkey(hitem, keyid, pub_encoded);
	  L(F("public key '%s' is also called '%s'\n") % hitem % keyid);
	  write_pubkey(keyid, pub_encoded, out);
	}
      else
	{
	  throw bad_decode(F("public key '%s' does not exist in our database") % hitem);
	}
      break;

    case manifest_item:
      if (app.db.manifest_version_exists(manifest_id(hitem)))
	{
	  manifest_data mdat;
	  data dat;
	  app.db.get_manifest_version(manifest_id(hitem), mdat);
	  unpack(mdat.inner(), dat);
	  out = dat();
	}
      else
	{
	  throw bad_decode(F("manifest '%s' does not exist in our database") % hitem);
	}
      break;

    case file_item:
      if (app.db.file_version_exists(file_id(hitem)))
	{
	  file_data fdat;
	  data dat;
	  app.db.get_file_version(file_id(hitem), fdat);
	  unpack(fdat.inner(), dat);
	  out = dat();
	}
      else
	{
	  throw bad_decode(F("file '%s' does not exist in our database") % hitem);
	}
      break;

    case mcert_item:
      if(app.db.manifest_cert_exists(hitem))
	{
	  manifest<cert> c;
	  app.db.get_manifest_cert(hitem, c);
	  string tmp;
	  write_cert(c.inner(), out);
	}
      else
	{
	  throw bad_decode(F("mcert '%s' does not exist in our database") % hitem);
	}
      break;

    case fcert_item:
      if(app.db.file_cert_exists(hitem))
	{
	  file<cert> c;
	  app.db.get_file_cert(hitem, c);
	  string tmp;
	  write_cert(c.inner(), out);
	}
      else
	{
	  throw bad_decode(F("fcert '%s' does not exist in our database") % hitem);
	}
      break;
    }
}


bool session::process_refine_cmd(merkle_node const & their_node)
{
  hexenc<prefix> hpref;
  their_node.get_hex_prefix(hpref);
  string typestr;

  netcmd_item_type_to_string(their_node.type, typestr);
  size_t lev = static_cast<size_t>(their_node.level);
  
  L(F("received 'refine' netcmd on %s node '%s', level %d\n") 
    % typestr % hpref % lev);
  
  if (!app.db.merkle_node_exists(typestr, this->collection, 
				 their_node.level, hpref))
    {
      L(F("no corresponding %s merkle node for prefix '%s', level %d\n")
	% typestr % hpref % lev);

      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
	{
	  switch (their_node.get_slot_state(slot))
	    {
	    case empty_state:
	      {
		// we agree, this slot is empty
		L(F("(#0) they have an empty slot %d (in a %s node '%s', level %d, we do not have)\n")
		  % slot % typestr % hpref % lev);
		continue;
	      }
	      break;
	    case live_leaf_state:
	      {
		// we want what *they* have
		id slotval;
		hexenc<id> hslotval;
		their_node.get_raw_slot(slot, slotval);
		their_node.get_hex_slot(slot, hslotval);
		L(F("(#0) they have a live leaf at slot %d (in a %s node '%s', level %d, we do not have)\n")
		  % slot % typestr % hpref % lev);
		L(F("(#0) requesting their %s leaf %s\n") % typestr % hslotval);
		queue_send_data_cmd(their_node.type, slotval);
	      }
	      break;
	    case dead_leaf_state:
	      {
		// we cannot ask for what they have, it is dead
		L(F("(#0) they have a dead leaf at slot %d (in a %s node '%s', level %d, we do not have)\n")
		  % slot % typestr % hpref % lev);
		continue;
	      }
	      break;
	    case subtree_state:
	      {
		// they have a subtree; might as well ask for that
		L(F("(#0) they have a subtree at slot %d (in a %s node '%s', level %d, we do not have)\n")
		  % slot % typestr % hpref % lev);
		merkle_node our_fake_subtree;
		their_node.extended_prefix(slot, our_fake_subtree.pref);
		our_fake_subtree.level = their_node.level + 1;
		our_fake_subtree.type = their_node.type;
		queue_refine_cmd(our_fake_subtree);
	      }
	      break;
	    }
	}
    }
  else
    {
      // we have a corresponding merkle node. there are 16 branches
      // to the following switch condition. it is awful. sorry.
      L(F("found corresponding %s merkle node for prefix '%s', level %d\n")
	% typestr % hpref % lev);
      merkle_node our_node;
      load_merkle_node(app, their_node.type, this->collection, 
		       their_node.level, hpref, our_node);
      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
	{	  
	  switch (their_node.get_slot_state(slot))
	    {
	    case empty_state:
	      switch (our_node.get_slot_state(slot))
		{

		case empty_state:
		  // 1: theirs == empty, ours == empty 
		  L(F("(#1) they have an empty slot %d in %s node '%s', level %d, and so do we\n")
		    % slot % typestr % hpref % lev);
		  continue;
		  break;

		case live_leaf_state:
		  // 2: theirs == empty, ours == live 
		  L(F("(#2) they have an empty slot %d in %s node '%s', level %d, we have a live leaf\n")
		    % slot % typestr % hpref % lev);
		  {
		    I(their_node.type == our_node.type);
		    string tmp;
		    id slotval;
		    our_node.get_raw_slot(slot, slotval);
		    load_data(their_node.type, slotval, this->app, tmp);
		    queue_data_cmd(their_node.type, slotval, tmp);
		  }
		  break;

		case dead_leaf_state:
		  // 3: theirs == empty, ours == dead 
		  L(F("(#3) they have an empty slot %d in %s node '%s', level %d, we have a dead leaf\n")
		    % slot % typestr % hpref % lev);
		  continue;
		  break;

		case subtree_state:
		  // 4: theirs == empty, ours == subtree 
		  L(F("(#4) they have an empty slot %d in %s node '%s', level %d, we have a subtree\n")
		    % slot % typestr % hpref % lev);
		  {
		    hexenc<prefix> subprefix;
		    our_node.extended_hex_prefix(slot, subprefix);
		    merkle_node our_subtree;
		    I(our_node.type == their_node.type);
		    load_merkle_node(app, their_node.type, this->collection, 
				     our_node.level + 1, subprefix, our_subtree);
		    I(our_node.type == our_subtree.type);
		    queue_refine_cmd(our_subtree);
		  }
		  break;

		}
	      break;


	    case live_leaf_state:
	      switch (our_node.get_slot_state(slot))
		{

		case empty_state:
		  // 5: theirs == live, ours == empty 
		  L(F("(#5) they have a live leaf at slot %d in %s node '%s', level %d, we have nothing\n")
		    % slot % typestr % hpref % lev);
		  {
		    id slotval;
		    their_node.get_raw_slot(slot, slotval);
		    queue_send_data_cmd(their_node.type, slotval);
		  }
		  break;

		case live_leaf_state:
		  // 6: theirs == live, ours == live 
		  L(F("(#6) they have a live leaf at slot %d in %s node '%s', and so do we\n")
		    % slot % typestr % hpref);
		  {
		    id our_slotval, their_slotval;
		    their_node.get_raw_slot(slot, their_slotval);
		    our_node.get_raw_slot(slot, our_slotval);		    
		    if (their_slotval == our_slotval)
		      {
			hexenc<id> hslotval;
			their_node.get_hex_slot(slot, hslotval);
			L(F("(#6) we both have live %s leaf '%s'\n") % typestr % hslotval);
			continue;
		      }
		    else
		      {
			I(their_node.type == our_node.type);
			string tmp;
			id our_slotval, their_slotval;
			our_node.get_raw_slot(slot, our_slotval);
			our_node.get_raw_slot(slot, their_slotval);			
			load_data(our_node.type, our_slotval, this->app, tmp);
			queue_send_data_cmd(their_node.type, their_slotval);
			queue_data_cmd(our_node.type, our_slotval, tmp);
		      }
		  }
		  break;

		case dead_leaf_state:
		  // 7: theirs == live, ours == dead 
		  L(F("(#7) they have a live leaf at slot %d in %s node %s, level %d, we have a dead one\n")
		    % slot % typestr % hpref % lev);
		  {
		    id our_slotval, their_slotval;
		    our_node.get_raw_slot(slot, our_slotval);
		    their_node.get_raw_slot(slot, their_slotval);
		    if (their_slotval == our_slotval)
		      {
			hexenc<id> hslotval;
			their_node.get_hex_slot(slot, hslotval);
			L(F("(#7) it's the same %s leaf '%s', but ours is dead\n") 
			  % typestr % hslotval);
			continue;
		      }
		    else
		      {
			queue_send_data_cmd(their_node.type, their_slotval);
		      }
		  }
		  break;

		case subtree_state:
		  // 8: theirs == live, ours == subtree 
		  L(F("(#8) they have a live leaf in slot %d of %s node '%s', level %d, we have a subtree\n")
		    % slot % typestr % hpref % lev);
		  {
		    hexenc<prefix> subprefix;
		    our_node.extended_hex_prefix(slot, subprefix);
		    merkle_node our_subtree;
		    load_merkle_node(app, our_node.type, this->collection, 
				     our_node.level + 1, subprefix, our_subtree);
		    queue_refine_cmd(our_subtree);
		  }
		  break;
		}
	      break;


	    case dead_leaf_state:
	      switch (our_node.get_slot_state(slot))
		{
		case empty_state:
		  // 9: theirs == dead, ours == empty 
		  L(F("(#9) they have a dead leaf at slot %d in %s node '%s', level %d, we have nothing\n")
		    % slot % typestr % hpref % lev);
		  continue;
		  break;

		case live_leaf_state:
		  // 10: theirs == dead, ours == live 
		  L(F("(#10) they have a dead leaf at slot %d in %s node '%s', level %d, we have a live one\n")
		    % slot % typestr % hpref % lev);
		  {
		    id our_slotval, their_slotval;
		    their_node.get_raw_slot(slot, their_slotval);
		    our_node.get_raw_slot(slot, our_slotval);
		    hexenc<id> hslotval;
		    our_node.get_hex_slot(slot, hslotval);
		    if (their_slotval == our_slotval)
		      {
			L(F("(#10) we both have %s leaf %s, theirs is dead\n") 
			  % typestr % hslotval);
			continue;
		      }
		    else
		      {
			I(their_node.type == our_node.type);
			string tmp;
			load_data(our_node.type, our_slotval, this->app, tmp);
			queue_data_cmd(our_node.type, our_slotval, tmp);
		      }
		  }
		  break;

		case dead_leaf_state:
		  // 11: theirs == dead, ours == dead 
		  L(F("(#11) they have a dead leaf at slot %d in %s node '%s', level %d, so do we\n")
		    % slot % typestr % hpref % lev);
		  continue;
		  break;

		case subtree_state:
		  // theirs == dead, ours == subtree 
		  L(F("(#12) they have a dead leaf in slot %d of %s node '%s', we have a subtree\n")
		    % slot % typestr % hpref % lev);
		  {
		    hexenc<prefix> subprefix;
		    our_node.extended_hex_prefix(slot, subprefix);
		    merkle_node our_subtree;
		    load_merkle_node(app, our_node.type, this->collection, 
				     our_node.level + 1, subprefix, our_subtree);
		    queue_refine_cmd(our_subtree);
		  }
		  break;
		}
	      break;


	    case subtree_state:
	      switch (our_node.get_slot_state(slot))
		{
		case empty_state:
		  // 13: theirs == subtree, ours == empty 
		  L(F("(#13) they have a subtree at slot %d in %s node '%s', level %d, we have nothing\n")
		    % slot % typestr % hpref % lev);
		  {
		    merkle_node our_fake_subtree;
		    their_node.extended_prefix(slot, our_fake_subtree.pref);
		    our_fake_subtree.level = their_node.level + 1;
		    our_fake_subtree.type = their_node.type;
		    queue_refine_cmd(our_fake_subtree);
		  }
		  break;

		case live_leaf_state:
		  // 14: theirs == subtree, ours == live 
		  L(F("(#14) they have a subtree at slot %d in %s node '%s', level %d, we have a live leaf\n")
		    % slot % typestr % hpref % lev);
		  {
		    size_t subslot;
		    id our_slotval;
		    merkle_node our_fake_subtree;
		    our_node.get_raw_slot(slot, our_slotval);
		    pick_slot_and_prefix_for_value(our_slotval, our_node.level + 1, subslot, 
						   our_fake_subtree.pref);
		    our_fake_subtree.type = their_node.type;
		    our_fake_subtree.level = our_node.level + 1;
		    our_fake_subtree.set_raw_slot(subslot, our_slotval);
		    our_fake_subtree.set_slot_state(subslot, our_node.get_slot_state(slot));
		    queue_refine_cmd(our_fake_subtree);
		  }
		  break;

		case dead_leaf_state:
		  // 15: theirs == subtree, ours == dead 
		  L(F("(#15) they have a subtree at slot %d in %s node '%s', level %d, we have a dead leaf\n")
		    % slot % typestr % hpref % lev);
		  {
		    size_t subslot;
		    id our_slotval;
		    merkle_node our_fake_subtree;
		    our_node.get_raw_slot(slot, our_slotval);
		    pick_slot_and_prefix_for_value(our_slotval, our_node.level + 1, subslot, 
						   our_fake_subtree.pref);
		    our_fake_subtree.type = their_node.type;
		    our_fake_subtree.level = our_node.level + 1;
		    our_fake_subtree.set_raw_slot(subslot, our_slotval);
		    our_fake_subtree.set_slot_state(subslot, our_node.get_slot_state(slot));
		    queue_refine_cmd(our_fake_subtree);    
		  }
		  break;

		case subtree_state:
		  // 16: theirs == subtree, ours == subtree 
		  L(F("(#16) they have a subtree at slot %d in %s node '%s', level %d, and so do we\n")
		    % slot % typestr % hpref % lev);
		  {
		    id our_slotval, their_slotval;
		    hexenc<id> hslotval;
		    their_node.get_raw_slot(slot, their_slotval);
		    our_node.get_raw_slot(slot, our_slotval);
		    our_node.get_hex_slot(slot, hslotval);
		    if (their_slotval == our_slotval)
		      {
			L(F("(#16) we both have %s subtree '%s'\n") % typestr % hslotval);
			continue;
		      }
		    else
		      {
			L(F("(#16) %s subtrees at slot %d differ, refining ours\n") % typestr % slot);
			hexenc<prefix> subprefix;
			our_node.extended_hex_prefix(slot, subprefix);
			merkle_node our_subtree;
			load_merkle_node(app, our_node.type, this->collection, 
					 our_node.level + 1, subprefix, our_subtree);
			queue_refine_cmd(our_subtree);
		      }
		  }
		  break;
		}
	      break;
	    }
	}
    }
  return true;
}


bool session::process_send_data_cmd(netcmd_item_type type,
				    id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  L(F("received 'send_data' netcmd requesting %s '%s'\n") 
    % typestr % hitem);
  if (data_exists(type, item, this->app))
    {
      string out;
      load_data(type, item, this->app, out);
      queue_data_cmd(type, item, out);
    }
  else
    {
      queue_nonexistant_cmd(type, item);
    }
  return true;
}

bool session::process_send_delta_cmd(netcmd_item_type type,
				     id const & base,
				     id const & ident)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  delta del;

  hexenc<id> hbase, hident;
  encode_hexenc(base, hbase);
  encode_hexenc(ident, hident);

  L(F("received 'send_delta' netcmd requesting %s edge '%s' -> '%s'\n") 
    % typestr % hbase % hident);

  switch (type)
    {
    case file_item:
      {
	file_id fbase(hbase), fident(hident);
	file_delta fdel;
	if (this->app.db.file_version_exists(fbase) 
	    && this->app.db.file_version_exists(fident))
	  {
	    file_data base_fdat, ident_fdat;
	    data base_dat, ident_dat;
	    this->app.db.get_file_version(fbase, base_fdat);
	    this->app.db.get_file_version(fident, ident_fdat);	    
	    string tmp;	    
	    unpack(base_fdat.inner(), base_dat);
	    unpack(ident_fdat.inner(), ident_dat);
	    compute_delta(base_dat(), ident_dat(), tmp);
	    del = delta(tmp);
	  }
	else
	  {
	    queue_nonexistant_cmd(type, ident);
	  }
      }
      break;

    case manifest_item:
      {
	manifest_id mbase(hbase), mident(hident);
	manifest_delta mdel;
	if (this->app.db.manifest_version_exists(mbase) 
	    && this->app.db.manifest_version_exists(mident))
	  {
	    manifest_data base_mdat, ident_mdat;
	    data base_dat, ident_dat;
	    this->app.db.get_manifest_version(mbase, base_mdat);
	    this->app.db.get_manifest_version(mident, ident_mdat);
	    string tmp;
	    unpack(base_mdat.inner(), base_dat);
	    unpack(ident_mdat.inner(), ident_dat);
	    compute_delta(base_dat(), ident_dat(), tmp);
	    del = delta(tmp);
	  }
	else
	  {
	    queue_nonexistant_cmd(type, ident);
	  }
      }
      break;
      
    default:
      throw bad_decode(F("delta requested for item type %s\n") % typestr);
    }
  queue_delta_cmd(type, base, ident, del);
  return true;
}

void session::update_merkle_trees(netcmd_item_type type,
				  hexenc<id> const & hident,
				  bool live_p)
{
  id raw_id;
  decode_hexenc(hident, raw_id);
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  for (set<string>::const_iterator i = this->all_collections.begin();
       i != this->all_collections.end(); ++i)
    {
      if (this->collection().find(*i) == 0)
	{
	  L(F("updating %s collection '%s' with item %s\n")
	    % typestr % *i % hident);
	  insert_into_merkle_tree(this->app, live_p, type, *i, raw_id(), 0); 
	}
    }
}

bool session::process_data_cmd(netcmd_item_type type,
			       id const & item, 
			       string const & dat)
{  
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  requested_items.erase(make_pair(type, item));
			   
  switch (type)
    {
    case key_item:
      if (this->app.db.public_key_exists(hitem))
	L(F("public key '%s' already exists in our database\n")  % hitem);
      else
	{
	  rsa_keypair_id keyid;
	  base64<rsa_pub_key> pub;
	  read_pubkey(dat, keyid, pub);
	  hexenc<id> tmp;
	  key_hash_code(keyid, pub, tmp);
	  if (! (tmp == hitem))
	    throw bad_decode(F("hash check failed for public key '%s' (%s);"
			       " wanted '%s' got '%s'")  
			     % hitem % keyid % hitem % tmp);
	  this->app.db.put_key(keyid, pub);
	  update_merkle_trees(key_item, tmp, true);
	}
      break;

    case mcert_item:
      if (this->app.db.manifest_cert_exists(hitem))
	L(F("manifest cert '%s' already exists in our database\n")  % hitem);
      else
	{
	  cert c;
	  read_cert(dat, c);
	  hexenc<id> tmp;
	  cert_hash_code(c, tmp);
	  if (! (tmp == hitem))
	    throw bad_decode(F("hash check failed for manifest cert '%s'")  % hitem);
	  this->app.db.put_manifest_cert(manifest<cert>(c));
	  update_merkle_trees(mcert_item, tmp, true);
	  if (c.name == ancestor_cert_name)
	    {
	      cert_value tmp_value;
	      hexenc<id> tmp_parent;
	      id child, parent;

	      decode_base64(c.value, tmp_value);
	      tmp_parent = tmp_value();
	      decode_hexenc(c.ident, child);
	      decode_hexenc(tmp_parent, parent);
	      L(F("noticed ancestry edge from '%s' -> '%s'\n") % tmp_parent % c.ident);
	      this->ancestry_edges.insert(make_pair(child, parent));
	    }
	}
      break;

    case fcert_item:
      if (this->app.db.file_cert_exists(hitem))
	L(F("file cert '%s' already exists in our database\n")  % hitem);
      else
	{
	  cert c;
	  read_cert(dat, c);
	  hexenc<id> tmp;
	  cert_hash_code(c, tmp);
	  if (! (tmp == hitem))
	    throw bad_decode(F("hash check failed for file cert '%s'")  % hitem);
	  this->app.db.put_file_cert(file<cert>(c));
	  update_merkle_trees(fcert_item, tmp, true);
	}
      break;

    case manifest_item:
      {
	manifest_id mid(hitem);
	if (this->app.db.manifest_version_exists(mid))
	  L(F("manifest version '%s' already exists in our database\n") % hitem);
	else
	  {
	    base64< gzip<data> > packed_dat;
	    pack(data(dat), packed_dat);
	    this->app.db.put_manifest(mid, manifest_data(packed_dat));
	    manifest_map man;
	    read_manifest_map(data(dat), man);
	    analyze_manifest(man);
	  }
      }
      break;

    case file_item:
      {
	file_id fid(hitem);
	if (this->app.db.file_version_exists(fid))
	  L(F("file version '%s' already exists in our database\n") % hitem);
	else
	  {
	    base64< gzip<data> > packed_dat;
	    pack(data(dat), packed_dat);
	    this->app.db.put_file(fid, file_data(packed_dat));
	  }
      }
      break;

    }
      return true;
}

bool session::process_delta_cmd(netcmd_item_type type,
				id const & base, 
				id const & ident, 
				delta const & del)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hbase, hident;
  encode_hexenc(base, hbase);
  encode_hexenc(ident, hident);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  requested_items.erase(make_pair(type, ident));

  switch (type)
    {
    case manifest_item:
      {
	manifest_id old_manifest(hbase), new_manifest(hident);
	if (! this->app.db.manifest_version_exists(old_manifest))
	  L(F("manifest delta base '%s' does not exist in our database\n") 
	    % hbase);
	else if (this->app.db.manifest_version_exists(new_manifest))
	  L(F("manifest delta head '%s' already exists in our database\n") 
	    % hident);
	else
	  {
	    manifest_data old_dat;
	    this->app.db.get_manifest_version(old_manifest, old_dat);
	    data old_unpacked;
	    unpack(old_dat.inner(), old_unpacked);
	    string tmp;
	    apply_delta(old_unpacked(), del(), tmp);
	    hexenc<id> confirm;
	    calculate_ident(data(tmp), confirm);
	    if (!(confirm == hident))
	      {
		L(F("reconstructed manifest from delta '%s' -> '%s' has wrong id '%s'\n") 
		  % hbase % hident % confirm);
	      }
	    else
	      {
		base64< gzip<delta> > packed_del;
		pack(del, packed_del);		
		this->app.db.put_manifest_version(old_manifest, new_manifest, 
						  manifest_delta(packed_del));
		manifest_map parent_man, child_man;
		read_manifest_map(old_unpacked, parent_man);
		read_manifest_map(data(tmp), child_man);
		analyze_manifest_edge(parent_man, child_man);
	      }					      
	  }
      }
      break;

    case file_item:
      {
	file_id old_file(hbase), new_file(hident);
	if (! this->app.db.file_version_exists(old_file))
	  L(F("file delta base '%s' does not exist in our database\n") 
	    % hbase);
	else if (this->app.db.file_version_exists(new_file))
	  L(F("file delta head '%s' already exists in our database\n") 
	    % hident);
	else
	  {
	    file_data old_dat;
	    this->app.db.get_file_version(old_file, old_dat);
	    data old_unpacked;
	    unpack(old_dat.inner(), old_unpacked);
	    string tmp;
	    apply_delta(old_unpacked(), del(), tmp);
	    hexenc<id> confirm;
	    calculate_ident(data(tmp), confirm);
	    if (!(confirm == hident))
	      {
		L(F("reconstructed file from delta '%s' -> '%s' has wrong id '%s'\n") 
		  % hbase % hident % confirm);
	      }
	    else
	      {
		base64< gzip<delta> > packed_del;
		pack(del, packed_del);		
		this->app.db.put_file_version(old_file, new_file, file_delta(packed_del));
	      }					      
	  }
      }
      break;
      
    default:
      L(F("ignoring delta received for item type %s\n") % typestr);
      break;
    }
  return true;
}

bool session::process_nonexistant_cmd(netcmd_item_type type,
				      id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  L(F("received 'nonexistant' netcmd for %s '%s'\n") 
    % typestr % hitem);
  requested_items.erase(make_pair(type, item));
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
      {
	id server, nonce;
	read_hello_cmd_payload(cmd.payload, server, nonce);
	return process_hello_cmd(server, nonce);
      }
      break;

    case auth_cmd:
      require(! authenticated, "auth netcmd received when not authenticated");
      require(voice == server_voice, "auth netcmd received in server voice");
      {
	protocol_role role;
	string collection, signature;
	id client, nonce1, nonce2;
	read_auth_cmd_payload(cmd.payload, role, collection, client, nonce1, nonce2, signature);
	return process_auth_cmd(role, collection, client, nonce1, nonce2, signature);
      }
      break;

    case confirm_cmd:
      require(! authenticated, "confirm netcmd received when not authenticated");
      require(voice == client_voice, "confirm netcmd received in client voice");
      {
	string signature;
	read_confirm_cmd_payload(cmd.payload, signature);
	return process_confirm_cmd(signature);
      }
      break;

    case refine_cmd:
      require(authenticated, "refine netcmd received when authenticated");
      {
	merkle_node node;
	read_refine_cmd_payload(cmd.payload, node);
	map< netcmd_item_type, done_marker>::iterator i = done_refinements.find(node.type);
	require(i != done_refinements.end(), "refinement netcmd refers to valid type");
	require(i->second.tree_is_done == false, "refinement netcmd received when tree is live");
	i->second.current_level_had_refinements = true;
	return process_refine_cmd(node);
      }
      break;

    case done_cmd:
      require(authenticated, "done netcmd received when authenticated");
      {
	size_t level;
	netcmd_item_type type;
	read_done_cmd_payload(cmd.payload, level, type);
	return process_done_cmd(level, type);
      }
      break;

    case send_data_cmd:
      require(authenticated, "send_data netcmd received when authenticated");
      require(role == source_role ||
	      role == source_and_sink_role, 
	      "send_data netcmd received in source or source/sink role");
      {
	netcmd_item_type type;
	id item;
	read_send_data_cmd_payload(cmd.payload, type, item);
	return process_send_data_cmd(type, item);
      }
      break;

    case send_delta_cmd:
      require(authenticated, "send_delta netcmd received when authenticated");
      require(role == source_role ||
	      role == source_and_sink_role, 
	      "send_delta netcmd received in source or source/sink role");
      {
	netcmd_item_type type;
	id base, ident;
	read_send_delta_cmd_payload(cmd.payload, type, base, ident);
	return process_send_delta_cmd(type, base, ident);
      }

    case data_cmd:
      require(authenticated, "data netcmd received when authenticated");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "data netcmd received in source or source/sink role");
      {
	netcmd_item_type type;
	id item;
	string dat;
	read_data_cmd_payload(cmd.payload, type, item, dat);
	return process_data_cmd(type, item, dat);
      }
      break;

    case delta_cmd:
      require(authenticated, "delta netcmd received when authenticated");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "delta netcmd received in source or source/sink role");
      {
	netcmd_item_type type;
	id base, ident;
	delta del;
	read_delta_cmd_payload(cmd.payload, type, base, ident, del);
	return process_delta_cmd(type, base, ident, del);
      }
      break;	  

    case nonexistant_cmd:
      require(authenticated, "nonexistant netcmd received when authenticated");
      require(role == sink_role ||
	      role == source_and_sink_role, 
	      "nonexistant netcmd received in sink or source/sink role");
      {
	netcmd_item_type type;
	id item;
	read_nonexistant_cmd_payload(cmd.payload, type, item);
	return process_nonexistant_cmd(type, item);
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

void session::maybe_say_goodbye()
{
  if (done_all_refinements() &&
      got_all_data())
    queue_bye_cmd();
}

bool session::arm()
{
  if (!armed)
    {
      if (read_netcmd(inbuf, cmd))
	{
	  inbuf.erase(0, cmd.encoded_size());	  
	  armed = true;
	}
    }
  return armed;
}      

bool session::process()
{
  if (!arm())
    return true;

  try 
    {
      transaction_guard guard(app.db);
      armed = false;
      L(F("processing %d byte input buffer from peer %s\n") % inbuf.size() % peer_id);
      bool ret = dispatch_payload(cmd);
      if (inbuf.size() >= constants::netcmd_maxsz)
	W(F("input buffer for peer %s is overfull after netcmd dispatch\n") % peer_id);
      guard.commit();
      maybe_say_goodbye();
      return ret;
    }
  catch (bad_decode & bd)
    {
      W(F("caught bad_decode exception processing peer %s: '%s'\n") % peer_id % bd.what);
      return false;
    }
}


static void call_server(protocol_role role,
			vector<utf8> const & collections,
			set<string> const & all_collections,
			app_state & app,
			utf8 const & address,
			port_type default_port,
			unsigned long timeout_seconds)
{
  Probe probe;
  Timeout timeout(static_cast<long>(timeout_seconds)), instant(0,1);

  // FIXME: split into labels and convert to ace here.

  P(F("connecting to %s\n") % address());
  Stream server(address().c_str(), default_port, timeout); 
  session sess(role, client_voice, collections, all_collections, app, 
	       address(), server.get_socketfd(), timeout);

  ticker input("bytes in"), output("bytes out");

  while (true)
    {       
      bool armed = sess.arm();

      probe.clear();
      probe.add(sess.stream, sess.which_events());
      Probe::result_type res = probe.ready(armed ? instant : timeout);
      Probe::ready_type event = res.second;
      socket_type fd = res.first;
      
      if (fd == -1 && !armed) 
	{
	  P(F("timed out waiting for I/O with peer %s, disconnecting\n") % sess.peer_id);
	  return;
	}
      
      if (event & Probe::ready_read)
	{
	  if (sess.read_some(&input))
	    {
	      armed = sess.arm();
	    }
	  else
	    {	      
	      P(F("read from fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
	      return;
	    }
	}
      
      if (event & Probe::ready_write)
	{
	  if (! sess.write_some(&output))
	    {
	      P(F("write on fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
	      return;
	    }
	}
      
      if (event & Probe::ready_oobd)
	{
	  P(F("got OOB data on fd %d (peer %s), disconnecting\n") 
	    % fd % sess.peer_id);
	  return;
	}      

      if (armed)
	{
	  if (!sess.process())
	    {
	      P(F("processing on fd %d (peer %s) finished, disconnecting\n") 
		% fd % sess.peer_id);
	      return;
	    }
	}

      if (sess.sent_goodbye && sess.outbuf.empty() && sess.received_goodbye)
	{
	  P(F("exchanged goodbyes and flushed output on fd %d (peer %s), disconnecting\n") 
	    % fd % sess.peer_id);
	  return;
	}	  
    }  
}

static void serve_connections(protocol_role role,
			      vector<utf8> const & collections,
			      set<string> const & all_collections,
			      app_state & app,
			      utf8 const & address,
			      port_type default_port,
			      unsigned long timeout_seconds,
			      unsigned long session_limit)
{
  Probe probe;  

  Timeout 
    forever, 
    timeout(static_cast<long>(timeout_seconds)), 
    instant(0,1);

  Address addr(address().c_str(), default_port, true);
  StreamServer server(addr, timeout);
  
  map<socket_type, shared_ptr<session> > sessions;
  set<socket_type> armed_sessions;

  P(F("beginning service on %s : %d\n") 
    % addr.get_name() % addr.get_port());
  
  while (true)
    {      
      probe.clear();
      armed_sessions.clear();

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
	{
	  if (i->second->arm())
	    {
	      L(F("fd %d is armed\n") % i->first);
	      armed_sessions.insert(i->first);
	    }
	  probe.add(i->second->stream, i->second->which_events());
	}

      L(F("i/o probe with %d armed\n") % armed_sessions.size());      
      Probe::result_type res = probe.ready(sessions.empty() ? forever 
					   : (armed_sessions.empty() ? timeout 
					      : instant));
      Probe::ready_type event = res.second;
      socket_type fd = res.first;
      
      if (fd == -1)
	{
	  if (armed_sessions.empty()) 
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
		
	      shared_ptr<session> sess(new session(role, server_voice, collections, 
						   all_collections, app,
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
		      if (sess->arm())
			armed_sessions.insert(fd);
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

      // now process any clients which are armed (have read a command)
      for (set<socket_type>::const_iterator i = armed_sessions.begin();
	   i != armed_sessions.end(); ++i)
	{
	  map<socket_type, shared_ptr<session> >::iterator j;
	  j = sessions.find(*i);
	  if (j == sessions.end())
	    continue;
	  else
	    {
	      shared_ptr<session> sess = j->second;
	      if (!sess->process())
		{
		  P(F("fd %d (peer %s) processing finished, disconnecting\n") 
		    % fd % sess->peer_id);
		  sessions.erase(j);
		}
	    }
	}
	

      // kill any clients which haven't done any i/o inside the timeout period
      // or who have said goodbye and flushed their output buffers
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
	  if (i->second->sent_goodbye && i->second->outbuf.empty() && i->second->received_goodbye)
	    {
	      P(F("fd %d (peer %s) exchanged goodbyes and flushed output, disconnecting\n") 
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

void rebuild_merkle_trees(app_state & app,
			  utf8 const & collection)
{
  transaction_guard guard(app.db);

  P(F("rebuilding merkle trees for collection %s\n") % collection);

  string typestr;
  merkle_node empty_root_node;

  empty_root_node.type = mcert_item;
  netcmd_item_type_to_string(mcert_item, typestr);
  app.db.erase_merkle_nodes(typestr, collection);
  store_merkle_node(app, collection, empty_root_node);

  empty_root_node.type = fcert_item;
  netcmd_item_type_to_string(fcert_item, typestr);
  app.db.erase_merkle_nodes(typestr, collection);
  store_merkle_node(app, collection, empty_root_node);

  empty_root_node.type = key_item;
  netcmd_item_type_to_string(key_item, typestr);
  app.db.erase_merkle_nodes(typestr, collection);
  store_merkle_node(app, collection, empty_root_node);

  // FIXME: do fcerts later 
  // ticker fcerts("fcerts");

  ticker mcerts("mcerts");
  ticker keys("keys");

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

    // insert all certs and keys reachable via these manifests
    for (set<manifest_id>::const_iterator man = manifest_ids.begin();
	 man != manifest_ids.end(); ++man)
      {
	app.db.get_manifest_certs(*man, certs);
	for (size_t i = 0; i < certs.size(); ++i)
	  {
	    hexenc<id> certhash;
	    id raw_id;
	    cert_hash_code(idx(certs, i).inner(), certhash);
	    decode_hexenc(certhash, raw_id);
	    insert_into_merkle_tree(app, true, mcert_item, collection, raw_id(), 0);
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
		    insert_into_merkle_tree(app, true, key_item, collection, raw_id(), 0);
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
  string mcert_item_str, fcert_item_str, key_item_str;
  netcmd_item_type_to_string(mcert_item, mcert_item_str);
  netcmd_item_type_to_string(mcert_item, fcert_item_str);
  netcmd_item_type_to_string(mcert_item, key_item_str);

  if (! (app.db.merkle_node_exists(mcert_item_str, collection, 0, ROOT_PREFIX.val)
	 && app.db.merkle_node_exists(fcert_item_str, collection, 0, ROOT_PREFIX.val)
	 && app.db.merkle_node_exists(key_item_str, collection, 0, ROOT_PREFIX.val)))
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

  set<string> all_collections;
  for (vector<utf8>::const_iterator j = collections.begin(); 
       j != collections.end(); ++j)
    {
      all_collections.insert((*j)());
    }

  vector< manifest<cert> > certs;
  app.db.get_manifest_certs(branch_cert_name, certs);
  for (vector< manifest<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value name;
      decode_base64(i->inner().value, name);
      for (vector<utf8>::const_iterator j = collections.begin(); 
	   j != collections.end(); ++j)
	{	
	  if ((*j)().find(name()) == 0 
	      && all_collections.find(name()) == all_collections.end())
	    {
	      if (name() != (*j)())
		P(F("%s included in collection %s\n") % (*j) % name);
	      all_collections.insert(name());
	    }
	}
    }


  if (voice == server_voice)
    {
      serve_connections(role, collections, all_collections, app,
			addr, static_cast<port_type>(constants::netsync_default_port), 
			static_cast<unsigned long>(constants::netsync_timeout_seconds), 
			static_cast<unsigned long>(constants::netsync_connection_limit));
    }
  else    
    {
      I(voice == client_voice);
      call_server(role, collections, all_collections, app, 
		  addr, static_cast<port_type>(constants::netsync_default_port), 
		  static_cast<unsigned long>(constants::netsync_timeout_seconds));
    }
}

