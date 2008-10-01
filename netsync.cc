// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <cstdlib>
#include <memory>
#include <list>
#include <deque>
#include <stack>

#include <time.h>

#include "lexical_cast.hh"
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include "lua_hooks.hh"
#include "key_store.hh"
#include "project.hh"
#include "database.hh"
#include "cert.hh"
#include "constants.hh"
#include "enumerator.hh"
#include "keys.hh"
#include "lua.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "refiner.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "xdelta.hh"
#include "epoch.hh"
#include "platform.hh"
#include "hmac.hh"
#include "globish.hh"
#include "uri.hh"
#include "options.hh"

#include "botan/botan.h"

#include "netxx/address.h"
#include "netxx/peer.h"
#include "netxx/probe.h"
#include "netxx/socket.h"
#include "netxx/sockopt.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"
#include "netxx/timeout.h"
#include "netxx_pipe.hh"
// TODO: things to do that will break protocol compatibility
//   -- need some way to upgrade anonymous to keyed pull, without user having
//      to explicitly specify which they want
//      just having a way to respond "access denied, try again" might work
//      but perhaps better to have the anonymous command include a note "I
//      _could_ use key <...> if you prefer", and if that would lead to more
//      access, could reply "I do prefer".  (Does this lead to too much
//      information exposure?  Allows anonymous people to probe what branches
//      a key has access to.)
//   -- "warning" packet type?
//   -- Richard Levitte wants, when you (e.g.) request '*' but don't have
//      access to all of it, you just get the parts you have access to
//      (maybe with warnings about skipped branches).  to do this right,
//      should have a way for the server to send back to the client "right,
//      you're not getting the following branches: ...", so the client will
//      not include them in its merkle trie.
//   -- add some sort of vhost field to the client's first packet, saying who
//      they expect to talk to

//
// This is the "new" network synchronization (netsync) system in
// monotone. It is based on synchronizing pairs of merkle trees over an
// interactive connection.
//
// A netsync process between peers treats each peer as either a source, a
// sink, or both. When a peer is only a source, it will not write any new
// items to its database. when a peer is only a sink, it will not send any
// items from its database. When a peer is both a source and sink, it may
// send and write items freely.
//
// The post-state of a netsync is that each sink contains a superset of the
// items in its corresponding source; when peers are behaving as both
// source and sink, this means that the post-state of the sync is for the
// peers to have identical item sets.
//
//
// Data structure
// --------------
//
// Each node in a merkle tree contains a fixed number of slots. this number
// is derived from a global parameter of the protocol -- the tree fanout --
// such that the number of slots is 2^fanout. For now we will assume that
// fanout is 4 thus there are 16 slots in a node, because this makes
// illustration easier. The other parameter of the protocol is the size of
// a hash; we use SHA1 so the hash is 20 bytes (160 bits) long.
//
// Each slot in a merkle tree node is in one of 3 states:
//
//   - empty
//   - leaf
//   - subtree
//
// In addition, each leaf contains a hash code which identifies an element
// of the set being synchronized. Each subtree slot contains a hash code of
// the node immediately beneath it in the merkle tree. Empty slots contain
// no hash codes.
//
// Since empty slots have no hash code, they are represented implicitly by
// a bitmap at the head of each merkle tree node. As an additional
// integrity check, each merkle tree node contains a label indicating its
// prefix in the tree, and a hash of its own contents.
//
// In total, then, the byte-level representation of a <160,4> merkle tree
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
// So, in the worst case such a node is 367 bytes, with these parameters.
//
//
// Protocol
// --------
//
// The protocol is a binary command-packet system over TCP; each packet
// consists of a single byte which identifies the protocol version, a byte
// which identifies the command name inside that version, a size_t sent as
// a uleb128 indicating the length of the packet, that many bytes of
// payload, and finally 20 bytes of SHA-1 HMAC calculated over the payload.
// The key for the SHA-1 HMAC is 20 bytes of 0 during authentication, and a
// 20-byte random key chosen by the client after authentication (discussed
// below). Decoding involves simply buffering until a sufficient number of
// bytes are received, then advancing the buffer pointer. Any time an
// integrity check (the HMAC) fails, the protocol is assumed to have lost
// synchronization, and the connection is dropped. The parties are free to
// drop the TCP stream at any point, if too much data is received or too
// much idle time passes; no commitments or transactions are made.
//
//
// Authentication and setup
// ------------------------
//
// The exchange begins in a non-authenticated state. The server sends a
// "hello <id> <nonce>" command, which identifies the server's RSA key and
// issues a nonce which must be used for a subsequent authentication.
//
// The client then responds with either:
//
// An "auth (source|sink|both) <include_pattern> <exclude_pattern> <id>
// <nonce1> <hmac key> <sig>" command, which identifies its RSA key, notes the
// role it wishes to play in the synchronization, identifies the pattern it
// wishes to sync with, signs the previous nonce with its own key, and informs
// the server of the HMAC key it wishes to use for this session (encrypted
// with the server's public key); or
//
// An "anonymous (source|sink|both) <include_pattern> <exclude_pattern>
// <hmac key>" command, which identifies the role it wishes to play in the
// synchronization, the pattern it wishes to sync with, and the HMAC key it
// wishes to use for this session (also encrypted with the server's public
// key).
//
// The server then replies with a "confirm" command, which contains no
// other data but will only have the correct HMAC integrity code if the
// server received and properly decrypted the HMAC key offered by the
// client. This transitions the peers into an authenticated state and
// begins epoch refinement. If epoch refinement and epoch transmission
// succeed, the peers switch to data refinement and data transmission.
//
//
// Refinement
// ----------
//
// Refinement is executed by "refiners"; there is a refiner for each
// set of 'items' being exchanged: epochs, keys, certs, and revisions.
// When refinement starts, each party knows only their own set of
// items; when refinement completes, each party has learned of the
// complete set of items it needs to send, and a count of items it's
// expecting to receive.
//
// For more details on the refinement process, see refiner.cc.
//
//
// Transmission
// ------------
//
// Once the set of items to send has been determined (for keys, certs, and
// revisions) each peer switches into a transmission mode. This mode
// involves walking the revision graph in ancestry-order and sending all
// the items the local peer has which the remote one does not. Since the
// remote and local peers both know all the items which need to be
// transferred (they learned during refinement) they know what to wait for
// and what to send.  The mechanisms of the transmission phase (notably,
// enumerator.cc) simply ensure that things are sent in the proper order,
// and without over-filling the output buffer too much.
//
//
// Shutdown
// --------
//
// After transmission completes, one special command, "bye", is used to
// shut down a connection gracefully. The shutdown sequence based on "bye"
// commands is documented below in session::process_bye_cmd.
//
//
// Note on epochs
// --------------
//
// One refinement and transmission phase preceeds all the others: epochs.
// Epochs are exchanged and compared in order to be sure that further
// refinement and transmission (on certs and revisions) makes sense; they
// are a sort of "immune system" to prevent incompatible databases (say
// between rebuilds due to bugs in monotone) from cross-contaminating.  The
// later refinements are only kicked off *after* all epochs are received
// and compare correctly.
//
//
// Note on dense coding
// --------------------
//
// This protocol is "raw binary" (non-text) because coding density is
// actually important here, and each packet consists of very
// information-dense material that you wouldn't have a hope of typing in,
// or interpreting manually anyways.
//

using std::auto_ptr;
using std::deque;
using std::make_pair;
using std::map;
using std::min;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;
using boost::lexical_cast;

struct server_initiated_sync_request
{
  string what;
  string address;
  string include;
  string exclude;
};
deque<server_initiated_sync_request> server_initiated_sync_requests;
LUAEXT(server_request_sync, )
{
  char const * w = luaL_checkstring(L, 1);
  char const * a = luaL_checkstring(L, 2);
  char const * i = luaL_checkstring(L, 3);
  char const * e = luaL_checkstring(L, 4);
  server_initiated_sync_request request;
  request.what = string(w);
  request.address = string(a);
  request.include = string(i);
  request.exclude = string(e);
  server_initiated_sync_requests.push_back(request);
  return 0;
}

static inline void
require(bool check, string const & context)
{
  if (!check)
    throw bad_decode(F("check of '%s' failed") % context);
}

static void
read_pubkey(string const & in,
            rsa_keypair_id & id,
            rsa_pub_key & pub)
{
  string tmp_id, tmp_key;
  size_t pos = 0;
  extract_variable_length_string(in, tmp_id, pos, "pubkey id");
  extract_variable_length_string(in, tmp_key, pos, "pubkey value");
  id = rsa_keypair_id(tmp_id);
  pub = rsa_pub_key(tmp_key);
}

static void
write_pubkey(rsa_keypair_id const & id,
             rsa_pub_key const & pub,
             string & out)
{
  insert_variable_length_string(id(), out);
  insert_variable_length_string(pub(), out);
}

struct netsync_error
{
  string msg;
  netsync_error(string const & s): msg(s) {}
};

