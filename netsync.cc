// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>
#include <memory>

#include <time.h>

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
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "xdelta.hh"

#include "cryptopp/osrng.h"

#include "netxx/address.h"
#include "netxx/peer.h"
#include "netxx/probe.h"
#include "netxx/socket.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"
#include "netxx/timeout.h"

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
// once a response is received for each requested key and revision cert
// (either data or nonexistant) the requesting party walks the graph of
// received revision certs and transmits send_data or send_delta commands
// for all the revisions mentionned in the certs which it does not already
// have in its database.
//
// for each revision it receives, the recipient requests all the file data or
// deltas described in that revision.
//
// once all requested files, manifests, revisions and certs are received (or
// noted as nonexistant), the recipient closes its connection.
//
// (aside: this protocol is raw binary because coding density is actually
// important here, and each packet consists of very information-dense
// material that you wouldn't have a hope of typing in manually anyways)
//

using namespace boost;
using namespace std;

static inline void 
require(bool check, string const & context)
{
  if (!check) 
    throw bad_decode(F("check of '%s' failed") % context);
}

struct 
done_marker
{
  bool current_level_had_refinements;
  bool tree_is_done;
  done_marker() : 
    current_level_had_refinements(false), 
    tree_is_done(false) 
  {}
};

struct 
session
{
  protocol_role role;
  protocol_voice const voice;
  vector<utf8> const & collections;
  set<string> const & all_collections;
  app_state & app;

  string peer_id;
  Netxx::socket_type fd;
  Netxx::Stream str;  

  string inbuf; 
  string outbuf;

  netcmd cmd;
  bool armed;
  bool arm();

  utf8 collection;
  id remote_peer_key_hash;
  bool authenticated;

  time_t last_io_time;
  auto_ptr<ticker> byte_in_ticker;
  auto_ptr<ticker> byte_out_ticker;
  auto_ptr<ticker> cert_in_ticker;
  auto_ptr<ticker> cert_out_ticker;
  auto_ptr<ticker> revision_in_ticker;
  auto_ptr<ticker> revision_out_ticker;

  map< std::pair<utf8, netcmd_item_type>, 
       boost::shared_ptr<merkle_table> > merkle_tables;

  map<netcmd_item_type, done_marker> done_refinements;
  map<netcmd_item_type, boost::shared_ptr< set<id> > > requested_items;
  map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestry;
  set< pair<id, id> > reverse_delta_requests;
  bool analyzed_ancestry;

  id saved_nonce;
  bool received_goodbye;
  bool sent_goodbye;
  boost::scoped_ptr<CryptoPP::AutoSeededRandomPool> prng;

  packet_db_writer dbw;

  session(protocol_role role,
          protocol_voice voice,
          vector<utf8> const & collections,
          set<string> const & all_collections,
          app_state & app,
          string const & peer,
          Netxx::socket_type sock, 
          Netxx::Timeout const & to);

  virtual ~session() {}

  id mk_nonce();
  void mark_recent_io();

  bool done_all_refinements();
  bool rcert_refinement_done();
  bool all_requested_revisions_received();

  void note_item_requested(netcmd_item_type ty, id const & i);
  bool item_request_outstanding(netcmd_item_type ty, id const & i);
  void note_item_arrived(netcmd_item_type ty, id const & i);

  void note_item_sent(netcmd_item_type ty, id const & i);

  bool got_all_data();
  void maybe_say_goodbye();

  void analyze_attachment(revision_id const & i, 
                          set<revision_id> & visited,
                          map<revision_id, bool> & attached);
  void request_rev_revisions(revision_id const & init, 
                             map<revision_id, bool> attached,
                             set<revision_id> visited);
  void request_fwd_revisions(revision_id const & i, 
                             map<revision_id, bool> attached,
                             set<revision_id> & visited);
  void analyze_ancestry_graph();
  void analyze_manifest(manifest_map const & man);

  Netxx::Probe::ready_type which_events() const;
  bool read_some();
  bool write_some();

  void write_netcmd_and_try_flush(netcmd const & cmd);
  void queue_bye_cmd();
  void queue_error_cmd(string const & errmsg);
  void queue_done_cmd(size_t level, netcmd_item_type type);
  void queue_hello_cmd(id const & server, 
                       id const & nonce);
  void queue_anonymous_cmd(protocol_role role, 
                           string const & collection, 
                           id const & nonce2);
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
  bool process_error_cmd(string const & errmsg);
  bool process_done_cmd(size_t level, netcmd_item_type type);
  bool process_hello_cmd(id const & server, 
                         id const & nonce);
  bool process_anonymous_cmd(protocol_role role, 
                             string const & collection, 
                             id const & nonce2);
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

  bool merkle_node_exists(netcmd_item_type type,
                          utf8 const & collection,			
                          size_t level,
                          prefix const & pref);

  void load_merkle_node(netcmd_item_type type,
                        utf8 const & collection,			
                        size_t level,
                        prefix const & pref,
                        merkle_ptr & node);

  void rebuild_merkle_trees(app_state & app,
                            utf8 const & collection);
  
  bool dispatch_payload(netcmd const & cmd);
  void begin_service();
  bool process();
};


struct 
root_prefix
{
  prefix val;
  root_prefix() : val("")
  {}
};