struct
session:
  public refiner_callbacks,
  public enumerator_callbacks
{
  protocol_role role;
  protocol_voice const voice;
  globish our_include_pattern;
  globish our_exclude_pattern;
  globish_matcher our_matcher;

  project_t & project;
  key_store & keys;
  lua_hooks & lua;
  bool use_transport_auth;
  rsa_keypair_id const & signing_key;
  vector<rsa_keypair_id> const & keys_to_push;

  string peer_id;
  shared_ptr<Netxx::StreamBase> str;

  string_queue inbuf;
  // deque of pair<string data, size_t cur_pos>
  deque< pair<string,size_t> > outbuf;
  // the total data stored in outbuf - this is
  // used as a valve to stop too much data
  // backing up
  size_t outbuf_size;

  netcmd cmd;
  bool armed;
  bool arm();

  bool received_remote_key;
  rsa_keypair_id remote_peer_key_name;
  netsync_session_key session_key;
  chained_hmac read_hmac;
  chained_hmac write_hmac;
  bool authenticated;

  time_t last_io_time;
  auto_ptr<ticker> byte_in_ticker;
  auto_ptr<ticker> byte_out_ticker;
  auto_ptr<ticker> cert_in_ticker;
  auto_ptr<ticker> cert_out_ticker;
  auto_ptr<ticker> revision_in_ticker;
  auto_ptr<ticker> revision_out_ticker;
  size_t bytes_in, bytes_out;
  size_t certs_in, certs_out;
  size_t revs_in, revs_out;
  size_t keys_in, keys_out;
  // used to identify this session to the netsync hooks.
  // We can't just use saved_nonce, because that's blank for all
  // anonymous connections and could lead to confusion.
  size_t session_id;
  static size_t session_count;

  // These are read from the server, written to the local database
  vector<revision_id> written_revisions;
  vector<rsa_keypair_id> written_keys;
  vector<cert> written_certs;

  // These are sent to the server
  vector<revision_id> sent_revisions;
  vector<rsa_keypair_id> sent_keys;
  vector<cert> sent_certs;

  id saved_nonce;

  enum
    {
      working_state,
      shutdown_state,
      confirmed_state
    }
    protocol_state;

  bool encountered_error;

  static const int no_error = 200;
  static const int partial_transfer = 211;
  static const int no_transfer = 212;

  static const int not_permitted = 412;
  static const int unknown_key = 422;
  static const int mixing_versions = 432;

  static const int role_mismatch = 512;
  static const int bad_command = 521;

  static const int failed_identification = 532;
  //static const int bad_data = 541;

  int error_code;

  bool set_totals;

  // Interface to refinement.
  refiner epoch_refiner;
  refiner key_refiner;
  refiner cert_refiner;
  refiner rev_refiner;

  // Interface to ancestry grovelling.
  revision_enumerator rev_enumerator;

  // Enumerator_callbacks methods.
  set<file_id> file_items_sent;
  bool process_this_rev(revision_id const & rev);
  bool queue_this_cert(id const & c);
  bool queue_this_file(id const & f);
  void note_file_data(file_id const & f);
  void note_file_delta(file_id const & src, file_id const & dst);
  void note_rev(revision_id const & rev);
  void note_cert(id const & c);

  session(options & opts,
          lua_hooks & lua,
          project_t & project,
          key_store & keys,
          protocol_role role,
          protocol_voice voice,
          globish const & our_include_pattern,
          globish const & our_exclude_pattern,
          string const & peer,
          shared_ptr<Netxx::StreamBase> sock,
          bool initiated_by_server = false);

  virtual ~session();

  id mk_nonce();
  void mark_recent_io();

  void set_session_key(string const & key);
  void set_session_key(rsa_oaep_sha_data const & key_encrypted);

  void setup_client_tickers();
  bool done_all_refinements();
  bool queued_all_items();
  bool received_all_items();
  bool finished_working();
  void maybe_step();
  void maybe_say_goodbye(transaction_guard & guard);

  void note_item_arrived(netcmd_item_type ty, id const & i);
  void maybe_note_epochs_finished();
  void note_item_sent(netcmd_item_type ty, id const & i);

  Netxx::Probe::ready_type which_events() const;
  bool read_some();
  bool write_some();

  void error(int errcode, string const & errmsg);

  void write_netcmd_and_try_flush(netcmd const & cmd);

  // Outgoing queue-writers.
  void queue_bye_cmd(u8 phase);
  void queue_error_cmd(string const & errmsg);
  void queue_done_cmd(netcmd_item_type type, size_t n_items);
  void queue_hello_cmd(rsa_keypair_id const & key_name,
                       rsa_pub_key const & pub_encoded,
                       id const & nonce);
  void queue_anonymous_cmd(protocol_role role,
                           globish const & include_pattern,
                           globish const & exclude_pattern,
                           id const & nonce2);
  void queue_auth_cmd(protocol_role role,
                      globish const & include_pattern,
                      globish const & exclude_pattern,
                      id const & client,
                      id const & nonce1,
                      id const & nonce2,
                      rsa_sha1_signature const & signature);
  void queue_confirm_cmd();
  void queue_refine_cmd(refinement_type ty, merkle_node const & node);
  void queue_data_cmd(netcmd_item_type type,
                      id const & item,
                      string const & dat);
  void queue_delta_cmd(netcmd_item_type type,
                       id const & base,
                       id const & ident,
                       delta const & del);

  // Incoming dispatch-called methods.
  bool process_error_cmd(string const & errmsg);
  bool process_hello_cmd(rsa_keypair_id const & server_keyname,
                         rsa_pub_key const & server_key,
                         id const & nonce);
  bool process_bye_cmd(u8 phase, transaction_guard & guard);
  bool process_anonymous_cmd(protocol_role role,
                             globish const & their_include_pattern,
                             globish const & their_exclude_pattern);
  bool process_auth_cmd(protocol_role role,
                        globish const & their_include_pattern,
                        globish const & their_exclude_pattern,
                        id const & client,
                        id const & nonce1,
                        rsa_sha1_signature const & signature);
  bool process_refine_cmd(refinement_type ty, merkle_node const & node);
  bool process_done_cmd(netcmd_item_type type, size_t n_items);
  bool process_data_cmd(netcmd_item_type type,
                        id const & item,
                        string const & dat);
  bool process_delta_cmd(netcmd_item_type type,
                         id const & base,
                         id const & ident,
                         delta const & del);
  bool process_usher_cmd(utf8 const & msg);

  // The incoming dispatcher.
  bool dispatch_payload(netcmd const & cmd,
                        transaction_guard & guard);

  // Various helpers.
  void assume_corresponding_role(protocol_role their_role);
  void respond_to_confirm_cmd();
  bool data_exists(netcmd_item_type type,
                   id const & item);
  void load_data(netcmd_item_type type,
                 id const & item,
                 string & out);

  void rebuild_merkle_trees(set<branch_name> const & branches);

  void send_all_data(netcmd_item_type ty, set<id> const & items);
  void begin_service();
  bool process(transaction_guard & guard);

  bool initiated_by_server;
};
size_t session::session_count = 0;

session::session(options & opts,
                 lua_hooks & lua,
                 project_t & project,
                 key_store & keys,
                 protocol_role role,
                 protocol_voice voice,
                 globish const & our_include_pattern,
                 globish const & our_exclude_pattern,
                 string const & peer,
                 shared_ptr<Netxx::StreamBase> sock,
                 bool initiated_by_server) :
  role(role),
  voice(voice),
  our_include_pattern(our_include_pattern),
  our_exclude_pattern(our_exclude_pattern),
  our_matcher(our_include_pattern, our_exclude_pattern),
  project(project),
  keys(keys),
  lua(lua),
  use_transport_auth(opts.use_transport_auth),
  signing_key(opts.signing_key),
  keys_to_push(opts.keys_to_push),
  peer_id(peer),
  str(sock),
  inbuf(),
  outbuf_size(0),
  armed(false),
  received_remote_key(false),
  remote_peer_key_name(""),
  session_key(constants::netsync_key_initializer),
  read_hmac(netsync_session_key(constants::netsync_key_initializer),
            use_transport_auth),
  write_hmac(netsync_session_key(constants::netsync_key_initializer),
             use_transport_auth),
  authenticated(false),
  last_io_time(::time(NULL)),
  byte_in_ticker(NULL),
  byte_out_ticker(NULL),
  cert_in_ticker(NULL),
  cert_out_ticker(NULL),
  revision_in_ticker(NULL),
  revision_out_ticker(NULL),
  bytes_in(0), bytes_out(0),
  certs_in(0), certs_out(0),
  revs_in(0), revs_out(0),
  keys_in(0), keys_out(0),
  session_id(++session_count),
  saved_nonce(""),
  protocol_state(working_state),
  encountered_error(false),
  error_code(no_transfer),
  set_totals(false),
  epoch_refiner(epoch_item, voice, *this),
  key_refiner(key_item, voice, *this),
  cert_refiner(cert_item, voice, *this),
  rev_refiner(revision_item, voice, *this),
  rev_enumerator(project, *this),
  initiated_by_server(initiated_by_server)
{}

session::~session()
{
  if (protocol_state == confirmed_state)
    error_code = no_error;
  else if (error_code == no_transfer &&
           (revs_in || revs_out ||
            certs_in || certs_out ||
            keys_in || keys_out))
    error_code = partial_transfer;

  vector<cert> unattached_written_certs;
  map<revision_id, vector<cert> > rev_written_certs;
  for (vector<revision_id>::iterator i = written_revisions.begin();
       i != written_revisions.end(); ++i)
    rev_written_certs.insert(make_pair(*i, vector<cert>()));
  for (vector<cert>::iterator i = written_certs.begin();
       i != written_certs.end(); ++i)
    {
      map<revision_id, vector<cert> >::iterator j;
      j = rev_written_certs.find(revision_id(i->ident));
      if (j == rev_written_certs.end())
        unattached_written_certs.push_back(*i);
      else
        j->second.push_back(*i);
    }

  if (!written_keys.empty()
      || !written_revisions.empty()
      || !written_certs.empty())
    {

      //Keys
      for (vector<rsa_keypair_id>::iterator i = written_keys.begin();
           i != written_keys.end(); ++i)
        {
          lua.hook_note_netsync_pubkey_received(*i, session_id);
        }

      //Revisions
      for (vector<revision_id>::iterator i = written_revisions.begin();
           i != written_revisions.end(); ++i)
        {
          vector<cert> & ctmp(rev_written_certs[*i]);
          set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            certs.insert(make_pair(j->key, make_pair(j->name, j->value)));

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_received(*i, rdat, certs,
                                                  session_id);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_written_certs.begin();
           i != unattached_written_certs.end(); ++i)
        lua.hook_note_netsync_cert_received(revision_id(i->ident), i->key,
                                            i->name, i->value, session_id);
    }

  if (!sent_keys.empty()
      || !sent_revisions.empty()
      || !sent_certs.empty())
    {

      vector<cert> unattached_sent_certs;
      map<revision_id, vector<cert> > rev_sent_certs;
      for (vector<revision_id>::iterator i = sent_revisions.begin();
           i != sent_revisions.end(); ++i)
        rev_sent_certs.insert(make_pair(*i, vector<cert>()));
      for (vector<cert>::iterator i = sent_certs.begin();
           i != sent_certs.end(); ++i)
        {
          map<revision_id, vector<cert> >::iterator j;
          j = rev_sent_certs.find(revision_id(i->ident));
          if (j == rev_sent_certs.end())
            unattached_sent_certs.push_back(*i);
          else
            j->second.push_back(*i);
        }

      //Keys
      for (vector<rsa_keypair_id>::iterator i = sent_keys.begin();
           i != sent_keys.end(); ++i)
        {
          lua.hook_note_netsync_pubkey_sent(*i, session_id);
        }

      //Revisions
      for (vector<revision_id>::iterator i = sent_revisions.begin();
           i != sent_revisions.end(); ++i)
        {
          vector<cert> & ctmp(rev_sent_certs[*i]);
          set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > certs;
          for (vector<cert>::const_iterator j = ctmp.begin();
               j != ctmp.end(); ++j)
            certs.insert(make_pair(j->key, make_pair(j->name, j->value)));

          revision_data rdat;
          project.db.get_revision(*i, rdat);
          lua.hook_note_netsync_revision_sent(*i, rdat, certs,
                                                  session_id);
        }

      //Certs (not attached to a new revision)
      for (vector<cert>::iterator i = unattached_sent_certs.begin();
           i != unattached_sent_certs.end(); ++i)
        lua.hook_note_netsync_cert_sent(revision_id(i->ident), i->key,
                                            i->name, i->value, session_id);
    }

  lua.hook_note_netsync_end(session_id, error_code,
                            bytes_in, bytes_out,
                            certs_in, certs_out,
                            revs_in, revs_out,
                            keys_in, keys_out);
}

bool
session::process_this_rev(revision_id const & rev)
{
  return (rev_refiner.items_to_send.find(rev.inner())
          != rev_refiner.items_to_send.end());
}

bool
session::queue_this_cert(id const & c)
{
  return (cert_refiner.items_to_send.find(c)
          != cert_refiner.items_to_send.end());
}

bool
session::queue_this_file(id const & f)
{
  return file_items_sent.find(file_id(f)) == file_items_sent.end();
}

void
session::note_file_data(file_id const & f)
{
  if (role == sink_role)
    return;
  file_data fd;
  project.db.get_file_version(f, fd);
  queue_data_cmd(file_item, f.inner(), fd.inner()());
  file_items_sent.insert(f);
}

void
session::note_file_delta(file_id const & src, file_id const & dst)
{
  if (role == sink_role)
    return;
  file_delta fdel;
  project.db.get_arbitrary_file_delta(src, dst, fdel);
  queue_delta_cmd(file_item, src.inner(), dst.inner(), fdel.inner());
  file_items_sent.insert(dst);
}

void
session::note_rev(revision_id const & rev)
{
  if (role == sink_role)
    return;
  revision_t rs;
  project.db.get_revision(rev, rs);
  data tmp;
  write_revision(rs, tmp);
  queue_data_cmd(revision_item, rev.inner(), tmp());
  sent_revisions.push_back(rev);
}

void
session::note_cert(id const & c)
{
  if (role == sink_role)
    return;
  revision<cert> cert;
  string str;
  project.db.get_revision_cert(c, cert);
  write_cert(cert.inner(), str);
  queue_data_cmd(cert_item, c, str);
  sent_certs.push_back(cert.inner());
}


id
session::mk_nonce()
{
  I(this->saved_nonce().empty());
  char buf[constants::merkle_hash_length_in_bytes];
  keys.get_rng().randomize(reinterpret_cast<Botan::byte *>(buf),
                             constants::merkle_hash_length_in_bytes);
  this->saved_nonce = id(string(buf, buf + constants::merkle_hash_length_in_bytes));
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void
session::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

void
session::set_session_key(string const & key)
{
  session_key = netsync_session_key(key);
  read_hmac.set_key(session_key);
  write_hmac.set_key(session_key);
}

void
session::set_session_key(rsa_oaep_sha_data const & hmac_key_encrypted)
{
  if (use_transport_auth)
    {
      string hmac_key;
      keys.decrypt_rsa(signing_key, hmac_key_encrypted, hmac_key);
      set_session_key(hmac_key);
    }
}

void
session::setup_client_tickers()
{
  // xgettext: please use short message and try to avoid multibytes chars
  byte_in_ticker.reset(new ticker(N_("bytes in"), ">", 1024, true));
  // xgettext: please use short message and try to avoid multibytes chars
  byte_out_ticker.reset(new ticker(N_("bytes out"), "<", 1024, true));
  if (role == sink_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      cert_in_ticker.reset(new ticker(N_("certs in"), "c", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(N_("revs in"), "r", 1));
    }
  else if (role == source_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      cert_out_ticker.reset(new ticker(N_("certs out"), "C", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(N_("revs out"), "R", 1));
    }
  else
    {
      I(role == source_and_sink_role);
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(N_("revs in"), "r", 1));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(N_("revs out"), "R", 1));
    }
}

bool
session::done_all_refinements()
{
  bool all = rev_refiner.done
    && cert_refiner.done
    && key_refiner.done
    && epoch_refiner.done;

  if (all && !set_totals)
    {
      if (cert_out_ticker.get())
        cert_out_ticker->set_total(cert_refiner.items_to_send.size());

      if (revision_out_ticker.get())
        revision_out_ticker->set_total(rev_refiner.items_to_send.size());

      if (cert_in_ticker.get())
        cert_in_ticker->set_total(cert_refiner.items_to_receive);

      if (revision_in_ticker.get())
        revision_in_ticker->set_total(rev_refiner.items_to_receive);

      set_totals = true;
    }
  return all;
}



bool
session::received_all_items()
{
  if (role == source_role)
    return true;
  bool all = rev_refiner.items_to_receive == 0
    && cert_refiner.items_to_receive == 0
    && key_refiner.items_to_receive == 0
    && epoch_refiner.items_to_receive == 0;
  return all;
}

bool
session::finished_working()
{
  bool all = done_all_refinements()
    && received_all_items()
    && queued_all_items()
    && rev_enumerator.done();
  return all;
}

bool
session::queued_all_items()
{
  if (role == sink_role)
    return true;
  bool all = rev_refiner.items_to_send.empty()
    && cert_refiner.items_to_send.empty()
    && key_refiner.items_to_send.empty()
    && epoch_refiner.items_to_send.empty();
  return all;
}


void
session::maybe_note_epochs_finished()
{
  // Maybe there are outstanding epoch requests.
  // These only matter if we're in sink or source-and-sink mode.
  if (!(epoch_refiner.items_to_receive == 0) && !(role == source_role))
    return;

  // And maybe we haven't even finished the refinement.
  if (!epoch_refiner.done)
    return;

  // If we ran into an error -- say a mismatched epoch -- don't do any
  // further refinements.
  if (encountered_error)
    return;

  // But otherwise, we're ready to go. Start the next
  // set of refinements.
  if (voice == client_voice)
    {
      L(FL("epoch refinement finished; beginning other refinements"));
      key_refiner.begin_refinement();
      cert_refiner.begin_refinement();
      rev_refiner.begin_refinement();
    }
  else
    L(FL("epoch refinement finished"));
}

static void
decrement_if_nonzero(netcmd_item_type ty,
                     size_t & n)
{
  if (n == 0)
    {
      string typestr;
      netcmd_item_type_to_string(ty, typestr);
      E(false, F("underflow on count of %s items to receive") % typestr);
    }
  --n;
  if (n == 0)
    {
      string typestr;
      netcmd_item_type_to_string(ty, typestr);
      L(FL("count of %s items to receive has reached zero") % typestr);
    }
}

void
session::note_item_arrived(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case cert_item:
      decrement_if_nonzero(ty, cert_refiner.items_to_receive);
      if (cert_in_ticker.get() != NULL)
        ++(*cert_in_ticker);
      ++certs_in;
      break;
    case revision_item:
      decrement_if_nonzero(ty, rev_refiner.items_to_receive);
      if (revision_in_ticker.get() != NULL)
        ++(*revision_in_ticker);
      ++revs_in;
      break;
    case key_item:
      decrement_if_nonzero(ty, key_refiner.items_to_receive);
      ++keys_in;
      break;
    case epoch_item:
      decrement_if_nonzero(ty, epoch_refiner.items_to_receive);
      break;
    default:
      // No ticker for other things.
      break;
    }
}