static root_prefix const & 
get_root_prefix()
{ 
  // this is not a static variable for a bizarre reason: mac OSX runs
  // static initializers in the "wrong" order (application before
  // libraries), so the initializer for a static string in cryptopp runs
  // after the initializer for a static variable outside a function
  // here. therefore encode_hexenc() fails in the static initializer here
  // and the program crashes. curious, eh?
  static root_prefix ROOT_PREFIX;
  return ROOT_PREFIX;
}

  
session::session(protocol_role role,
                 protocol_voice voice,
                 vector<utf8> const & collections,
                 set<string> const & all_coll,
                 app_state & app,
                 string const & peer,
                 Netxx::socket_type sock, 
                 Netxx::Timeout const & to) : 
  role(role),
  voice(voice),
  collections(collections),
  all_collections(all_coll),
  app(app),
  peer_id(peer),
  fd(sock),
  str(sock, to),
  inbuf(""),
  outbuf(""),
  armed(false),
  collection(""),
  remote_peer_key_hash(""),
  authenticated(false),
  last_io_time(::time(NULL)),
  byte_in_ticker(NULL),
  byte_out_ticker(NULL),
  cert_in_ticker(NULL),
  cert_out_ticker(NULL),
  revision_in_ticker(NULL),
  revision_out_ticker(NULL),
  analyzed_ancestry(false),
  saved_nonce(""),
  received_goodbye(false),
  sent_goodbye(false),
  dbw(app, true)
{
  if (voice == client_voice)
    {
      N(collections.size() == 1,
          F("client can only sync one collection at a time"));
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

  done_refinements.insert(make_pair(rcert_item, done_marker()));
  done_refinements.insert(make_pair(mcert_item, done_marker()));
  done_refinements.insert(make_pair(fcert_item, done_marker()));
  done_refinements.insert(make_pair(key_item, done_marker()));
  
  requested_items.insert(make_pair(rcert_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(fcert_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(mcert_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(key_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(revision_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(manifest_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(file_item, boost::shared_ptr< set<id> >(new set<id>())));

  for (vector<utf8>::const_iterator i = collections.begin();
       i != collections.end(); ++i)
    {
      rebuild_merkle_trees(app, *i);
    }
}

id 
session::mk_nonce()
{
  I(this->saved_nonce().size() == 0);
  char buf[constants::merkle_hash_length_in_bytes];
  prng->GenerateBlock(reinterpret_cast<byte *>(buf), constants::merkle_hash_length_in_bytes);
  this->saved_nonce = string(buf, buf + constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void 
session::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

bool 
session::done_all_refinements()
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


bool 
session::rcert_refinement_done()
{
  return done_refinements[rcert_item].tree_is_done;
}

bool 
session::got_all_data()
{
  for (map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator i =
         requested_items.begin(); i != requested_items.end(); ++i)
    {
      if (! i->second->empty())
        return false;
    }
  return true;
}

bool 
session::all_requested_revisions_received()
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(revision_item);
  I(i != requested_items.end());
  return i->second->empty();
}

void
session::note_item_requested(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(ty);
  I(i != requested_items.end());
  i->second->insert(ident);
}

void
session::note_item_arrived(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(ty);
  I(i != requested_items.end());
  i->second->erase(ident);

  switch (ty)
    {
    case rcert_item:
      if (cert_in_ticker.get() != NULL)
        ++(*cert_in_ticker);
      break;
    case revision_item:
      if (revision_in_ticker.get() != NULL)
        ++(*revision_in_ticker);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

bool 
session::item_request_outstanding(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(ty);
  I(i != requested_items.end());
  return i->second->find(ident) != i->second->end();
}


void
session::note_item_sent(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case rcert_item:
      if (cert_out_ticker.get() != NULL)
        ++(*cert_out_ticker);
      break;
    case revision_item:
      if (revision_out_ticker.get() != NULL)
        ++(*revision_out_ticker);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

void 
session::write_netcmd_and_try_flush(netcmd const & cmd)
{
  write_netcmd(cmd, outbuf);
  // FIXME: this helps keep the protocol pipeline full but it seems to
  // interfere with initial and final sequences. careful with it.
  // write_some();
  // read_some();
}

void 
session::analyze_manifest(manifest_map const & man)
{
  L(F("analyzing %d entries in manifest\n") % man.size());
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    {
      if (! this->app.db.file_version_exists(manifest_entry_id(i)))
        {
          id tmp;
          decode_hexenc(manifest_entry_id(i).inner(), tmp);
          queue_send_data_cmd(file_item, tmp);
        }
    }
}

static bool 
is_attached(revision_id const & i, 
            map<revision_id, bool> const & attach_map)
{
  map<revision_id, bool>::const_iterator j = attach_map.find(i);
  I(j != attach_map.end());
  return j->second;
}

// this tells us whether a particular revision is "attached" -- meaning
// either our database contains the underlying manifest or else one of our
// parents (recursively, and only in the current ancestry graph we're
// requesting) is attached. if it's detached we will request it using a
// different (more efficient and less failure-prone) algorithm

void
session::analyze_attachment(revision_id const & i, 
                            set<revision_id> & visited,
                            map<revision_id, bool> & attached)
{
  typedef map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestryT;

  if (visited.find(i) != visited.end())
    return;

  visited.insert(i);
  
  bool curr_attached = false;

  if (app.db.revision_exists(i))
    {
      L(F("revision %s is attached via database\n") % i);
      curr_attached = true;
    }
  else
    {
      L(F("checking attachment of %s in ancestry\n") % i);
      ancestryT::const_iterator j = ancestry.find(i);
      if (j != ancestry.end())
        {
          for (edge_map::const_iterator k = j->second->second.edges.begin();
               k != j->second->second.edges.end(); ++k)
            {
              L(F("checking attachment of %s in parent %s\n") % i % edge_old_revision(k));
              analyze_attachment(edge_old_revision(k), visited, attached);
              if (is_attached(edge_old_revision(k), attached))
                {
                  L(F("revision %s is attached via parent %s\n") % i % edge_old_revision(k));
                  curr_attached = true;
                }
            }
        }
    }
  L(F("decided that revision %s %s attached\n") % i % (curr_attached ? "is" : "is not"));
  attached[i] = curr_attached;
}

static inline id
plain_id(manifest_id const & i)
{
  id tmp;
  hexenc<id> htmp(i.inner());
  decode_hexenc(htmp, tmp);
  return tmp;
}

static inline id
plain_id(file_id const & i)
{
  id tmp;
  hexenc<id> htmp(i.inner());
  decode_hexenc(htmp, tmp);
  return tmp;
}

void 
session::request_rev_revisions(revision_id const & init, 
                               map<revision_id, bool> attached,
                               set<revision_id> visited)
{
  typedef map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestryT;

  set<manifest_id> seen_manifests;
  set<file_id> seen_files;

  set<revision_id> frontier;
  frontier.insert(init);
  while(!frontier.empty())
    {
      set<revision_id> next_frontier;
      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        {
          if (is_attached(*i, attached))
            continue;
          
          if (visited.find(*i) != visited.end())
            continue;

          visited.insert(*i);

          ancestryT::const_iterator j = ancestry.find(*i);
          if (j != ancestry.end())
            {
              
              for (edge_map::const_iterator k = j->second->second.edges.begin();
                   k != j->second->second.edges.end(); ++k)
                {

                  next_frontier.insert(edge_old_revision(k));

                  // check out the manifest delta edge
                  manifest_id parent_manifest = edge_old_manifest(k);
                  manifest_id child_manifest = j->second->second.new_manifest;  

                  // first, if we have a child we've never seen before we will need
                  // to request it in its entrety.                
                  if (seen_manifests.find(child_manifest) == seen_manifests.end())
                    {
                      if (this->app.db.manifest_version_exists(child_manifest))
                        L(F("not requesting (in reverse) initial manifest %s as we already have it\n") % child_manifest);
                      else
                        {
                          L(F("requesting (in reverse) initial manifest data %s\n") % child_manifest);
                          queue_send_data_cmd(manifest_item, plain_id(child_manifest));
                        }
                      seen_manifests.insert(child_manifest);
                    }

                  // second, if the parent is nonempty, we want to ask for an edge to it                  
                  if (!parent_manifest.inner()().empty())
                    {
                      if (this->app.db.manifest_version_exists(parent_manifest))
                        L(F("not requesting (in reverse) manifest delta to %s as we already have it\n") % parent_manifest);
                      else
                        {
                          L(F("requesting (in reverse) manifest delta %s -> %s\n") 
                            % child_manifest % parent_manifest);
                          reverse_delta_requests.insert(make_pair(plain_id(child_manifest),
                                                                  plain_id(parent_manifest)));
                          queue_send_delta_cmd(manifest_item, 
                                               plain_id(child_manifest), 
                                               plain_id(parent_manifest));
                        }
                      seen_manifests.insert(parent_manifest);
                    }


                  
                  // check out each file delta edge
                  change_set const & cset = edge_changes(k);
                  for (change_set::delta_map::const_iterator d = cset.deltas.begin(); 
                       d != cset.deltas.end(); ++d)
                    {
                      file_id parent_file (delta_entry_src(d));
                      file_id child_file (delta_entry_dst(d));


                      // first, if we have a child we've never seen before we will need
                      // to request it in its entrety.            
                      if (seen_files.find(child_file) == seen_files.end())
                        {
                          if (this->app.db.file_version_exists(child_file))
                            L(F("not requesting (in reverse) initial file %s as we already have it\n") % child_file);
                          else
                            {
                              L(F("requesting (in reverse) initial file data %s\n") % child_file);
                              queue_send_data_cmd(file_item, plain_id(child_file));
                            }
                          seen_files.insert(child_file);
                        }
                      
                      // second, if the parent is nonempty, we want to ask for an edge to it              
                      if (!parent_file.inner()().empty())
                        {
                          if (this->app.db.file_version_exists(parent_file))
                            L(F("not requesting (in reverse) file delta to %s as we already have it\n") % parent_file);
                          else
                            {
                              L(F("requesting (in reverse) file delta %s -> %s on %s\n") 
                                % child_file % parent_file % delta_entry_path(d));
                              reverse_delta_requests.insert(make_pair(plain_id(child_file),
                                                                      plain_id(parent_file)));
                              queue_send_delta_cmd(file_item, 
                                                   plain_id(child_file), 
                                                   plain_id(parent_file));
                            }
                          seen_files.insert(parent_file);
                        }                     
                    }
                }
              
              // now actually consume the data packet, which will wait on the
              // arrival of its prerequisites in the packet_db_writer
              this->dbw.consume_revision_data(j->first, j->second->first);
            }
        }
      frontier = next_frontier;
    }
}

void 
session::request_fwd_revisions(revision_id const & i, 
                               map<revision_id, bool> attached,
                               set<revision_id> & visited)
{
  if (visited.find(i) != visited.end())
    return;
  
  visited.insert(i);
  
  L(F("visiting revision '%s' for forward deltas\n") % i);

  typedef map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestryT;
  
  ancestryT::const_iterator j = ancestry.find(i);
  if (j != ancestry.end())
    {
      edge_map::const_iterator an_attached_edge = j->second->second.edges.end();

      // first make sure we've requested enough to get to here by
      // calling ourselves recursively. this is the forward path after all.

      for (edge_map::const_iterator k = j->second->second.edges.begin();
           k != j->second->second.edges.end(); ++k)
        {
          if (is_attached(edge_old_revision(k), attached))
            {
              request_fwd_revisions(edge_old_revision(k), attached, visited);
              an_attached_edge = k;
            }
        }
      
      I(an_attached_edge != j->second->second.edges.end());
      
      // check out the manifest delta edge
      manifest_id parent_manifest = edge_old_manifest(an_attached_edge);
      manifest_id child_manifest = j->second->second.new_manifest;      
      if (this->app.db.manifest_version_exists(child_manifest))
        L(F("not requesting forward manifest delta to '%s' as we already have it\n") 
          % child_manifest);
      else
        {
          if (parent_manifest.inner()().empty())
            {
              L(F("requesting full manifest data %s\n") % child_manifest);
              queue_send_data_cmd(manifest_item, plain_id(child_manifest));
            }
          else
            {
              L(F("requesting forward manifest delta %s -> %s\n")
                % parent_manifest % child_manifest);
              queue_send_delta_cmd(manifest_item, 
                                   plain_id(parent_manifest), 
                                   plain_id(child_manifest));
            }
        }

      // check out each file delta edge
      change_set const & an_attached_cset = an_attached_edge->second.second;
      for (change_set::delta_map::const_iterator k = an_attached_cset.deltas.begin();
           k != an_attached_cset.deltas.end(); ++k)
        {
          if (this->app.db.file_version_exists(delta_entry_dst(k)))
            L(F("not requesting forward delta %s -> %s on file %s as we already have it\n")
              % delta_entry_src(k) % delta_entry_dst(k) % delta_entry_path(k));
          else
            {
              if (delta_entry_src(k).inner()().empty())
                {
                  L(F("requesting full file data %s\n") % delta_entry_dst(k));
                  queue_send_data_cmd(file_item, plain_id(delta_entry_dst(k)));
                }
              else
                {
                  
                  L(F("requesting forward delta %s -> %s on file %s\n")
                    % delta_entry_src(k) % delta_entry_dst(k) % delta_entry_path(k));
                  queue_send_delta_cmd(file_item, 
                                       plain_id(delta_entry_src(k)), 
                                       plain_id(delta_entry_dst(k)));
                }
            }
        }
      // now actually consume the data packet, which will wait on the
      // arrival of its prerequisites in the packet_db_writer
      this->dbw.consume_revision_data(j->first, j->second->first);
    }
}

void 
session::analyze_ancestry_graph()
{
  typedef map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestryT;

  if (! (all_requested_revisions_received() && rcert_refinement_done()))
    return;

  if (analyzed_ancestry)
    return;

  set<revision_id> heads;
  {
    set<revision_id> nodes, parents;
    L(F("analyzing %d ancestry edges\n") % ancestry.size());
    
    for (ancestryT::const_iterator i = ancestry.begin(); i != ancestry.end(); ++i)
      {
        nodes.insert(i->first);
        for (edge_map::const_iterator j = i->second->second.edges.begin();
             j != i->second->second.edges.end(); ++j)
          {
            parents.insert(edge_old_revision(j));
          }
      }
    
    set_difference(nodes.begin(), nodes.end(),
                   parents.begin(), parents.end(),
                   inserter(heads, heads.begin()));
  }

  L(F("isolated %d heads\n") % heads.size());

  // first we determine the "attachment status" of each node in our ancestry
  // graph. 

  map<revision_id, bool> attached;
  set<revision_id> visited;
  for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    analyze_attachment(*i, visited, attached);

  // then we walk the graph upwards, recursively, starting from each of the
  // heads. we either walk requesting forward deltas or reverse deltas,
  // depending on whether we are walking an attached or detached subgraph,
  // respectively. the forward walk ignores detached nodes, the backward walk
  // ignores attached nodes.

  set<revision_id> fwd_visited, rev_visited;

  for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    {
      map<revision_id, bool>::const_iterator k = attached.find(*i);
      I(k != attached.end());
      
      if (k->second)
        {
          L(F("requesting attached ancestry of revision '%s'\n") % *i);
          request_fwd_revisions(*i, attached, fwd_visited);
        }
      else
        {
          L(F("requesting detached ancestry of revision '%s'\n") % *i);
          request_rev_revisions(*i, attached, rev_visited);
        }       
    }
  analyzed_ancestry = true;
}

Netxx::Probe::ready_type 
session::which_events() const
{    
  if (outbuf.empty())
    {
      if (inbuf.size() < constants::netcmd_maxsz)
        return Netxx::Probe::ready_read | Netxx::Probe::ready_oobd;
      else
        return Netxx::Probe::ready_oobd;
    }
  else
    {
      if (inbuf.size() < constants::netcmd_maxsz)
        return Netxx::Probe::ready_write | Netxx::Probe::ready_read | Netxx::Probe::ready_oobd;
      else
        return Netxx::Probe::ready_write | Netxx::Probe::ready_oobd;
    }       
}

bool 
session::read_some()
{
  I(inbuf.size() < constants::netcmd_maxsz);
  char tmp[constants::bufsz];
  Netxx::signed_size_type count = str.read(tmp, sizeof(tmp));
  if(count > 0)
    {
      L(F("read %d bytes from fd %d (peer %s)\n") % count % fd % peer_id);
      inbuf.append(string(tmp, tmp + count));
      mark_recent_io();
      if (byte_in_ticker.get() != NULL)
        (*byte_in_ticker) += count;
      return true;
    }
  else
    return false;
}

bool 
session::write_some()
{
  I(!outbuf.empty());    
  Netxx::signed_size_type count = str.write(outbuf.data(), 
                                            std::min(outbuf.size(), constants::bufsz));
  if(count > 0)
    {
      outbuf.erase(0, count);
      L(F("wrote %d bytes to fd %d (peer %s), %d remain in output buffer\n") 
        % count % fd % peer_id % outbuf.size());
      mark_recent_io();
      if (byte_out_ticker.get() != NULL)
        (*byte_out_ticker) += count;
      return true;
    }
  else
    return false;
}

// senders

void 
session::queue_bye_cmd() 
{
  L(F("queueing 'bye' command\n"));
  netcmd cmd;
  cmd.cmd_code = bye_cmd;
  write_netcmd_and_try_flush(cmd);
  this->sent_goodbye = true;
}

void 
session::queue_error_cmd(string const & errmsg)
{
  L(F("queueing 'error' command\n"));
  netcmd cmd;
  cmd.cmd_code = error_cmd;
  write_error_cmd_payload(errmsg, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_done_cmd(size_t level, 
                        netcmd_item_type type) 
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing 'done' command for %s level %s\n") % typestr % level);
  netcmd cmd;
  cmd.cmd_code = done_cmd;
  write_done_cmd_payload(level, type, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_hello_cmd(id const & server, 
                         id const & nonce) 
{
  netcmd cmd;
  cmd.cmd_code = hello_cmd;
  write_hello_cmd_payload(server, nonce, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_anonymous_cmd(protocol_role role, 
                             string const & collection, 
                             id const & nonce2)
{
  netcmd cmd;
  cmd.cmd_code = anonymous_cmd;
  write_anonymous_cmd_payload(role, collection, nonce2, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_auth_cmd(protocol_role role, 
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
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_confirm_cmd(string const & signature)
{
  netcmd cmd;
  cmd.cmd_code = confirm_cmd;
  write_confirm_cmd_payload(signature, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_refine_cmd(merkle_node const & node)
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
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_send_data_cmd(netcmd_item_type type,
                             id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);

  if (role == source_role)
    {
      L(F("not queueing request for %s '%s' as we are in pure source role\n") 
        % typestr % hid);
      return;
    }

  if (item_request_outstanding(type, item))
    {
      L(F("not queueing request for %s '%s' as we already requested it\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing request for data of %s item '%s'\n")
    % typestr % hid);
  netcmd cmd;
  cmd.cmd_code = send_data_cmd;
  write_send_data_cmd_payload(type, item, cmd.payload);
  write_netcmd_and_try_flush(cmd);
  note_item_requested(type, item);
}
    
void 
session::queue_send_delta_cmd(netcmd_item_type type,
                              id const & base, 
                              id const & ident)
{
  I(type == manifest_item || type == file_item);

  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid;
  encode_hexenc(base, base_hid);
  hexenc<id> ident_hid;
  encode_hexenc(ident, ident_hid);
  
  if (role == source_role)
    {
      L(F("not queueing request for %s delta '%s' -> '%s' as we are in pure source role\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  if (item_request_outstanding(type, ident))
    {
      L(F("not queueing request for %s delta '%s' -> '%s' as we already requested the target\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  L(F("queueing request for contents of %s delta '%s' -> '%s'\n")
    % typestr % base_hid % ident_hid);
  netcmd cmd;
  cmd.cmd_code = send_delta_cmd;
  write_send_delta_cmd_payload(type, base, ident, cmd.payload);
  write_netcmd_and_try_flush(cmd);
  note_item_requested(type, ident);
}

void 
session::queue_data_cmd(netcmd_item_type type,
                        id const & item, 
                        string const & dat)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);

  if (role == sink_role)
    {
      L(F("not queueing %s data for '%s' as we are in pure sink role\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing %d bytes of data for %s item '%s'\n")
    % dat.size() % typestr % hid);
  netcmd cmd;
  cmd.cmd_code = data_cmd;
  write_data_cmd_payload(type, item, dat, cmd.payload);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, item);
}

void
session::queue_delta_cmd(netcmd_item_type type,
                         id const & base, 
                         id const & ident, 
                         delta const & del)
{
  I(type == manifest_item || type == file_item);
  I(! del().empty() || ident == base);
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid;
  encode_hexenc(base, base_hid);
  hexenc<id> ident_hid;
  encode_hexenc(ident, ident_hid);

  if (role == sink_role)
    {
      L(F("not queueing %s delta '%s' -> '%s' as we are in pure sink role\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  L(F("queueing %s delta '%s' -> '%s'\n")
    % typestr % base_hid % ident_hid);
  netcmd cmd;
  cmd.cmd_code = delta_cmd;  
  write_delta_cmd_payload(type, base, ident, del, cmd.payload);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, ident);
}

void 
session::queue_nonexistant_cmd(netcmd_item_type type,
                               id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);
  if (role == sink_role)
    {
      L(F("not queueing note of nonexistence of %s item '%s' as we are in pure sink role\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing note of nonexistance of %s item '%s'\n")
    % typestr % hid);
  netcmd cmd;
  cmd.cmd_code = nonexistant_cmd;
  write_nonexistant_cmd_payload(type, item, cmd.payload);
  write_netcmd_and_try_flush(cmd);
}

// processors

bool 
session::process_bye_cmd() 
{
  L(F("received 'bye' netcmd\n"));
  this->received_goodbye = true;
  return true;
}

bool 
session::process_error_cmd(string const & errmsg) 
{
  W(F("received network error: %s\n") % errmsg);
  this->received_goodbye = true;
  return true;
}

bool 
session::process_done_cmd(size_t level, netcmd_item_type type) 
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

      if (all_requested_revisions_received())
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

bool 
session::process_hello_cmd(id const & server, 
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
      
      if (app.signing_key() != "")
        {
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
          load_priv_key(app, app.signing_key, our_priv);
          make_signature(app.lua, app.signing_key, our_priv, nonce(), sig);
          decode_base64(sig, sig_raw);
          
          // make a new nonce of our own and send off the 'auth'
          queue_auth_cmd(this->role, this->collection(), our_key_hash_raw, 
                         nonce, mk_nonce(), sig_raw());
        }
      else
        {
          queue_anonymous_cmd(this->role, this->collection(), mk_nonce());
        }
      return true;
    }
  else
    {
      W(F("unknown server key.  disconnecting.\n"));
    }
  return false;
}

bool 
session::process_anonymous_cmd(protocol_role role, 
                               string const & collection, 
                               id const & nonce2)
{
  hexenc<id> hnonce2;
  encode_hexenc(nonce2, hnonce2);

  L(F("received 'anonymous' netcmd from client for collection '%s' "
      "in %s mode with nonce2 '%s'\n")
    %  collection % (role == source_and_sink_role ? "source and sink" :
                     (role == source_role ? "source " : "sink"))
    % hnonce2);

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

  if (role != sink_role)
    {
      W(F("rejected attempt at anonymous connection for write\n"));
      this->saved_nonce = id("");
      return false;
    }

  if (! ((this->role == source_role || this->role == source_and_sink_role)
         && app.lua.hook_get_netsync_anonymous_read_permitted(collection)))
    {
      W(F("anonymous read permission denied for '%s'\n") % collection);
      this->saved_nonce = id("");
      return false;
    }

  // get our private key and sign back
  L(F("anonymous read permitted, signing back nonce\n"));
  base64<rsa_sha1_signature> sig;
  rsa_sha1_signature sig_raw;
  base64< arc4<rsa_priv_key> > our_priv;
  load_priv_key(app, app.signing_key, our_priv);
  make_signature(app.lua, app.signing_key, our_priv, nonce2(), sig);
  decode_base64(sig, sig_raw);
  queue_confirm_cmd(sig_raw());
  this->collection = collection;
  this->authenticated = true;
  this->role = source_role;
  return true;
}

bool 
session::process_auth_cmd(protocol_role role, 
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

  if (!app.db.public_key_exists(their_key_hash))
    {
      W(F("unknown key hash '%s'\n") % their_key_hash);
      this->saved_nonce = id("");
      return false;
    }
  
  // get their public key
  rsa_keypair_id their_id;
  base64<rsa_pub_key> their_key;
  app.db.get_pubkey(their_key_hash, their_id, their_key);

  if (role == sink_role || role == source_and_sink_role)
    {
      if (! ((this->role == source_role || this->role == source_and_sink_role)
             && app.lua.hook_get_netsync_read_permitted(collection, 
                                                        their_id())))
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
                                                         their_id())))
        {
          W(F("write permission denied for '%s'\n") % collection);
          this->saved_nonce = id("");
          return false;
        }
    }
  
  // save their identity 
  this->remote_peer_key_hash = client;
  
  // check the signature
  base64<rsa_sha1_signature> sig;
  encode_base64(rsa_sha1_signature(signature), sig);
  if (check_signature(app.lua, their_id, their_key, nonce1(), sig))
    {
      // get our private key and sign back
      L(F("client signature OK, accepting authentication\n"));
      base64<rsa_sha1_signature> sig;
      rsa_sha1_signature sig_raw;
      base64< arc4<rsa_priv_key> > our_priv;
      load_priv_key(app, app.signing_key, our_priv);
      make_signature(app.lua, app.signing_key, our_priv, nonce2(), sig);
      decode_base64(sig, sig_raw);
      queue_confirm_cmd(sig_raw());
      this->collection = collection;
      this->authenticated = true;
      // assume the (possibly degraded) opposite role
      switch (role)
        {
        case source_role:
          I(this->role != source_role);
          this->role = sink_role;
          break;
        case source_and_sink_role:
          I(this->role == source_and_sink_role);
          break;
        case sink_role:
          I(this->role != sink_role);
          this->role = source_role;
          break;          
        }
      return true;
    }
  else
    {
      W(F("bad client signature\n"));         
    }  
  return false;
}

bool 
session::process_confirm_cmd(string const & signature)
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
          merkle_ptr root;
          load_merkle_node(key_item, this->collection, 0, get_root_prefix().val, root);
          queue_refine_cmd(*root);
          queue_done_cmd(0, key_item);

          load_merkle_node(mcert_item, this->collection, 0, get_root_prefix().val, root);
          queue_refine_cmd(*root);
          queue_done_cmd(0, mcert_item);

          load_merkle_node(fcert_item, this->collection, 0, get_root_prefix().val, root);
          queue_refine_cmd(*root);
          queue_done_cmd(0, fcert_item);

          load_merkle_node(rcert_item, this->collection, 0, get_root_prefix().val, root);
          queue_refine_cmd(*root);
          queue_done_cmd(0, rcert_item);
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

static bool 
data_exists(netcmd_item_type type, 
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
      return false;
    case mcert_item:
      return false;
    case manifest_item:
      return app.db.manifest_version_exists(manifest_id(hitem));
    case file_item:
      return app.db.file_version_exists(file_id(hitem));
    case revision_item:
      return app.db.revision_exists(revision_id(hitem));
    case rcert_item:
      return app.db.revision_cert_exists(hitem);
    }
  return false;
}

static void 
load_data(netcmd_item_type type, 
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

    case revision_item:
      if (app.db.revision_exists(revision_id(hitem)))
        {
          revision_data mdat;
          data dat;
          app.db.get_revision(revision_id(hitem), mdat);
          unpack(mdat.inner(), dat);
          out = dat();
        }
      else
        {
          throw bad_decode(F("revision '%s' does not exist in our database") % hitem);
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

    case rcert_item:
      if(app.db.revision_cert_exists(hitem))
        {
          revision<cert> c;
          app.db.get_revision_cert(hitem, c);
          string tmp;
          write_cert(c.inner(), out);
        }
      else
        {
          throw bad_decode(F("rcert '%s' does not exist in our database") % hitem);
        }
      break;

    case mcert_item:
      throw bad_decode(F("mcert '%s' not supported") % hitem);
      break;

    case fcert_item:
      throw bad_decode(F("fcert '%s' not supported") % hitem);
      break;
    }
}


bool 
session::process_refine_cmd(merkle_node const & their_node)
{
  prefix pref;
  hexenc<prefix> hpref;
  their_node.get_raw_prefix(pref);
  their_node.get_hex_prefix(hpref);
  string typestr;

  netcmd_item_type_to_string(their_node.type, typestr);
  size_t lev = static_cast<size_t>(their_node.level);
  
  L(F("received 'refine' netcmd on %s node '%s', level %d\n") 
    % typestr % hpref % lev);
  
  if (!merkle_node_exists(their_node.type, this->collection, 
                          their_node.level, pref))
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
      merkle_ptr our_node;
      load_merkle_node(their_node.type, this->collection, 
                       their_node.level, pref, our_node);
      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {         
          switch (their_node.get_slot_state(slot))
            {
            case empty_state:
              switch (our_node->get_slot_state(slot))
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
                    I(their_node.type == our_node->type);
                    string tmp;
                    id slotval;
                    our_node->get_raw_slot(slot, slotval);
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
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    I(our_node->type == their_node.type);
                    load_merkle_node(their_node.type, this->collection, 
                                     our_node->level + 1, subprefix, our_subtree);
                    I(our_node->type == our_subtree->type);
                    queue_refine_cmd(*our_subtree);
                  }
                  break;

                }
              break;


            case live_leaf_state:
              switch (our_node->get_slot_state(slot))
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
                    our_node->get_raw_slot(slot, our_slotval);               
                    if (their_slotval == our_slotval)
                      {
                        hexenc<id> hslotval;
                        their_node.get_hex_slot(slot, hslotval);
                        L(F("(#6) we both have live %s leaf '%s'\n") % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        I(their_node.type == our_node->type);
                        string tmp;
                        load_data(our_node->type, our_slotval, this->app, tmp);
                        queue_send_data_cmd(their_node.type, their_slotval);
                        queue_data_cmd(our_node->type, our_slotval, tmp);
                      }
                  }
                  break;

                case dead_leaf_state:
                  // 7: theirs == live, ours == dead 
                  L(F("(#7) they have a live leaf at slot %d in %s node %s, level %d, we have a dead one\n")
                    % slot % typestr % hpref % lev);
                  {
                    id our_slotval, their_slotval;
                    our_node->get_raw_slot(slot, our_slotval);
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

                    id their_slotval;
                    hexenc<id> their_hval;
                    their_node.get_raw_slot(slot, their_slotval);
                    encode_hexenc(their_slotval, their_hval);
                    if (data_exists(their_node.type, their_slotval, app))
                      L(F("(#8) we have a copy of their live leaf '%s' in slot %d of %s node '%s', level %d\n")
                        % their_hval % slot % typestr % hpref % lev);
                    else
                      {
                        L(F("(#8) requesting a copy of their live leaf '%s' in slot %d of %s node '%s', level %d\n")
                          % their_hval % slot % typestr % hpref % lev);
                        queue_send_data_cmd(their_node.type, their_slotval);
                      }
                    
                    L(F("(#8) sending our subtree for refinement, in slot %d of %s node '%s', level %d\n")
                      % slot % typestr % hpref % lev);
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    load_merkle_node(our_node->type, this->collection, 
                                     our_node->level + 1, subprefix, our_subtree);
                    queue_refine_cmd(*our_subtree);
                  }
                  break;
                }
              break;


            case dead_leaf_state:
              switch (our_node->get_slot_state(slot))
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
                    our_node->get_raw_slot(slot, our_slotval);
                    hexenc<id> hslotval;
                    our_node->get_hex_slot(slot, hslotval);
                    if (their_slotval == our_slotval)
                      {
                        L(F("(#10) we both have %s leaf %s, theirs is dead\n") 
                          % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        I(their_node.type == our_node->type);
                        string tmp;
                        load_data(our_node->type, our_slotval, this->app, tmp);
                        queue_data_cmd(our_node->type, our_slotval, tmp);
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
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    load_merkle_node(our_node->type, this->collection, 
                                     our_node->level + 1, subprefix, our_subtree);
                    queue_refine_cmd(*our_subtree);
                  }
                  break;
                }
              break;


            case subtree_state:
              switch (our_node->get_slot_state(slot))
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
                    our_node->get_raw_slot(slot, our_slotval);
                    hexenc<id> hslotval;
                    encode_hexenc(our_slotval, hslotval);
                    
                    pick_slot_and_prefix_for_value(our_slotval, our_node->level + 1, subslot, 
                                                   our_fake_subtree.pref);
                    L(F("(#14) pushed our leaf '%s' into fake subtree slot %d, level %d\n")
                      % hslotval % subslot % (lev + 1));
                    our_fake_subtree.type = their_node.type;
                    our_fake_subtree.level = our_node->level + 1;
                    our_fake_subtree.set_raw_slot(subslot, our_slotval);
                    our_fake_subtree.set_slot_state(subslot, our_node->get_slot_state(slot));
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
                    our_node->get_raw_slot(slot, our_slotval);
                    pick_slot_and_prefix_for_value(our_slotval, our_node->level + 1, subslot, 
                                                   our_fake_subtree.pref);
                    our_fake_subtree.type = their_node.type;
                    our_fake_subtree.level = our_node->level + 1;
                    our_fake_subtree.set_raw_slot(subslot, our_slotval);
                    our_fake_subtree.set_slot_state(subslot, our_node->get_slot_state(slot));
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
                    our_node->get_raw_slot(slot, our_slotval);
                    our_node->get_hex_slot(slot, hslotval);
                    if (their_slotval == our_slotval)
                      {
                        L(F("(#16) we both have %s subtree '%s'\n") % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        L(F("(#16) %s subtrees at slot %d differ, refining ours\n") % typestr % slot);
                        prefix subprefix;
                        our_node->extended_raw_prefix(slot, subprefix);
                        merkle_ptr our_subtree;
                        load_merkle_node(our_node->type, this->collection, 
                                         our_node->level + 1, subprefix, our_subtree);
                        queue_refine_cmd(*our_subtree);
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


bool 
session::process_send_data_cmd(netcmd_item_type type,
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

bool 
session::process_send_delta_cmd(netcmd_item_type type,
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
            return process_send_data_cmd(type, ident);
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
            return process_send_data_cmd(type, ident);
          }
      }
      break;
      
    default:
      throw bad_decode(F("delta requested for item type %s\n") % typestr);
    }
  queue_delta_cmd(type, base, ident, del);
  return true;
}

bool 
session::process_data_cmd(netcmd_item_type type,
                          id const & item, 
                          string const & dat)
{  
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  // FIXME: what does the above comment mean?  note_item_arrived does require
  // that the item passed to it have been requested...
  note_item_arrived(type, item);
                           
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
          this->dbw.consume_public_key(keyid, pub);
        }
      break;

    case mcert_item:
      L(F("ignoring manifest cert '%s'\n") % hitem);
      break;

    case rcert_item:
      if (this->app.db.revision_cert_exists(hitem))
        L(F("revision cert '%s' already exists in our database\n")  % hitem);
      else
        {
          cert c;
          read_cert(dat, c);
          hexenc<id> tmp;
          cert_hash_code(c, tmp);
          if (! (tmp == hitem))
            throw bad_decode(F("hash check failed for revision cert '%s'")  % hitem);
          this->dbw.consume_revision_cert(revision<cert>(c));
          if (!app.db.revision_exists(revision_id(c.ident)))
            {
              id rid;
              decode_hexenc(c.ident, rid);
              queue_send_data_cmd(revision_item, rid);
            }
        }
      break;

    case fcert_item:
      L(F("ignoring file cert '%s'\n") % hitem);
      break;

    case revision_item:
      {
        revision_id rid(hitem);
        if (this->app.db.revision_exists(rid))
          L(F("revision '%s' already exists in our database\n") % hitem);
        else
          {
            L(F("received revision '%s' \n") % hitem);
            boost::shared_ptr< pair<revision_data, revision_set > > 
              rp(new pair<revision_data, revision_set>());
            
            base64< gzip<data> > packed;
            pack(data(dat), packed);
            rp->first = revision_data(packed);
            read_revision_set(dat, rp->second);
            ancestry.insert(std::make_pair(rid, rp));
            if (rcert_refinement_done())
              {
                analyze_ancestry_graph();
              }
          }
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
            this->dbw.consume_manifest_data(mid, manifest_data(packed_dat));
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
            this->dbw.consume_file_data(fid, file_data(packed_dat));
          }
      }
      break;

    }
      return true;
}

bool 
session::process_delta_cmd(netcmd_item_type type,
                           id const & base, 
                           id const & ident, 
                           delta const & del)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hbase, hident;
  encode_hexenc(base, hbase);
  encode_hexenc(ident, hident);

  pair<id,id> id_pair = make_pair(base, ident);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  // FIXME: what does the above comment mean?  note_item_arrived does require
  // that the item passed to it have been requested...
  note_item_arrived(type, ident);

  switch (type)
    {
    case manifest_item:
      {
        manifest_id src_manifest(hbase), dst_manifest(hident);
        base64< gzip<delta> > packed_del;
        pack(del, packed_del);
        if (reverse_delta_requests.find(id_pair)
            != reverse_delta_requests.end())
          {
            reverse_delta_requests.erase(id_pair);
            this->dbw.consume_manifest_reverse_delta(src_manifest, 
                                                     dst_manifest,
                                                     manifest_delta(packed_del));
          }
        else
          this->dbw.consume_manifest_delta(src_manifest, 
                                           dst_manifest,
                                           manifest_delta(packed_del));
        
      }
      break;

    case file_item:
      {
        file_id src_file(hbase), dst_file(hident);
        base64< gzip<delta> > packed_del;
        pack(del, packed_del);
        if (reverse_delta_requests.find(id_pair)
            != reverse_delta_requests.end())
          {
            reverse_delta_requests.erase(id_pair);
            this->dbw.consume_file_reverse_delta(src_file, 
                                                 dst_file,
                                                 file_delta(packed_del));
          }
        else
          this->dbw.consume_file_delta(src_file, 
                                       dst_file,
                                       file_delta(packed_del));
      }
      break;
      
    default:
      L(F("ignoring delta received for item type %s\n") % typestr);
      break;
    }
  return true;
}

bool 
session::process_nonexistant_cmd(netcmd_item_type type,
                                 id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  L(F("received 'nonexistant' netcmd for %s '%s'\n") 
    % typestr % hitem);
  note_item_arrived(type, item);
  return true;
}

bool
session::merkle_node_exists(netcmd_item_type type,
                            utf8 const & collection,			
                            size_t level,
                            prefix const & pref)
{
  map< std::pair<utf8, netcmd_item_type>, 
       boost::shared_ptr<merkle_table> >::const_iterator i = 
    merkle_tables.find(std::make_pair(collection,type));
  
  I(i != merkle_tables.end());
  merkle_table::const_iterator j = i->second->find(std::make_pair(pref, level));
  return (j != i->second->end());
}

void 
session::load_merkle_node(netcmd_item_type type,
                          utf8 const & collection,			
                          size_t level,
                          prefix const & pref,
                          merkle_ptr & node)
{
  map< std::pair<utf8, netcmd_item_type>, 
       boost::shared_ptr<merkle_table> >::const_iterator i = 
    merkle_tables.find(std::make_pair(collection,type));
  
  I(i != merkle_tables.end());
  merkle_table::const_iterator j = i->second->find(std::make_pair(pref, level));
  I(j != i->second->end());
  node = j->second;
}


bool 
session::dispatch_payload(netcmd const & cmd)
{
  
  switch (cmd.cmd_code)
    {
      
    case bye_cmd:
      return process_bye_cmd();
      break;

    case error_cmd:
      {
        string errmsg;
        read_error_cmd_payload(cmd.payload, errmsg);
        return process_error_cmd(errmsg);
      }
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

    case anonymous_cmd:
      require(! authenticated, "anonymous netcmd received when not authenticated");
      require(voice == server_voice, "anonymous netcmd received in server voice");
      require(role == source_role ||
              role == source_and_sink_role, 
              "anonymous netcmd received in source or source/sink role");
      {
        protocol_role role;
        string collection;
        id nonce2;
        read_anonymous_cmd_payload(cmd.payload, role, collection, nonce2);
        return process_anonymous_cmd(role, collection, nonce2);
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
void 
session::begin_service()
{
  base64<rsa_pub_key> pub_encoded;
  app.db.get_key(app.signing_key, pub_encoded);
  hexenc<id> keyhash;
  id keyhash_raw;
  key_hash_code(app.signing_key, pub_encoded, keyhash);
  decode_hexenc(keyhash, keyhash_raw);
  queue_hello_cmd(keyhash_raw(), mk_nonce());
}

void 
session::maybe_say_goodbye()
{
  if (done_all_refinements() &&
      got_all_data())
    queue_bye_cmd();
}

bool 
session::arm()
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
  try 
    {      
      if (!arm())
        return true;
      
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


static void 
call_server(protocol_role role,
            vector<utf8> const & collections,
            set<string> const & all_collections,
            app_state & app,
            utf8 const & address,
            Netxx::port_type default_port,
            unsigned long timeout_seconds)
{
  Netxx::Probe probe;
  Netxx::Timeout timeout(static_cast<long>(timeout_seconds)), instant(0,1);

  // FIXME: split into labels and convert to ace here.

  P(F("connecting to %s\n") % address());
  Netxx::Stream server(address().c_str(), default_port, timeout); 
  session sess(role, client_voice, collections, all_collections, app, 
               address(), server.get_socketfd(), timeout);

  sess.byte_in_ticker.reset(new ticker("bytes in", ">", 256));
  sess.byte_out_ticker.reset(new ticker("bytes out", "<", 256));
  if (role == sink_role)
    {
      sess.cert_in_ticker.reset(new ticker("certs in", "c", 3));
      sess.revision_in_ticker.reset(new ticker("revs in", "r", 1));
    }
  else if (role == source_role)
    {
      sess.cert_out_ticker.reset(new ticker("certs out", "C", 3));
      sess.revision_out_ticker.reset(new ticker("revs out", "R", 1));
    }
  else
    {
      I(role == source_and_sink_role);
      sess.revision_in_ticker.reset(new ticker("revs in", "r", 1));
      sess.revision_out_ticker.reset(new ticker("revs out", "R", 1));
    }
  
  while (true)
    {       
      bool armed = false;
      try 
        {
          armed = sess.arm();
        }
      catch (bad_decode & bd)
        {
          W(F("caught bad_decode exception decoding input from peer %s: '%s'\n") 
            % sess.peer_id % bd.what);
          return;         
        }

      probe.clear();
      probe.add(sess.str, sess.which_events());
      Netxx::Probe::result_type res = probe.ready(armed ? instant : timeout);
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;
      
      if (fd == -1 && !armed) 
        {
          P(F("timed out waiting for I/O with peer %s, disconnecting\n") % sess.peer_id);
          return;
        }
      
      if (event & Netxx::Probe::ready_read)
        {
          if (sess.read_some())
            {
              try 
                {
                  armed = sess.arm();
                }
              catch (bad_decode & bd)
                {
                  W(F("caught bad_decode exception decoding input from peer %s: '%s'\n") 
                    % sess.peer_id % bd.what);
                  return;         
                }
            }
          else
            {         
              if (sess.sent_goodbye)
                P(F("read from fd %d (peer %s) closed OK after goodbye\n") % fd % sess.peer_id);
              else
                P(F("read from fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
              return;
            }
        }
      
      if (event & Netxx::Probe::ready_write)
        {
          if (! sess.write_some())
            {
              if (sess.sent_goodbye)
                P(F("write on fd %d (peer %s) closed OK after goodbye\n") % fd % sess.peer_id);
              else
                P(F("write on fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
              return;
            }
        }
      
      if (event & Netxx::Probe::ready_oobd)
        {
          P(F("got OOB data on fd %d (peer %s), disconnecting\n") 
            % fd % sess.peer_id);
          return;
        }      

      if (armed)
        {
          if (!sess.process())
            {
              P(F("terminated exchange with %s\n") 
                % sess.peer_id);
              return;
            }
        }

      if (sess.sent_goodbye && sess.outbuf.empty() && sess.received_goodbye)
        {
          P(F("successful exchange with %s\n") 
            % sess.peer_id);
          return;
        }         
    }  
}

static void 
arm_sessions_and_calculate_probe(Netxx::Probe & probe,
                                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                 set<Netxx::socket_type> & armed_sessions)
{
  set<Netxx::socket_type> arm_failed;
  for (map<Netxx::socket_type, 
         shared_ptr<session> >::const_iterator i = sessions.begin();
       i != sessions.end(); ++i)
    {
      try 
        {
          if (i->second->arm())
            {
              L(F("fd %d is armed\n") % i->first);
              armed_sessions.insert(i->first);
            }
          probe.add(i->second->str, i->second->which_events());
        }
      catch (bad_decode & bd)
        {
          W(F("caught bad_decode exception decoding input from peer %s: '%s', marking as bad\n") 
            % i->second->peer_id % bd.what);
          arm_failed.insert(i->first);
        }         
    }
  for (set<Netxx::socket_type>::const_iterator i = arm_failed.begin();
       i != arm_failed.end(); ++i)
    {
      sessions.erase(*i);
    }
}

static void
handle_new_connection(Netxx::Address & addr,
                      Netxx::StreamServer & server,
                      Netxx::Timeout & timeout,
                      protocol_role role,
                      vector<utf8> const & collections,
                      set<string> const & all_collections,                    
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      app_state & app)
{
  L(F("accepting new connection on %s : %d\n") 
    % addr.get_name() % addr.get_port());
  Netxx::Peer client = server.accept_connection();
  
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

static void 
handle_read_available(Netxx::socket_type fd,
                      shared_ptr<session> sess,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      set<Netxx::socket_type> & armed_sessions,
                      bool & live_p)
{
  if (sess->read_some())
    {
      try
        {
          if (sess->arm())
            armed_sessions.insert(fd);
        }
      catch (bad_decode & bd)
        {
          W(F("caught bad_decode exception decoding input from peer %s: '%s', disconnecting\n") 
            % sess->peer_id % bd.what);
          sessions.erase(fd);
          live_p = false;
        }
    }
  else
    {
      P(F("fd %d (peer %s) read failed, disconnecting\n") 
        % fd % sess->peer_id);
      sessions.erase(fd);
      live_p = false;
    }
}


static void 
handle_write_available(Netxx::socket_type fd,
                       shared_ptr<session> sess,
                       map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       bool & live_p)
{
  if (! sess->write_some())
    {
      P(F("fd %d (peer %s) write failed, disconnecting\n") 
        % fd % sess->peer_id);
      sessions.erase(fd);
      live_p = false;
    }
}

static void
process_armed_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       set<Netxx::socket_type> & armed_sessions)
{
  for (set<Netxx::socket_type>::const_iterator i = armed_sessions.begin();
       i != armed_sessions.end(); ++i)
    {
      map<Netxx::socket_type, shared_ptr<session> >::iterator j;
      j = sessions.find(*i);
      if (j == sessions.end())
        continue;
      else
        {
          Netxx::socket_type fd = j->first;
          shared_ptr<session> sess = j->second;
          if (!sess->process())
            {
              P(F("fd %d (peer %s) processing finished, disconnecting\n") 
                % fd % sess->peer_id);
              sessions.erase(j);
            }
        }
    }
}

static void
reap_dead_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                   unsigned long timeout_seconds)
{
  // kill any clients which haven't done any i/o inside the timeout period
  // or who have said goodbye and flushed their output buffers
  set<Netxx::socket_type> dead_clients;
  time_t now = ::time(NULL);
  for (map<Netxx::socket_type, shared_ptr<session> >::const_iterator i = sessions.begin();
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
  for (set<Netxx::socket_type>::const_iterator i = dead_clients.begin();
       i != dead_clients.end(); ++i)
    {
      sessions.erase(*i);
    }
}

static void 
serve_connections(protocol_role role,
                  vector<utf8> const & collections,
                  set<string> const & all_collections,
                  app_state & app,
                  utf8 const & address,
                  Netxx::port_type default_port,
                  unsigned long timeout_seconds,
                  unsigned long session_limit)
{
  Netxx::Probe probe;  

  Netxx::Timeout 
    forever, 
    timeout(static_cast<long>(timeout_seconds)), 
    instant(0,1);

  Netxx::Address addr(address().c_str(), default_port, true);

  P(F("beginning service on %s : %d\n") 
    % addr.get_name() % addr.get_port());

  Netxx::StreamServer server(addr, timeout);
  
  map<Netxx::socket_type, shared_ptr<session> > sessions;
  set<Netxx::socket_type> armed_sessions;
  
  while (true)
    {      
      probe.clear();
      armed_sessions.clear();

      if (sessions.size() >= session_limit)
        W(F("session limit %d reached, some connections will be refused\n") % session_limit);
      else
        probe.add(server);

      arm_sessions_and_calculate_probe(probe, sessions, armed_sessions);

      L(F("i/o probe with %d armed\n") % armed_sessions.size());      
      Netxx::Probe::result_type res = probe.ready(sessions.empty() ? forever 
                                           : (armed_sessions.empty() ? timeout 
                                              : instant));
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;
      
      if (fd == -1)
        {
          if (armed_sessions.empty()) 
            L(F("timed out waiting for I/O (listening on %s : %d)\n") 
              % addr.get_name() % addr.get_port());
        }
      
      // we either got a new connection
      else if (fd == server)
        handle_new_connection(addr, server, timeout, role, 
                              collections, all_collections, sessions, app);
      
      // or an existing session woke up
      else
        {
          map<Netxx::socket_type, shared_ptr<session> >::iterator i;
          i = sessions.find(fd);
          if (i == sessions.end())
            {
              L(F("got woken up for action on unknown fd %d\n") % fd);
            }
          else
            {
              shared_ptr<session> sess = i->second;
              bool live_p = true;

              if (event & Netxx::Probe::ready_read)
                handle_read_available(fd, sess, sessions, armed_sessions, live_p);
                
              if (live_p && (event & Netxx::Probe::ready_write))
                handle_write_available(fd, sess, sessions, live_p);
                
              if (live_p && (event & Netxx::Probe::ready_oobd))
                {
                  P(F("got some OOB data on fd %d (peer %s), disconnecting\n") 
                    % fd % sess->peer_id);
                  sessions.erase(i);
                }
            }
        }
      process_armed_sessions(sessions, armed_sessions);
      reap_dead_sessions(sessions, timeout_seconds);
    }
}


/////////////////////////////////////////////////
//
// layer 4: monotone interface layer
//
/////////////////////////////////////////////////

static boost::shared_ptr<merkle_table>
make_root_node(session & sess,
               utf8 const & coll,
               netcmd_item_type ty)
{
  boost::shared_ptr<merkle_table> tab = 
    boost::shared_ptr<merkle_table>(new merkle_table());
  
  merkle_ptr tmp = merkle_ptr(new merkle_node());
  tmp->type = ty;

  tab->insert(std::make_pair(std::make_pair(get_root_prefix().val, 0), tmp));

  sess.merkle_tables[std::make_pair(coll, ty)] = tab;
  return tab;
}


void 
session::rebuild_merkle_trees(app_state & app,
                              utf8 const & collection)
{
  P(F("rebuilding merkle trees for collection %s\n") % collection);

  // we're not using these anymore..
  make_root_node(*this, collection, mcert_item);
  make_root_node(*this, collection, fcert_item);

  boost::shared_ptr<merkle_table> rtab = make_root_node(*this, collection, rcert_item);
  boost::shared_ptr<merkle_table> ktab = make_root_node(*this, collection, key_item);

  ticker rcerts("rcerts", "r", 256);
  ticker keys("keys", "k", 1);

  set<revision_id> revision_ids;
  set<rsa_keypair_id> inserted_keys;

  {
    // get all matching branch names
    vector< revision<cert> > certs;
    set<string> branchnames;
    app.db.get_revision_certs(branch_cert_name, certs);
    for (size_t i = 0; i < certs.size(); ++i)
      {
        cert_value name;
        decode_base64(idx(certs, i).inner().value, name);
        if (name().find(collection()) == 0)
          {
            if (branchnames.find(name()) == branchnames.end())
              P(F("including branch %s\n") % name());
            branchnames.insert(name());
            revision_ids.insert(revision_id(idx(certs, i).inner().ident));
          }
      }

    typedef std::vector< std::pair<hexenc<id>,
      std::pair<revision_id, rsa_keypair_id> > > rcert_idx;

    rcert_idx idx;
    app.db.get_revision_cert_index(idx);

    // insert all certs and keys reachable via these revisions
    for (rcert_idx::const_iterator i = idx.begin(); i != idx.end(); ++i)
      {
        hexenc<id> const & hash = i->first;
        revision_id const & ident = i->second.first;
        rsa_keypair_id const & key = i->second.second;

        if (revision_ids.find(ident) == revision_ids.end())
          continue;
        
        id raw_hash;
        decode_hexenc(hash, raw_hash);
        insert_into_merkle_tree(*rtab, rcert_item, true, raw_hash(), 0);
        ++rcerts;
        if (inserted_keys.find(key) == inserted_keys.end())
          {
            if (app.db.public_key_exists(key))
              {
                base64<rsa_pub_key> pub_encoded;
                app.db.get_key(key, pub_encoded);
                hexenc<id> keyhash;
                key_hash_code(key, pub_encoded, keyhash);
                decode_hexenc(keyhash, raw_hash);
                insert_into_merkle_tree(*ktab, key_item, true, raw_hash(), 0);
                ++keys;
              }
            inserted_keys.insert(key);
          }
      }
  }  

  recalculate_merkle_codes(*ktab, get_root_prefix().val, 0);
  recalculate_merkle_codes(*rtab, get_root_prefix().val, 0);
}

void 
run_netsync_protocol(protocol_voice voice, 
                     protocol_role role, 
                     utf8 const & addr, 
                     vector<utf8> collections,
                     app_state & app)
{  

  set<string> all_collections;
  for (vector<utf8>::const_iterator j = collections.begin(); 
       j != collections.end(); ++j)
    {
      all_collections.insert((*j)());
    }

  vector< revision<cert> > certs;
  app.db.get_revision_certs(branch_cert_name, certs);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
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

  try 
    {
      if (voice == server_voice)
        {
          serve_connections(role, collections, all_collections, app,
                            addr, static_cast<Netxx::port_type>(constants::netsync_default_port), 
                            static_cast<unsigned long>(constants::netsync_timeout_seconds), 
                            static_cast<unsigned long>(constants::netsync_connection_limit));
        }
      else    
        {
          I(voice == client_voice);
          transaction_guard guard(app.db);
          call_server(role, collections, all_collections, app, 
                      addr, static_cast<Netxx::port_type>(constants::netsync_default_port), 
                      static_cast<unsigned long>(constants::netsync_timeout_seconds));
          guard.commit();
        }
    }
  catch (Netxx::Exception & e)
    {      
      throw oops((F("trapped network exception: %s\n") % e.what()).str());;
    }
}