void
session::note_item_sent(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case cert_item:
      cert_refiner.items_to_send.erase(ident);
      if (cert_out_ticker.get() != NULL)
        ++(*cert_out_ticker);
      ++certs_out;
      break;
    case revision_item:
      rev_refiner.items_to_send.erase(ident);
      if (revision_out_ticker.get() != NULL)
        ++(*revision_out_ticker);
      ++revs_out;
      break;
    case key_item:
      key_refiner.items_to_send.erase(ident);
      ++keys_out;
      break;
    case epoch_item:
      epoch_refiner.items_to_send.erase(ident);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

void
session::write_netcmd_and_try_flush(netcmd const & cmd)
{
  if (!encountered_error)
  {
    string buf;
    cmd.write(buf, write_hmac);
    outbuf.push_back(make_pair(buf, 0));
    outbuf_size += buf.size();
  }
  else
    L(FL("dropping outgoing netcmd (because we're in error unwind mode)"));
  // FIXME: this helps keep the protocol pipeline full but it seems to
  // interfere with initial and final sequences. careful with it.
  // write_some();
  // read_some();
}

// This method triggers a special "error unwind" mode to netsync.  In this
// mode, all received data is ignored, and no new data is queued.  We simply
// stay connected long enough for the current write buffer to be flushed, to
// ensure that our peer receives the error message.
// Affects read_some, write_some, and process .
void
session::error(int errcode, string const & errmsg)
{
  error_code = errcode;
  throw netsync_error(errmsg);
}

Netxx::Probe::ready_type
session::which_events() const
{
  // Only ask to read if we're not armed.
  if (outbuf.empty())
    {
      if (inbuf.size() < constants::netcmd_maxsz && !armed)
        return Netxx::Probe::ready_read | Netxx::Probe::ready_oobd;
      else
        return Netxx::Probe::ready_oobd;
    }
  else
    {
      if (inbuf.size() < constants::netcmd_maxsz && !armed)
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
  Netxx::signed_size_type count = str->read(tmp, sizeof(tmp));
  if (count > 0)
    {
      L(FL("read %d bytes from fd %d (peer %s)")
        % count % str->get_socketfd() % peer_id);
      if (encountered_error)
        {
          L(FL("in error unwind mode, so throwing them into the bit bucket"));
          return true;
        }
      inbuf.append(tmp,count);
      mark_recent_io();
      if (byte_in_ticker.get() != NULL)
        (*byte_in_ticker) += count;
      bytes_in += count;
      return true;
    }
  else
    return false;
}

bool
session::write_some()
{
  I(!outbuf.empty());
  size_t writelen = outbuf.front().first.size() - outbuf.front().second;
  Netxx::signed_size_type count = str->write(outbuf.front().first.data() + outbuf.front().second,
                                            min(writelen,
                                            constants::bufsz));
  if (count > 0)
    {
      if ((size_t)count == writelen)
        {
          outbuf_size -= outbuf.front().first.size();
          outbuf.pop_front();
        }
      else
        {
          outbuf.front().second += count;
        }
      L(FL("wrote %d bytes to fd %d (peer %s)")
        % count % str->get_socketfd() % peer_id);
      mark_recent_io();
      if (byte_out_ticker.get() != NULL)
        (*byte_out_ticker) += count;
      bytes_out += count;
      if (encountered_error && outbuf.empty())
        {
          // we've flushed our error message, so it's time to get out.
          L(FL("finished flushing output queue in error unwind mode, disconnecting"));
          return false;
        }
      return true;
    }
  else
    return false;
}

// senders

void
session::queue_error_cmd(string const & errmsg)
{
  L(FL("queueing 'error' command"));
  netcmd cmd;
  cmd.write_error_cmd(errmsg);
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_bye_cmd(u8 phase)
{
  L(FL("queueing 'bye' command, phase %d")
    % static_cast<size_t>(phase));
  netcmd cmd;
  cmd.write_bye_cmd(phase);
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_done_cmd(netcmd_item_type type,
                        size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("queueing 'done' command for %s (%d items)")
    % typestr % n_items);
  netcmd cmd;
  cmd.write_done_cmd(type, n_items);
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_hello_cmd(rsa_keypair_id const & key_name,
                         rsa_pub_key const & pub,
                         id const & nonce)
{
  if (use_transport_auth)
    cmd.write_hello_cmd(key_name, pub, nonce);
  else
    cmd.write_hello_cmd(key_name, rsa_pub_key(), nonce);
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_anonymous_cmd(protocol_role role,
                             globish const & include_pattern,
                             globish const & exclude_pattern,
                             id const & nonce2)
{
  netcmd cmd;
  rsa_oaep_sha_data hmac_key_encrypted;
  if (use_transport_auth)
    project.db.encrypt_rsa(remote_peer_key_name, nonce2(), hmac_key_encrypted);
  cmd.write_anonymous_cmd(role, include_pattern, exclude_pattern,
                          hmac_key_encrypted);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void
session::queue_auth_cmd(protocol_role role,
                        globish const & include_pattern,
                        globish const & exclude_pattern,
                        id const & client,
                        id const & nonce1,
                        id const & nonce2,
                        rsa_sha1_signature const & signature)
{
  netcmd cmd;
  rsa_oaep_sha_data hmac_key_encrypted;
  I(use_transport_auth);
  project.db.encrypt_rsa(remote_peer_key_name, nonce2(), hmac_key_encrypted);
  cmd.write_auth_cmd(role, include_pattern, exclude_pattern, client,
                     nonce1, hmac_key_encrypted, signature);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void
session::queue_confirm_cmd()
{
  netcmd cmd;
  cmd.write_confirm_cmd();
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_refine_cmd(refinement_type ty, merkle_node const & node)
{
  string typestr;
  hexenc<prefix> hpref;
  node.get_hex_prefix(hpref);
  netcmd_item_type_to_string(node.type, typestr);
  L(FL("queueing refinement %s of %s node '%s', level %d")
    % (ty == refinement_query ? "query" : "response")
    % typestr % hpref() % static_cast<int>(node.level));
  netcmd cmd;
  cmd.write_refine_cmd(ty, node);
  write_netcmd_and_try_flush(cmd);
}

void
session::queue_data_cmd(netcmd_item_type type,
                        id const & item,
                        string const & dat)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;

  if (global_sanity.debug_p())
    encode_hexenc(item, hid);

  if (role == sink_role)
    {
      L(FL("not queueing %s data for '%s' as we are in pure sink role")
        % typestr % hid());
      return;
    }

  L(FL("queueing %d bytes of data for %s item '%s'")
    % dat.size() % typestr % hid());

  netcmd cmd;
  // TODO: This pair of functions will make two copies of a large
  // file, the first in cmd.write_data_cmd, and the second in
  // write_netcmd_and_try_flush when the data is copied from the
  // cmd.payload variable to the string buffer for output.  This
  // double copy should be collapsed out, it may be better to use
  // a string_queue for output as well as input, as that will reduce
  // the amount of mallocs that happen when the string queue is large
  // enough to just store the data.
  cmd.write_data_cmd(type, item, dat);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, item);
}

void
session::queue_delta_cmd(netcmd_item_type type,
                         id const & base,
                         id const & ident,
                         delta const & del)
{
  I(type == file_item);
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid,
             ident_hid;

  if (global_sanity.debug_p())
    {
      encode_hexenc(base, base_hid);
      encode_hexenc(ident, ident_hid);
    }

  if (role == sink_role)
    {
      L(FL("not queueing %s delta '%s' -> '%s' as we are in pure sink role")
        % typestr % base_hid() % ident_hid());
      return;
    }

  L(FL("queueing %s delta '%s' -> '%s'")
    % typestr % base_hid() % ident_hid());
  netcmd cmd;
  cmd.write_delta_cmd(type, base, ident, del);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, ident);
}


// processors

bool
session::process_error_cmd(string const & errmsg)
{
  // "xxx string" with xxx being digits means there's an error code
  if (errmsg.size() > 4 && errmsg.substr(3,1) == " ")
    {
      try
        {
          int err = boost::lexical_cast<int>(errmsg.substr(0,3));
          if (err >= 100)
            {
              error_code = err;
              throw bad_decode(F("received network error: %s")
                               % errmsg.substr(4));
            }
        }
      catch (boost::bad_lexical_cast)
        { // ok, so it wasn't a number
        }
    }
  throw bad_decode(F("received network error: %s") % errmsg);
}

static const var_domain known_servers_domain = var_domain("known-servers");

bool
session::process_hello_cmd(rsa_keypair_id const & their_keyname,
                           rsa_pub_key const & their_key,
                           id const & nonce)
{
  I(!this->received_remote_key);
  I(this->saved_nonce().empty());

  if (use_transport_auth)
    {
      id their_key_hash;
      key_hash_code(their_keyname, their_key, their_key_hash);
      var_value printable_key_hash(encode_hexenc(their_key_hash()));
      L(FL("server key has name %s, hash %s")
        % their_keyname % printable_key_hash);
      var_key their_key_key(known_servers_domain, var_name(peer_id));
      if (project.db.var_exists(their_key_key))
        {
          var_value expected_key_hash;
          project.db.get_var(their_key_key, expected_key_hash);
          if (expected_key_hash != printable_key_hash)
            {
              P(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                  "@ WARNING: SERVER IDENTIFICATION HAS CHANGED              @\n"
                  "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                  "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY\n"
                  "it is also possible that the server key has just been changed\n"
                  "remote host sent key %s\n"
                  "I expected %s\n"
                  "'%s unset %s %s' overrides this check")
                % printable_key_hash
                % expected_key_hash
                % ui.prog_name % their_key_key.first % their_key_key.second);
              E(false, F("server key changed"));
            }
        }
      else
        {
          P(F("first time connecting to server %s\n"
              "I'll assume it's really them, but you might want to double-check\n"
              "their key's fingerprint: %s")
            % peer_id
            % printable_key_hash);
          project.db.set_var(their_key_key, printable_key_hash);
        }

      if (project.db.public_key_exists(their_keyname))
        {
          rsa_pub_key tmp;
          project.db.get_key(their_keyname, tmp);

          E(keys_match(their_keyname, tmp, their_keyname, their_key),
            F("the server sent a key with the key id '%s'\n"
              "which is already in use in your database. you may want to execute\n"
              "  %s dropkey %s\n"
              "on your local database before you run this command again,\n"
              "assuming that key currently present in your database does NOT have\n"
              "a private counterpart (or in other words, is one of YOUR keys)")
            % their_keyname % ui.prog_name % their_keyname);
        }
      else
        {
          // this should now always return true since we just checked
          // for the existence of this particular key
          I(project.db.put_key(their_keyname, their_key));
          W(F("saving public key for %s to database") % their_keyname);
        }

      {
        hexenc<id> hnonce;
        encode_hexenc(nonce, hnonce);
        L(FL("received 'hello' netcmd from server '%s' with nonce '%s'")
          % printable_key_hash % hnonce);
      }

      I(project.db.public_key_exists(their_key_hash));

      // save their identity
      this->received_remote_key = true;
      this->remote_peer_key_name = their_keyname;
    }

  // clients always include in the synchronization set, every branch that the
  // user requested
  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  for (set<branch_name>::const_iterator i = all_branches.begin();
      i != all_branches.end(); i++)
    {
      if (our_matcher((*i)()))
        ok_branches.insert(*i);
    }
  rebuild_merkle_trees(ok_branches);

  if (!initiated_by_server)
    setup_client_tickers();

  if (use_transport_auth && signing_key() != "")
    {
      // get our key pair
      load_key_pair(keys, signing_key);

      // make a signature with it;
      // this also ensures our public key is in the database
      rsa_sha1_signature sig;
      keys.make_signature(project.db, signing_key, nonce(), sig);

      // get the hash identifier for our pubkey
      rsa_pub_key our_pub;
      project.db.get_key(signing_key, our_pub);
      id our_key_hash_raw;
      key_hash_code(signing_key, our_pub, our_key_hash_raw);

      // make a new nonce of our own and send off the 'auth'
      queue_auth_cmd(this->role, our_include_pattern, our_exclude_pattern,
                     our_key_hash_raw, nonce, mk_nonce(), sig);
    }
  else
    {
      queue_anonymous_cmd(this->role, our_include_pattern,
                          our_exclude_pattern, mk_nonce());
    }

  lua.hook_note_netsync_start(session_id, "client", this->role,
                              peer_id, their_keyname,
                              our_include_pattern, our_exclude_pattern);
  return true;
}

bool
session::process_anonymous_cmd(protocol_role their_role,
                               globish const & their_include_pattern,
                               globish const & their_exclude_pattern)
{
  // Internally netsync thinks in terms of sources and sinks. Users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // We therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: The "role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.
  //

  lua.hook_note_netsync_start(session_id, "server", their_role,
                              peer_id, rsa_keypair_id(),
                              their_include_pattern, their_exclude_pattern);

  // Client must be a sink and server must be a source (anonymous
  // read-only), unless transport auth is disabled.
  //
  // If running in no-transport-auth mode, we operate anonymously and
  // permit adoption of any role.

  if (use_transport_auth)
    {
      if (their_role != sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                F("rejected attempt at anonymous connection for write").str());
        }

      if (this->role == sink_role)
        {
          this->saved_nonce = id("");
          error(role_mismatch,
                F("rejected attempt at anonymous connection while running as sink").str());
        }
    }

  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);
  for (set<branch_name>::const_iterator i = all_branches.begin();
      i != all_branches.end(); i++)
    {
      if (their_matcher((*i)()))
        {
          if (use_transport_auth &&
              !lua.hook_get_netsync_read_permitted((*i)()))
            {
              error(not_permitted,
                    (F("anonymous access to branch '%s' denied by server")
                     % *i).str());
            }
          else
            ok_branches.insert(*i);
        }
    }

  if (use_transport_auth)
    {
      P(F("allowed anonymous read permission for '%s' excluding '%s'")
        % their_include_pattern % their_exclude_pattern);
      this->role = source_role;
    }
  else
    {
      P(F("allowed anonymous read/write permission for '%s' excluding '%s'")
        % their_include_pattern % their_exclude_pattern);
      assume_corresponding_role(their_role);
    }

  rebuild_merkle_trees(ok_branches);

  this->remote_peer_key_name = rsa_keypair_id("");
  this->authenticated = true;
  return true;
}

void
session::assume_corresponding_role(protocol_role their_role)
{
  // Assume the (possibly degraded) opposite role.
  switch (their_role)
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
}

bool
session::process_auth_cmd(protocol_role their_role,
                          globish const & their_include_pattern,
                          globish const & their_exclude_pattern,
                          id const & client,
                          id const & nonce1,
                          rsa_sha1_signature const & signature)
{
  I(!this->received_remote_key);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);

  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);

  if (!project.db.public_key_exists(client))
    {
      // If it's not in the db, it still could be in the keystore if we
      // have the private key that goes with it.
      rsa_keypair_id their_key_id;
      keypair their_keypair;
      if (keys.maybe_get_key_pair(client, their_key_id, their_keypair))
        project.db.put_key(their_key_id, their_keypair.pub);
      else
        {
          return process_anonymous_cmd(their_role,
                                       their_include_pattern,
                                       their_exclude_pattern);
          /*
          this->saved_nonce = id("");

          lua.hook_note_netsync_start(session_id, "server", their_role,
                                      peer_id, rsa_keypair_id("-unknown-"),
                                      their_include_pattern,
                                      their_exclude_pattern);
          error(unknown_key,
                (F("remote public key hash '%s' is unknown")
                 % encode_hexenc(client())).str());
          */
        }
    }

  // Get their public key.
  rsa_keypair_id their_id;
  rsa_pub_key their_key;
  project.db.get_pubkey(client, their_id, their_key);

  lua.hook_note_netsync_start(session_id, "server", their_role,
                              peer_id, their_id,
                              their_include_pattern, their_exclude_pattern);

  // Check that they replied with the nonce we asked for.
  if (!(nonce1 == this->saved_nonce))
    {
      this->saved_nonce = id("");
      error(failed_identification,
            F("detected replay attack in auth netcmd").str());
    }

  // Internally netsync thinks in terms of sources and sinks. users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // We therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: The "their_role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.

  // Client as sink, server as source (reading).

  if (their_role == sink_role || their_role == source_and_sink_role)
    {
      if (this->role != source_role && this->role != source_and_sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' read permission for '%s' excluding '%s' while running as pure sink")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }
    }

  set<branch_name> all_branches, ok_branches;
  project.get_branch_list(all_branches);
  for (set<branch_name>::const_iterator i = all_branches.begin();
       i != all_branches.end(); i++)
    {
      if (their_matcher((*i)()))
        {
          if (!lua.hook_get_netsync_read_permitted((*i)(), their_id))
            {
              error(not_permitted,
                    (F("denied '%s' read permission for '%s' excluding '%s' because of branch '%s'")
                     % their_id % their_include_pattern % their_exclude_pattern % *i).str());
            }
          else
            ok_branches.insert(*i);
        }
    }

  // If we're source_and_sink_role, continue even with no branches readable
  // eg. serve --db=empty.db
  P(F("allowed '%s' read permission for '%s' excluding '%s'")
    % their_id % their_include_pattern % their_exclude_pattern);

  // Client as source, server as sink (writing).

  if (their_role == source_role || their_role == source_and_sink_role)
    {
      if (this->role != sink_role && this->role != source_and_sink_role)
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' write permission for '%s' excluding '%s' while running as pure source")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }

      if (!lua.hook_get_netsync_write_permitted(their_id))
        {
          this->saved_nonce = id("");
          error(not_permitted,
                (F("denied '%s' write permission for '%s' excluding '%s'")
                 % their_id % their_include_pattern % their_exclude_pattern).str());
        }

      P(F("allowed '%s' write permission for '%s' excluding '%s'")
        % their_id % their_include_pattern % their_exclude_pattern);
    }

  rebuild_merkle_trees(ok_branches);

  this->received_remote_key = true;

  // Check the signature.
  if (project.db.check_signature(their_id, nonce1(), signature) == cert_ok)
    {
      // Get our private key and sign back.
      L(FL("client signature OK, accepting authentication"));
      this->authenticated = true;
      this->remote_peer_key_name = their_id;

      assume_corresponding_role(their_role);
      return true;
    }
  else
    {
      error(failed_identification, (F("bad client signature")).str());
    }
  return false;
}

bool
session::process_refine_cmd(refinement_type ty, merkle_node const & node)
{
  string typestr;
  netcmd_item_type_to_string(node.type, typestr);
  L(FL("processing refine cmd for %s node at level %d")
    % typestr % node.level);

  switch (node.type)
    {
    case file_item:
      W(F("Unexpected 'refine' command on non-refined item type"));
      break;

    case key_item:
      key_refiner.process_refinement_command(ty, node);
      break;

    case revision_item:
      rev_refiner.process_refinement_command(ty, node);
      break;

    case cert_item:
      cert_refiner.process_refinement_command(ty, node);
      break;

    case epoch_item:
      epoch_refiner.process_refinement_command(ty, node);
      break;
    }
  return true;
}

bool
session::process_bye_cmd(u8 phase,
                         transaction_guard & guard)
{

// Ideal shutdown
// ~~~~~~~~~~~~~~~
//
//             I/O events                 state transitions
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   ~~~~~~~~~~~~~~~~~~~
//                                        client: C_WORKING
//                                        server: S_WORKING
// 0. [refinement, data, deltas, etc.]
//                                        client: C_SHUTDOWN
//                                        (client checkpoints here)
// 1. client -> "bye 0"
// 2.           "bye 0"  -> server
//                                        server: S_SHUTDOWN
//                                        (server checkpoints here)
// 3.           "bye 1"  <- server
// 4. client <- "bye 1"
//                                        client: C_CONFIRMED
// 5. client -> "bye 2"
// 6.           "bye 2"  -> server
//                                        server: S_CONFIRMED
// 7. [server drops connection]
//
//
// Affects of I/O errors or disconnections
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//   C_WORKING: report error and fault
//   S_WORKING: report error and recover
//  C_SHUTDOWN: report error and fault
//  S_SHUTDOWN: report success and recover
//              (and warn that client might falsely see error)
// C_CONFIRMED: report success
// S_CONFIRMED: report success

  switch (phase)
    {
    case 0:
      if (voice == server_voice &&
          protocol_state == working_state)
        {
          protocol_state = shutdown_state;
          guard.do_checkpoint();
          queue_bye_cmd(1);
        }
      else
        error(bad_command, "unexpected bye phase 0 received");
      break;

    case 1:
      if (voice == client_voice &&
          protocol_state == shutdown_state)
        {
          protocol_state = confirmed_state;
          queue_bye_cmd(2);
        }
      else
        error(bad_command, "unexpected bye phase 1 received");
      break;

    case 2:
      if (voice == server_voice &&
          protocol_state == shutdown_state)
        {
          protocol_state = confirmed_state;
          return false;
        }
      else
        error(bad_command, "unexpected bye phase 2 received");
      break;

    default:
      error(bad_command, (F("unknown bye phase %d received") % phase).str());
    }

  return true;
}

bool
session::process_done_cmd(netcmd_item_type type, size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("received 'done' command for %s (%s items)") % typestr % n_items);
  switch (type)
    {
    case file_item:
      W(F("Unexpected 'done' command on non-refined item type"));
      break;

    case key_item:
      key_refiner.process_done_command(n_items);
      if (key_refiner.done && role != sink_role)
        send_all_data(key_item, key_refiner.items_to_send);
      break;

    case revision_item:
      rev_refiner.process_done_command(n_items);
      break;

    case cert_item:
      cert_refiner.process_done_command(n_items);
      break;

    case epoch_item:
      epoch_refiner.process_done_command(n_items);
      if (epoch_refiner.done)
        {
          send_all_data(epoch_item, epoch_refiner.items_to_send);
          maybe_note_epochs_finished();
        }
      break;
    }
  return true;
}

void
session::respond_to_confirm_cmd()
{
  epoch_refiner.begin_refinement();
}

bool
session::data_exists(netcmd_item_type type,
                     id const & item)
{
  switch (type)
    {
    case key_item:
      return key_refiner.local_item_exists(item)
        || project.db.public_key_exists(item);
    case file_item:
      return project.db.file_version_exists(file_id(item));
    case revision_item:
      return rev_refiner.local_item_exists(item)
        || project.db.revision_exists(revision_id(item));
    case cert_item:
      return cert_refiner.local_item_exists(item)
        || project.db.revision_cert_exists(revision_id(item));
    case epoch_item:
      return epoch_refiner.local_item_exists(item)
        || project.db.epoch_exists(epoch_id(item));
    }
  return false;
}

void
session::load_data(netcmd_item_type type,
                   id const & item,
                   string & out)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  if (!data_exists(type, item))
    throw bad_decode(F("%s with hash '%s' does not exist in our database")
                     % typestr % hitem());

  switch (type)
    {
    case epoch_item:
      {
        branch_name branch;
        epoch_data epoch;
        project.db.get_epoch(epoch_id(item), branch, epoch);
        write_epoch(branch, epoch, out);
      }
      break;
    case key_item:
      {
        rsa_keypair_id keyid;
        rsa_pub_key pub;
        project.db.get_pubkey(item, keyid, pub);
        L(FL("public key '%s' is also called '%s'") % hitem() % keyid);
        write_pubkey(keyid, pub, out);
        sent_keys.push_back(keyid);
      }
      break;

    case revision_item:
      {
        revision_data mdat;
        data dat;
        project.db.get_revision(revision_id(item), mdat);
        out = mdat.inner()();
      }
      break;

    case file_item:
      {
        file_data fdat;
        data dat;
        project.db.get_file_version(file_id(item), fdat);
        out = fdat.inner()();
      }
      break;

    case cert_item:
      {
        revision<cert> c;
        project.db.get_revision_cert(item, c);
        string tmp;
        write_cert(c.inner(), out);
      }
      break;
    }
}

bool
session::process_data_cmd(netcmd_item_type type,
                          id const & item,
                          string const & dat)
{
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  note_item_arrived(type, item);
  if (data_exists(type, item))
    {
      L(FL("%s '%s' already exists in our database") % typestr % hitem());
      if (type == epoch_item)
        maybe_note_epochs_finished();
      return true;
    }

  switch (type)
    {
    case epoch_item:
      {
        branch_name branch;
        epoch_data epoch;
        read_epoch(dat, branch, epoch);
        L(FL("received epoch %s for branch %s")
          % encode_hexenc(epoch.inner()()) % branch);
        map<branch_name, epoch_data> epochs;
        project.db.get_epochs(epochs);
        map<branch_name, epoch_data>::const_iterator i;
        i = epochs.find(branch);
        if (i == epochs.end())
          {
            L(FL("branch %s has no epoch; setting epoch to %s")
              % branch % encode_hexenc(epoch.inner()()));
            project.db.set_epoch(branch, epoch);
          }
        else
          {
            L(FL("branch %s already has an epoch; checking") % branch);
            // If we get here, then we know that the epoch must be
            // different, because if it were the same then the
            // if (epoch_exists()) branch up above would have been taken.
            // if somehow this is wrong, then we have broken epoch
            // hashing or something, which is very dangerous, so play it
            // safe...
            I(!(i->second == epoch));

            // It is safe to call 'error' here, because if we get here,
            // then the current netcmd packet cannot possibly have
            // written anything to the database.
            error(mixing_versions,
                  (F("Mismatched epoch on branch %s."
                     " Server has '%s', client has '%s'.")
                   % branch
                   % encode_hexenc((voice == server_voice
                                    ? i->second: epoch).inner()())
                   % encode_hexenc((voice == server_voice
                                    ? epoch : i->second).inner()())).str());
          }
      }
      maybe_note_epochs_finished();
      break;

    case key_item:
      {
        rsa_keypair_id keyid;
        rsa_pub_key pub;
        read_pubkey(dat, keyid, pub);
        id tmp;
        key_hash_code(keyid, pub, tmp);
        if (! (tmp == item))
          {
            throw bad_decode(F("hash check failed for public key '%s' (%s);"
                               " wanted '%s' got '%s'")
                               % hitem() % keyid % hitem()
                               % encode_hexenc(tmp()));
          }
        if (project.db.put_key(keyid, pub))
          written_keys.push_back(keyid);
        else
          error(partial_transfer,
                (F("Received duplicate key %s") % keyid).str());
      }
      break;

    case cert_item:
      {
        cert c;
        read_cert(dat, c);
        id tmp;
        cert_hash_code(c, tmp);
        if (! (tmp == item))
          throw bad_decode(F("hash check failed for revision cert '%s'") % hitem());
        if (project.db.put_revision_cert(revision<cert>(c)))
          written_certs.push_back(c);
      }
      break;

    case revision_item:
      {
        L(FL("received revision '%s'") % hitem());
        if (project.db.put_revision(revision_id(item), revision_data(dat)))
          written_revisions.push_back(revision_id(item));
      }
      break;

    case file_item:
      {
        L(FL("received file '%s'") % hitem());
        project.db.put_file(file_id(item), file_data(dat));
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

  pair<id,id> id_pair = make_pair(base, ident);

  note_item_arrived(type, ident);

  switch (type)
    {
    case file_item:
      {
        file_id src_file(base), dst_file(ident);
        project.db.put_file_version(src_file, dst_file, file_delta(del));
      }
      break;

    default:
      L(FL("ignoring delta received for item type %s") % typestr);
      break;
    }
  return true;
}

bool
session::process_usher_cmd(utf8 const & msg)
{
  if (msg().size())
    {
      if (msg()[0] == '!')
        P(F("Received warning from usher: %s") % msg().substr(1));
      else
        L(FL("Received greeting from usher: %s") % msg().substr(1));
    }
  netcmd cmdout;
  cmdout.write_usher_reply_cmd(utf8(peer_id), our_include_pattern);
  write_netcmd_and_try_flush(cmdout);
  L(FL("Sent reply."));
  return true;
}


void
session::send_all_data(netcmd_item_type ty, set<id> const & items)
{
  string typestr;
  netcmd_item_type_to_string(ty, typestr);

  // Use temporary; passed arg will be invalidated during iteration.
  set<id> tmp = items;

  for (set<id>::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      if (data_exists(ty, *i))
        {
          string out;
          load_data(ty, *i, out);
          queue_data_cmd(ty, *i, out);
        }
    }
}

bool
session::dispatch_payload(netcmd const & cmd,
                          transaction_guard & guard)
{

  switch (cmd.get_cmd_code())
    {

    case error_cmd:
      {
        string errmsg;
        cmd.read_error_cmd(errmsg);
        return process_error_cmd(errmsg);
      }
      break;

    case hello_cmd:
      require(! authenticated, "hello netcmd received when not authenticated");
      require(voice == client_voice, "hello netcmd received in client voice");
      {
        rsa_keypair_id server_keyname;
        rsa_pub_key server_key;
        id nonce;
        cmd.read_hello_cmd(server_keyname, server_key, nonce);
        return process_hello_cmd(server_keyname, server_key, nonce);
      }
      break;

    case bye_cmd:
      require(authenticated, "bye netcmd received when not authenticated");
      {
        u8 phase;
        cmd.read_bye_cmd(phase);
        return process_bye_cmd(phase, guard);
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
        globish their_include_pattern, their_exclude_pattern;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_anonymous_cmd(role, their_include_pattern, their_exclude_pattern, hmac_key_encrypted);
        L(FL("received 'anonymous' netcmd from client for pattern '%s' excluding '%s' "
            "in %s mode\n")
          % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink"))));

        set_session_key(hmac_key_encrypted);
        if (!process_anonymous_cmd(role, their_include_pattern, their_exclude_pattern))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case auth_cmd:
      require(! authenticated, "auth netcmd received when not authenticated");
      require(voice == server_voice, "auth netcmd received in server voice");
      {
        protocol_role role;
        rsa_sha1_signature signature;
        globish their_include_pattern, their_exclude_pattern;
        id client, nonce1, nonce2;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                          client, nonce1, hmac_key_encrypted, signature);

        hexenc<id> their_key_hash;
        encode_hexenc(client, their_key_hash);
        hexenc<id> hnonce1;
        encode_hexenc(nonce1, hnonce1);

        L(FL("received 'auth(hmac)' netcmd from client '%s' for pattern '%s' "
            "exclude '%s' in %s mode with nonce1 '%s'\n")
          % their_key_hash % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink")))
          % hnonce1);

        set_session_key(hmac_key_encrypted);

        if (!process_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                              client, nonce1, signature))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case confirm_cmd:
      require(! authenticated, "confirm netcmd received when not authenticated");
      require(voice == client_voice, "confirm netcmd received in client voice");
      {
        string signature;
        cmd.read_confirm_cmd();
        this->authenticated = true;
        respond_to_confirm_cmd();
        return true;
      }
      break;

    case refine_cmd:
      require(authenticated, "refine netcmd received when authenticated");
      {
        merkle_node node;
        refinement_type ty;
        cmd.read_refine_cmd(ty, node);
        return process_refine_cmd(ty, node);
      }
      break;

    case done_cmd:
      require(authenticated, "done netcmd received when not authenticated");
      {
        size_t n_items;
        netcmd_item_type type;
        cmd.read_done_cmd(type, n_items);
        return process_done_cmd(type, n_items);
      }
      break;

    case data_cmd:
      require(authenticated, "data netcmd received when not authenticated");
      require(role == sink_role ||
              role == source_and_sink_role,
              "data netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id item;
        string dat;
        cmd.read_data_cmd(type, item, dat);
        return process_data_cmd(type, item, dat);
      }
      break;

    case delta_cmd:
      require(authenticated, "delta netcmd received when not authenticated");
      require(role == sink_role ||
              role == source_and_sink_role,
              "delta netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id base, ident;
        delta del;
        cmd.read_delta_cmd(type, base, ident, del);
        return process_delta_cmd(type, base, ident, del);
      }
      break;

    case usher_cmd:
      {
        utf8 greeting;
        cmd.read_usher_cmd(greeting);
        return process_usher_cmd(greeting);
      }
      break;

    case usher_reply_cmd:
      return false; // Should not happen.
      break;
    }
  return false;
}

// This kicks off the whole cascade starting from "hello".
void
session::begin_service()
{
  keypair kp;
  if (use_transport_auth)
    keys.get_key_pair(signing_key, kp);
  queue_hello_cmd(signing_key, kp.pub, mk_nonce());
}

void
session::maybe_step()
{
  while (done_all_refinements()
         && !rev_enumerator.done()
         && outbuf_size < constants::bufsz * 10)
    {
      rev_enumerator.step();
    }
}

void
session::maybe_say_goodbye(transaction_guard & guard)
{
  if (voice == client_voice
      && protocol_state == working_state
      && finished_working())
    {
      protocol_state = shutdown_state;
      guard.do_checkpoint();
      queue_bye_cmd(0);
    }
}

bool
session::arm()
{
  if (!armed)
    {
      // Don't pack the buffer unnecessarily.
      if (outbuf_size > constants::bufsz * 10)
        return false;

      if (cmd.read(inbuf, read_hmac))
        {
          armed = true;
        }
    }
  return armed;
}

bool session::process(transaction_guard & guard)
{
  if (encountered_error)
    return true;
  try
    {
      if (!arm())
        return true;

      armed = false;
      L(FL("processing %d byte input buffer from peer %s")
        % inbuf.size() % peer_id);

      size_t sz = cmd.encoded_size();
      bool ret = dispatch_payload(cmd, guard);

      if (inbuf.size() >= constants::netcmd_maxsz)
        W(F("input buffer for peer %s is overfull "
            "after netcmd dispatch") % peer_id);

      guard.maybe_checkpoint(sz);

      if (!ret)
        L(FL("finishing processing with '%d' packet")
          % cmd.get_cmd_code());
      return ret;
    }
  catch (bad_decode & bd)
    {
      W(F("protocol error while processing peer %s: '%s'")
        % peer_id % bd.what);
      return false;
    }
  catch (netsync_error & err)
    {
      W(F("error: %s") % err.msg);
      queue_error_cmd(boost::lexical_cast<string>(error_code) + " " + err.msg);
      encountered_error = true;
      return true; // Don't terminate until we've send the error_cmd.
    }
}


static shared_ptr<Netxx::StreamBase>
build_stream_to_server(options & opts, lua_hooks & lua,
                       netsync_connection_info info,
                       Netxx::port_type default_port,
                       Netxx::Timeout timeout)
{
  shared_ptr<Netxx::StreamBase> server;

  if (info.client.use_argv)
    {
      I(!info.client.argv.empty());
      string cmd = info.client.argv[0];
      info.client.argv.erase(info.client.argv.begin());
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::PipeStream(cmd, info.client.argv));
    }
  else
    {
#ifdef USE_IPV6
      bool use_ipv6=true;
#else
      bool use_ipv6=false;
#endif
      string host(info.client.u.host);
      if (host.empty())
        host = info.client.unparsed();
      if (!info.client.u.port.empty())
        default_port = lexical_cast<Netxx::port_type>(info.client.u.port);
      Netxx::Address addr(info.client.unparsed().c_str(),
                          default_port, use_ipv6);
      return shared_ptr<Netxx::StreamBase>
        (new Netxx::Stream(addr, timeout));
    }
}

static void
call_server(options & opts,
            lua_hooks & lua,
            project_t & project,
            key_store & keys,
            protocol_role role,
            netsync_connection_info const & info,
            Netxx::port_type default_port,
            unsigned long timeout_seconds)
{
  Netxx::PipeCompatibleProbe probe;
  transaction_guard guard(project.db);

  Netxx::Timeout timeout(static_cast<long>(timeout_seconds)), instant(0,1);

  P(F("connecting to %s") % info.client.unparsed);

  shared_ptr<Netxx::StreamBase> server
    = build_stream_to_server(opts, lua,
                             info, default_port,
                             timeout);


  // 'false' here means not to revert changes when the SockOpt
  // goes out of scope.
  Netxx::SockOpt socket_options(server->get_socketfd(), false);
  socket_options.set_non_blocking();

  session sess(opts, lua, project, keys,
               role, client_voice,
               info.client.include_pattern,
               info.client.exclude_pattern,
               info.client.unparsed(), server);

  while (true)
    {
      bool armed = false;
      try
        {
          armed = sess.arm();
        }
      catch (bad_decode & bd)
        {
          E(false, F("protocol error while processing peer %s: '%s'")
            % sess.peer_id % bd.what);
        }

      sess.maybe_step();
      sess.maybe_say_goodbye(guard);

      probe.clear();
      probe.add(*(sess.str), sess.which_events());
      Netxx::Probe::result_type res = probe.ready(armed ? instant : timeout);
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;

      if (fd == -1 && !armed)
        {
          E(false, (F("timed out waiting for I/O with "
                      "peer %s, disconnecting")
                    % sess.peer_id));
        }

      bool all_io_clean = (event != Netxx::Probe::ready_oobd);

      if (event & Netxx::Probe::ready_read)
        all_io_clean = all_io_clean && sess.read_some();

      if (event & Netxx::Probe::ready_write)
        all_io_clean = all_io_clean && sess.write_some();

      if (armed)
        if (!sess.process(guard))
          {
            // Commit whatever work we managed to accomplish anyways.
            guard.commit();

            // We failed during processing. This should only happen in
            // client voice when we have a decode exception, or received an
            // error from our server (which is translated to a decode
            // exception). We call these cases E() errors.
            E(false, F("processing failure while talking to "
                       "peer %s, disconnecting")
              % sess.peer_id);
            return;
          }

      if (!all_io_clean)
        {
          // Commit whatever work we managed to accomplish anyways.
          guard.commit();

          // We had an I/O error. We must decide if this represents a
          // user-reported error or a clean disconnect. See protocol
          // state diagram in session::process_bye_cmd.

          if (sess.protocol_state == session::confirmed_state)
            {
              P(F("successful exchange with %s")
                % sess.peer_id);
              return;
            }
          else if (sess.encountered_error)
            {
              P(F("peer %s disconnected after we informed them of error")
                % sess.peer_id);
              return;
            }
          else
            E(false, (F("I/O failure while talking to "
                        "peer %s, disconnecting")
                      % sess.peer_id));
        }
    }
}

static void
drop_session_associated_with_fd(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                Netxx::socket_type fd)
{
  // This is a bit of a hack. Initially all "file descriptors" in
  // netsync were full duplex, so we could get away with indexing
  // sessions by their file descriptor.
  //
  // When using pipes in unix, it's no longer true: a session gets
  // entered in the session map under its read pipe fd *and* its write
  // pipe fd. When we're in such a situation the socket fd is "-1" and
  // we downcast to a PipeStream and use its read+write fds.
  //
  // When using pipes in windows, we use a full duplex pipe (named
  // pipe) so the socket-like abstraction holds.

  I(fd != -1);
  map<Netxx::socket_type, shared_ptr<session> >::const_iterator i = sessions.find(fd);
  I(i != sessions.end());
  shared_ptr<session> sess = i->second;
  fd = sess->str->get_socketfd();
  if (fd != -1)
    {
      sessions.erase(fd);
    }
  else
    {
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(sess->str);
      I(static_cast<bool>(pipe));
      I(pipe->get_writefd() != -1);
      I(pipe->get_readfd() != -1);
      sessions.erase(pipe->get_readfd());
      sessions.erase(pipe->get_writefd());
    }
}

static void
arm_sessions_and_calculate_probe(Netxx::PipeCompatibleProbe & probe,
                                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                 set<Netxx::socket_type> & armed_sessions,
                                 transaction_guard & guard)
{
  set<Netxx::socket_type> arm_failed;
  for (map<Netxx::socket_type,
         shared_ptr<session> >::const_iterator i = sessions.begin();
       i != sessions.end(); ++i)
    {
      i->second->maybe_step();
      i->second->maybe_say_goodbye(guard);
      try
        {
          if (i->second->arm())
            {
              L(FL("fd %d is armed") % i->first);
              armed_sessions.insert(i->first);
            }
          probe.add(*i->second->str, i->second->which_events());
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s', marking as bad")
            % i->second->peer_id % bd.what);
          arm_failed.insert(i->first);
        }
    }
  for (set<Netxx::socket_type>::const_iterator i = arm_failed.begin();
       i != arm_failed.end(); ++i)
    {
      drop_session_associated_with_fd(sessions, *i);
    }
}

static void
handle_new_connection(options & opts,
                      lua_hooks & lua,
                      project_t & project,
                      key_store & keys,
                      Netxx::Address & addr,
                      Netxx::StreamServer & server,
                      Netxx::Timeout & timeout,
                      protocol_role role,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions)
{
  L(FL("accepting new connection on %s : %s")
    % (addr.get_name()?addr.get_name():"") % lexical_cast<string>(addr.get_port()));
  Netxx::Peer client = server.accept_connection();

  if (!client)
    {
      L(FL("accept() returned a dead client"));
    }
  else
    {
      P(F("accepted new client connection from %s : %s")
        % client.get_address() % lexical_cast<string>(client.get_port()));

      // 'false' here means not to revert changes when the SockOpt
      // goes out of scope.
      Netxx::SockOpt socket_options(client.get_socketfd(), false);
      socket_options.set_non_blocking();

      shared_ptr<Netxx::Stream> str =
        shared_ptr<Netxx::Stream>
        (new Netxx::Stream(client.get_socketfd(), timeout));

      shared_ptr<session> sess(new session(opts, lua, project, keys,
                                           role, server_voice,
                                           globish("*"), globish(""),
                                           lexical_cast<string>(client), str));
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
          W(F("protocol error while processing peer %s: '%s', disconnecting")
            % sess->peer_id % bd.what);
          drop_session_associated_with_fd(sessions, fd);
          live_p = false;
        }
    }
  else
    {
      switch (sess->protocol_state)
        {
        case session::working_state:
          P(F("peer %s read failed in working state (error)")
            % sess->peer_id);
          break;

        case session::shutdown_state:
          P(F("peer %s read failed in shutdown state "
              "(possibly client misreported error)")
            % sess->peer_id);
          break;

        case session::confirmed_state:
          P(F("peer %s read failed in confirmed state (success)")
            % sess->peer_id);
          break;
        }
      drop_session_associated_with_fd(sessions, fd);
      live_p = false;
    }
}


static void
handle_write_available(Netxx::socket_type fd,
                       shared_ptr<session> sess,
                       map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       bool & live_p)
{
  if (!sess->write_some())
    {
      switch (sess->protocol_state)
        {
        case session::working_state:
          P(F("peer %s write failed in working state (error)")
            % sess->peer_id);
          break;

        case session::shutdown_state:
          P(F("peer %s write failed in shutdown state "
              "(possibly client misreported error)")
            % sess->peer_id);
          break;

        case session::confirmed_state:
          P(F("peer %s write failed in confirmed state (success)")
            % sess->peer_id);
          break;
        }

      drop_session_associated_with_fd(sessions, fd);
      live_p = false;
    }
}

static void
process_armed_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       set<Netxx::socket_type> & armed_sessions,
                       transaction_guard & guard)
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
          shared_ptr<session> sess = j->second;
          if (!sess->process(guard))
            {
              P(F("peer %s processing finished, disconnecting")
                % sess->peer_id);
              drop_session_associated_with_fd(sessions, *i);
            }
        }
    }
}

static void
reap_dead_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                   unsigned long timeout_seconds)
{
  // Kill any clients which haven't done any i/o inside the timeout period
  // or who have exchanged all items and flushed their output buffers.
  set<Netxx::socket_type> dead_clients;
  time_t now = ::time(NULL);
  for (map<Netxx::socket_type, shared_ptr<session> >::const_iterator
         i = sessions.begin(); i != sessions.end(); ++i)
    {
      if (static_cast<unsigned long>(i->second->last_io_time + timeout_seconds)
          < static_cast<unsigned long>(now))
        {
          P(F("fd %d (peer %s) has been idle too long, disconnecting")
            % i->first % i->second->peer_id);
          dead_clients.insert(i->first);
        }
    }
  for (set<Netxx::socket_type>::const_iterator i = dead_clients.begin();
       i != dead_clients.end(); ++i)
    {
      drop_session_associated_with_fd(sessions, *i);
    }
}

static void
serve_connections(options & opts,
                  lua_hooks & lua,
                  project_t & project,
                  key_store & keys,
                  protocol_role role,
                  std::list<utf8> const & addresses,
                  Netxx::port_type default_port,
                  unsigned long timeout_seconds,
                  unsigned long session_limit)
{
  Netxx::PipeCompatibleProbe probe;

  Netxx::Timeout
    forever,
    timeout(static_cast<long>(timeout_seconds)),
    instant(0,1);

#ifdef USE_IPV6
  bool use_ipv6=true;
#else
  bool use_ipv6=false;
#endif
  // This will be true when we try to bind while using IPv6.  See comments
  // further down.
  bool try_again=false;

  do
    {
      try
        {
          try_again = false;

          Netxx::Address addr(use_ipv6);

          if (addresses.empty())
            addr.add_all_addresses(default_port);
          else
            {
              for (std::list<utf8>::const_iterator it = addresses.begin(); it != addresses.end(); ++it)
                {
                  const utf8 & address = *it;
                  if (!address().empty())
                    {
                      size_t l_colon = address().find(':');
                      size_t r_colon = address().rfind(':');

                      if (l_colon == r_colon && l_colon == 0)
                        {
                          // can't be an IPv6 address as there is only one colon
                          // must be a : followed by a port
                          string port_str = address().substr(1);
                          addr.add_all_addresses(std::atoi(port_str.c_str()));
                        }
                      else
                        addr.add_address(address().c_str(), default_port);
                    }
                }
            }

          // If se use IPv6 and the initialisation of server fails, we want
          // to try again with IPv4.  The reason is that someone may have
          // downloaded a IPv6-enabled monotone on a system that doesn't
          // have IPv6, and which might fail therefore.
          // On failure, Netxx::NetworkException is thrown, and we catch
          // it further down.
          try_again=use_ipv6;

          Netxx::StreamServer server(addr, timeout);

          // If we came this far, whatever we used (IPv6 or IPv4) was
          // accepted, so we don't need to try again any more.
          try_again=false;

          const char *name = addr.get_name();
          P(F("beginning service on %s : %s")
            % (name != NULL ? name : _("<all interfaces>"))
            % lexical_cast<string>(addr.get_port()));

          map<Netxx::socket_type, shared_ptr<session> > sessions;
          set<Netxx::socket_type> armed_sessions;

          shared_ptr<transaction_guard> guard;

          while (true)
            {
              probe.clear();
              armed_sessions.clear();

              if (sessions.size() >= session_limit)
                W(F("session limit %d reached, some connections "
                    "will be refused") % session_limit);
              else
                probe.add(server);

              if (!guard)
                guard = shared_ptr<transaction_guard>
                  (new transaction_guard(project.db));

              I(guard);

              while (!server_initiated_sync_requests.empty())
                {
                  server_initiated_sync_request request
                    = server_initiated_sync_requests.front();
                  server_initiated_sync_requests.pop_front();

                  netsync_connection_info info;
                  info.client.unparsed = utf8(request.address);
                  info.client.include_pattern = globish(request.include);
                  info.client.exclude_pattern = globish(request.exclude);
                  info.client.use_argv = false;
                  parse_uri(info.client.unparsed(), info.client.u);

                  try
                    {
                      P(F("connecting to %s") % info.client.unparsed);
                      shared_ptr<Netxx::StreamBase> server
                        = build_stream_to_server(opts, lua,
                                                 info, default_port,
                                                 timeout);

                      // 'false' here means not to revert changes when
                      // the SockOpt goes out of scope.
                      Netxx::SockOpt socket_options(server->get_socketfd(), false);
                      socket_options.set_non_blocking();

                      protocol_role role = source_and_sink_role;
                      if (request.what == "sync")
                        role = source_and_sink_role;
                      else if (request.what == "push")
                        role = source_role;
                      else if (request.what == "pull")
                        role = sink_role;

                      shared_ptr<session> sess(new session(opts, lua,
                                                           project, keys,
                                                           role, client_voice,
                                                           info.client.include_pattern,
                                                           info.client.exclude_pattern,
                                                           info.client.unparsed(),
                                                           server, true));

                      sessions.insert(make_pair(server->get_socketfd(), sess));
                    }
                  catch (Netxx::NetworkException & e)
                    {
                      P(F("Network error: %s") % e.what());
                    }
                }

              arm_sessions_and_calculate_probe(probe, sessions, armed_sessions, *guard);

              L(FL("i/o probe with %d armed") % armed_sessions.size());
              Netxx::socket_type fd;
              Netxx::Timeout how_long;
              if (sessions.empty())
                how_long = forever;
              else if (armed_sessions.empty())
                how_long = timeout;
              else
                how_long = instant;
              do
                {
                  Netxx::Probe::result_type res = probe.ready(how_long);
                  how_long = instant;
                  Netxx::Probe::ready_type event = res.second;
                  fd = res.first;

                  if (fd == -1)
                    {
                      if (armed_sessions.empty())
                        L(FL("timed out waiting for I/O (listening on %s : %s)")
                          % addr.get_name() % lexical_cast<string>(addr.get_port()));
                    }

                  // we either got a new connection
                  else if (fd == server)
                    handle_new_connection(opts, lua, project, keys,
                                          addr, server, timeout, role,
                                          sessions);

                  // or an existing session woke up
                  else
                    {
                      map<Netxx::socket_type, shared_ptr<session> >::iterator i;
                      i = sessions.find(fd);
                      if (i == sessions.end())
                        {
                          L(FL("got woken up for action on unknown fd %d") % fd);
                        }
                      else
                        {
                          probe.remove(*(i->second->str));
                          shared_ptr<session> sess = i->second;
                          bool live_p = true;

                          try
                            {
                              if (event & Netxx::Probe::ready_read)
                                handle_read_available(fd, sess, sessions,
                                                      armed_sessions, live_p);

                              if (live_p && (event & Netxx::Probe::ready_write))
                                handle_write_available(fd, sess, sessions, live_p);
                            }
                          catch (Netxx::Exception &)
                            {
                              P(F("Network error on peer %s, disconnecting")
                                % sess->peer_id);
                              drop_session_associated_with_fd(sessions, fd);
                            }
                          if (live_p && (event & Netxx::Probe::ready_oobd))
                            {
                              P(F("got OOB from peer %s, disconnecting")
                                % sess->peer_id);
                              drop_session_associated_with_fd(sessions, fd);
                            }
                        }
                    }
                }
              while (fd != -1);
              process_armed_sessions(sessions, armed_sessions, *guard);
              reap_dead_sessions(sessions, timeout_seconds);

              if (sessions.empty())
                {
                  // Let the guard die completely if everything's gone quiet.
                  guard->commit();
                  guard.reset();
                }
            }
        }
      // This exception is thrown when bind() fails somewhere in Netxx.
      catch (Netxx::NetworkException &)
        {
          // If we tried with IPv6 and failed, we want to try again using IPv4.
          if (try_again)
            {
              use_ipv6 = false;
            }
          // In all other cases, just rethrow the exception.
          else
            throw;
        }
      // This exception is thrown when there is no support for the type of
      // connection we want to do in the kernel, for example when a socket()
      // call fails somewhere in Netxx.
      catch (Netxx::Exception &)
        {
          // If we tried with IPv6 and failed, we want to try again using IPv4.
          if (try_again)
            {
              use_ipv6 = false;
            }
          // In all other cases, just rethrow the exception.
          else
            throw;
        }
    }
  while(try_again);
  }

static void
serve_single_connection(shared_ptr<session> sess,
                        unsigned long timeout_seconds)
{
  Netxx::PipeCompatibleProbe probe;

  Netxx::Timeout
    forever,
    timeout(static_cast<long>(timeout_seconds)),
    instant(0,1);

  P(F("beginning service on %s") % sess->peer_id);

  sess->begin_service();

  transaction_guard guard(sess->project.db);

  map<Netxx::socket_type, shared_ptr<session> > sessions;
  set<Netxx::socket_type> armed_sessions;

  if (sess->str->get_socketfd() == -1)
    {
      // Unix pipes are non-duplex, have two filedescriptors
      shared_ptr<Netxx::PipeStream> pipe =
        boost::dynamic_pointer_cast<Netxx::PipeStream, Netxx::StreamBase>(sess->str);
      I(pipe);
      sessions[pipe->get_writefd()]=sess;
      sessions[pipe->get_readfd()]=sess;
    }
  else
    sessions[sess->str->get_socketfd()]=sess;

  while (!sessions.empty())
    {
      probe.clear();
      armed_sessions.clear();

      arm_sessions_and_calculate_probe(probe, sessions, armed_sessions, guard);

      L(FL("i/o probe with %d armed") % armed_sessions.size());
      Netxx::Probe::result_type res = probe.ready((armed_sessions.empty() ? timeout
                                                   : instant));
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;

      if (fd == -1)
        {
          if (armed_sessions.empty())
            L(FL("timed out waiting for I/O (listening on %s)")
              % sess->peer_id);
        }

      // an existing session woke up
      else
        {
          map<Netxx::socket_type, shared_ptr<session> >::iterator i;
          i = sessions.find(fd);
          if (i == sessions.end())
            {
              L(FL("got woken up for action on unknown fd %d") % fd);
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
                  P(F("got some OOB data on fd %d (peer %s), disconnecting")
                    % fd % sess->peer_id);
                  drop_session_associated_with_fd(sessions, fd);
                }
            }
        }
      process_armed_sessions(sessions, armed_sessions, guard);
      reap_dead_sessions(sessions, timeout_seconds);
    }
}


void
insert_with_parents(revision_id rev,
                    refiner & ref,
                    revision_enumerator & rev_enumerator,
                    set<revision_id> & revs,
                    ticker & revisions_ticker)
{
  deque<revision_id> work;
  work.push_back(rev);
  while (!work.empty())
    {
      revision_id rid = work.front();
      work.pop_front();

      if (!null_id(rid) && revs.find(rid) == revs.end())
        {
          revs.insert(rid);
          ++revisions_ticker;
          ref.note_local_item(rid.inner());
          vector<revision_id> parents;
          rev_enumerator.get_revision_parents(rid, parents);
          for (vector<revision_id>::const_iterator i = parents.begin();
               i != parents.end(); ++i)
            {
              work.push_back(*i);
            }
        }
    }
}

void
session::rebuild_merkle_trees(set<branch_name> const & branchnames)
{
  P(F("finding items to synchronize:"));
  for (set<branch_name>::const_iterator i = branchnames.begin();
      i != branchnames.end(); ++i)
    L(FL("including branch %s") % *i);

  // xgettext: please use short message and try to avoid multibytes chars
  ticker revisions_ticker(N_("revisions"), "r", 64);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker certs_ticker(N_("certificates"), "c", 256);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker keys_ticker(N_("keys"), "k", 1);

  set<revision_id> revision_ids;
  set<rsa_keypair_id> inserted_keys;

  {
    for (set<branch_name>::const_iterator i = branchnames.begin();
         i != branchnames.end(); ++i)
      {
        // Get branch certs.
        vector< revision<cert> > certs;
        project.get_branch_certs(*i, certs);
        for (vector< revision<cert> >::const_iterator j = certs.begin();
             j != certs.end(); j++)
          {
            revision_id rid(j->inner().ident);
            insert_with_parents(rid, rev_refiner, rev_enumerator,
                                revision_ids, revisions_ticker);
            // Branch certs go in here, others later on.
            id item;
            cert_hash_code(j->inner(), item);
            cert_refiner.note_local_item(item);
            rev_enumerator.note_cert(rid, item);
            if (inserted_keys.find(j->inner().key) == inserted_keys.end())
              inserted_keys.insert(j->inner().key);
          }
      }
  }

  {
    map<branch_name, epoch_data> epochs;
    project.db.get_epochs(epochs);

    epoch_data epoch_zero(string(constants::epochlen_bytes, '\x00'));
    for (set<branch_name>::const_iterator i = branchnames.begin();
         i != branchnames.end(); ++i)
      {
        branch_name const & branch(*i);
        map<branch_name, epoch_data>::const_iterator j;
        j = epochs.find(branch);

        // Set to zero any epoch which is not yet set.
        if (j == epochs.end())
          {
            L(FL("setting epoch on %s to zero") % branch);
            epochs.insert(make_pair(branch, epoch_zero));
            project.db.set_epoch(branch, epoch_zero);
          }

        // Then insert all epochs into merkle tree.
        j = epochs.find(branch);
        I(j != epochs.end());
        epoch_id eid;
        epoch_hash_code(j->first, j->second, eid);
        epoch_refiner.note_local_item(eid.inner());
      }
  }

  {
    typedef vector< pair<revision_id,
      pair<revision_id, rsa_keypair_id> > > cert_idx;

    cert_idx idx;
    project.db.get_revision_cert_nobranch_index(idx);

    // Insert all non-branch certs reachable via these revisions
    // (branch certs were inserted earlier).

    for (cert_idx::const_iterator i = idx.begin(); i != idx.end(); ++i)
      {
        revision_id const & hash = i->first;
        revision_id const & ident = i->second.first;
        rsa_keypair_id const & key = i->second.second;

        rev_enumerator.note_cert(ident, hash.inner());

        if (revision_ids.find(ident) == revision_ids.end())
          continue;

        cert_refiner.note_local_item(hash.inner());
        ++certs_ticker;
        if (inserted_keys.find(key) == inserted_keys.end())
            inserted_keys.insert(key);
      }
  }

  // Add any keys specified on the command line.
  for (vector<rsa_keypair_id>::const_iterator key
         = keys_to_push.begin();
       key != keys_to_push.end(); ++key)
    {
      if (inserted_keys.find(*key) == inserted_keys.end())
        {
          if (!project.db.public_key_exists(*key))
            {
              keypair kp;
              if (keys.maybe_get_key_pair(*key, kp))
                project.db.put_key(*key, kp.pub);
              else
                W(F("Cannot find key '%s'") % *key);
            }
          inserted_keys.insert(*key);
        }
    }

  // Insert all the keys.
  for (set<rsa_keypair_id>::const_iterator key = inserted_keys.begin();
       key != inserted_keys.end(); key++)
    {
      if (project.db.public_key_exists(*key))
        {
          rsa_pub_key pub;
          project.db.get_key(*key, pub);
          id keyhash;
          key_hash_code(*key, pub, keyhash);

          if (global_sanity.debug_p())
            L(FL("noting key '%s' = '%s' to send")
              % *key
              % encode_hexenc(keyhash()));

          key_refiner.note_local_item(keyhash);
          ++keys_ticker;
        }
    }

  rev_refiner.reindex_local_items();
  cert_refiner.reindex_local_items();
  key_refiner.reindex_local_items();
  epoch_refiner.reindex_local_items();
}

void
run_netsync_protocol(options & opts, lua_hooks & lua,
                     project_t & project, key_store & keys,
                     protocol_voice voice,
                     protocol_role role,
                     netsync_connection_info const & info)
{
  if (info.client.include_pattern().find_first_of("'\"") != string::npos)
    {
      W(F("include branch pattern contains a quote character:\n"
          "%s") % info.client.include_pattern());
    }

  if (info.client.exclude_pattern().find_first_of("'\"") != string::npos)
    {
      W(F("exclude branch pattern contains a quote character:\n"
          "%s") % info.client.exclude_pattern());
    }

  // We do not want to be killed by SIGPIPE from a network disconnect.
  ignore_sigpipe();

  try
    {
      if (voice == server_voice)
        {
          if (opts.bind_stdio)
            {
              shared_ptr<Netxx::PipeStream> str(new Netxx::PipeStream(0,1));
              shared_ptr<session> sess(new session(opts, lua, project, keys,
                                                   role, server_voice,
                                                   globish("*"), globish(""),
                                                   "stdio", str));
              serve_single_connection(sess,constants::netsync_timeout_seconds);
            }
          else
            serve_connections(opts, lua, project, keys,
                              role,
                              info.server.addrs,
                              static_cast<Netxx::port_type>(constants::netsync_default_port),
                              static_cast<unsigned long>(constants::netsync_timeout_seconds),
                              static_cast<unsigned long>(constants::netsync_connection_limit));
        }
      else
        {
          I(voice == client_voice);
          call_server(opts, lua, project, keys,
                      role, info,
                      static_cast<Netxx::port_type>(constants::netsync_default_port),
                      static_cast<unsigned long>(constants::netsync_timeout_seconds));
        }
    }
  catch (Netxx::NetworkException & e)
    {
      throw informative_failure((F("network error: %s") % e.what()).str());
    }
  catch (Netxx::Exception & e)
    {
      throw oops((F("network error: %s") % e.what()).str());;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
