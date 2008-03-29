// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <set>
#include <utility>

#include <boost/shared_ptr.hpp>

#include "refiner.hh"
#include "vocab.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"

using std::inserter;
using std::make_pair;
using std::set;
using std::set_difference;
using std::string;

using boost::dynamic_bitset;

// Our goal is to learn the complete set of items to send. To do this
// we exchange two types of refinement commands: queries and responses.
//
//  - On receiving a 'query' refinement for a node (p,l) you have:
//    - Compare the query node to your node (p,l), noting all the leaves
//      you must send as a result of what you learn in comparison.
//    - For each slot, if you have a subtree where the peer does not
//      (or you both do, and yours differs) send a sub-query for that
//      node, incrementing your query-in-flight counter.
//    - Send a 'response' refinement carrying your node (p,l)
//
//  - On receiving a 'query' refinement for a node (p,l) you don't have:
//    - Send a 'response' refinement carrying an empty synthetic node (p,l)
//
//  - On receiving a 'response' refinement for (p,l)
//    - Compare the query node to your node (p,l), noting all the leaves
//      you must send as a result of what you learn in comparison.
//    - Decrement your query-in-flight counter.
//
// The client kicks the process off by sending a query refinement for the
// root node. When the client's query-in-flight counter drops to zero,
// the client sends a done command, stating how many items it will be
// sending.
//
// When the server receives a done command, it echoes it back stating how
// many items *it* is going to send.
//
// When either side receives a done command, it transitions to
// streaming send mode, sending all the items it's calculated.

void
refiner::note_local_item(id const & item)
{
  local_items.insert(item);
  insert_into_merkle_tree(table, type, item, 0);
}

void
refiner::reindex_local_items()
{
  recalculate_merkle_codes(table, prefix(""), 0);
}

void
refiner::load_merkle_node(size_t level, prefix const & pref,
                          merkle_ptr & node)
{
  merkle_table::const_iterator j = table.find(make_pair(pref, level));
  I(j != table.end());
  node = j->second;
}

bool
refiner::merkle_node_exists(size_t level,
                            prefix const & pref)
{
  merkle_table::const_iterator j = table.find(make_pair(pref, level));
  return (j != table.end());
}

void
refiner::calculate_items_to_send()
{
  if (calculated_items_to_send)
    return;

  items_to_send.clear();
  items_to_receive = 0;

  set_difference(local_items.begin(), local_items.end(),
                 peer_items.begin(), peer_items.end(),
                 inserter(items_to_send, items_to_send.begin()));

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  //   L(FL("%s determined %d %s items to send") 
  //     % voicestr() % items_to_send.size() % typestr);
  calculated_items_to_send = true;
}


void
refiner::send_subquery(merkle_node const & our_node, size_t slot)
{
  prefix subprefix;
  our_node.extended_raw_prefix(slot, subprefix);
  merkle_ptr our_subtree;
  load_merkle_node(our_node.level + 1, subprefix, our_subtree);
  // L(FL("%s queueing subquery on level %d\n") % voicestr() % (our_node.level + 1));
  cb.queue_refine_cmd(refinement_query, *our_subtree);
  ++queries_in_flight;
}

void
refiner::send_synthetic_subquery(merkle_node const & our_node, size_t slot)
{
  id val;
  size_t subslot;
  dynamic_bitset<unsigned char> subprefix;

  our_node.get_raw_slot(slot, val);
  pick_slot_and_prefix_for_value(val, our_node.level + 1, subslot, subprefix);

  merkle_node synth_node;
  synth_node.pref = subprefix;
  synth_node.level = our_node.level + 1;
  synth_node.type = our_node.type;
  synth_node.set_raw_slot(subslot, val);
  synth_node.set_slot_state(subslot, our_node.get_slot_state(slot));

  // L(FL("%s queueing synthetic subquery on level %d\n") % voicestr() % (our_node.level + 1));
  cb.queue_refine_cmd(refinement_query, synth_node);
  ++queries_in_flight;
}

void
refiner::note_subtree_shared_with_peer(merkle_node const & our_node, size_t slot)
{
  prefix pref;
  our_node.extended_raw_prefix(slot, pref);
  collect_items_in_subtree(table, pref, our_node.level+1, peer_items);
}

refiner::refiner(netcmd_item_type type, protocol_voice voice, refiner_callbacks & cb)
  : type(type), voice (voice), cb(cb),
    sent_initial_query(false),
    queries_in_flight(0),
    calculated_items_to_send(false),
    done(false),
    items_to_receive(0)
{
  merkle_ptr root = merkle_ptr(new merkle_node());
  root->type = type;
  table.insert(make_pair(make_pair(prefix(""), 0), root));
}

void
refiner::note_item_in_peer(merkle_node const & their_node, size_t slot)
{
  I(slot < constants::merkle_num_slots);
  id slotval;
  their_node.get_raw_slot(slot, slotval);
  peer_items.insert(slotval);

  // Write a debug message
  /*
  {
    id slotval;
    their_node.get_raw_slot(slot, slotval);

    hexenc<prefix> hpref;
    their_node.get_hex_prefix(hpref);

    string typestr;
    netcmd_item_type_to_string(their_node.type, typestr);

    L(FL("%s's peer has %s '%s' at slot %d (in node '%s', level %d)")
      % voicestr() % typestr % slotval
      % slot % hpref % their_node.level);
  }
  */
}


void
refiner::begin_refinement()
{
  merkle_ptr root;
  load_merkle_node(0, prefix(""), root);
  // L(FL("%s queueing initial node\n") % voicestr());
  cb.queue_refine_cmd(refinement_query, *root);
  ++queries_in_flight;
  sent_initial_query = true;
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("Beginning %s refinement on %s.") % typestr % voicestr());
}
  
void
refiner::process_done_command(size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  calculate_items_to_send();
  items_to_receive = n_items;

  L(FL("%s finished %s refinement: %d to send, %d to receive")
    % voicestr() % typestr % items_to_send.size() % items_to_receive);

  /*
  if (local_items.size() < 25) 
    {
      // Debugging aid.
      L(FL("+++ %d items in %s") % local_items.size() % voicestr());
      for (set<id>::const_iterator i = local_items.begin(); 
           i != local_items.end(); ++i)
        {
          L(FL("%s item %s") % voicestr() % *i);
        }
      L(FL("--- items in %s") % voicestr());
    }
  */

  if (voice == server_voice) 
    {
      //       L(FL("server responding to [done %s %d] with [done %s %d]")
      //         % typestr % n_items % typestr % items_to_send.size());
      cb.queue_done_cmd(type, items_to_send.size());
    }

  done = true;
  
  // we can clear up the merkle trie's memory now
  table.clear();
}


void
refiner::process_refinement_command(refinement_type ty,
                                    merkle_node const & their_node)
{
  prefix pref;
  hexenc<prefix> hpref;
  their_node.get_raw_prefix(pref);
  their_node.get_hex_prefix(hpref);
  string typestr;

  netcmd_item_type_to_string(their_node.type, typestr);
  size_t lev = static_cast<size_t>(their_node.level);

  //   L(FL("%s received refinement %s netcmd on %s node '%s', level %d") %
  //   voicestr() % (ty == refinement_query ? "query" : "response") %
  //   typestr % hpref % lev);

  merkle_ptr our_node;

  if (merkle_node_exists(their_node.level, pref))
    load_merkle_node(their_node.level, pref, our_node);
  else
    {
      // Synthesize empty node if we don't have one.
      our_node = merkle_ptr(new merkle_node);
      our_node->pref = their_node.pref;
      our_node->level = their_node.level;
      our_node->type = their_node.type;
    }

  for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
    {
      // Note any leaves they have.
      if (their_node.get_slot_state(slot) == leaf_state)
        note_item_in_peer(their_node, slot);

      if (ty == refinement_query)
        {
          // This block handles the interesting asymmetric cases of subtree
          // vs. leaf.
          //
          // Note that in general we're not allowed to send a new query
          // packet when we're looking at a response. This wrinkle is both
          // why this block appears to do slightly more work than necessary,
          // and why it's predicated on "ty == refinement_query". More detail
          // in the cases below.

          if (their_node.get_slot_state(slot) == leaf_state
              && our_node->get_slot_state(slot) == subtree_state)
            {
              // If they have a leaf and we have a subtree, we need to look
              // in our subtree to find if their leaf is present, and send
              // them a "query" that will inform them, in passing, of the
              // presence of our node.

              id their_slotval;
              their_node.get_raw_slot(slot, their_slotval);
              size_t snum;
              merkle_ptr mp;
              if (locate_item(table, their_slotval, snum, mp))
                {
                  cb.queue_refine_cmd(refinement_query, *mp);
                  ++queries_in_flight;
                }                  

            }

          else if (their_node.get_slot_state(slot) == subtree_state
                   && our_node->get_slot_state(slot) == leaf_state)
            {
              // If they have a subtree and we have a leaf, we need to
              // arrange for a subquery to explore the subtree looking for
              // the leaf in *their* subtree. The tricky part is that we
              // cannot have this subquery triggered by our response
              // packet. We need to initiate a new (redundant) query here to
              // prompt our peer to explore the subtree.
              //
              // This is purely for the sake of balancing the bracketing of
              // queries and responses: if they were to reply to our
              // response packet, our query-in-flight counter would have
              // temporarily dropped to zero and we'd have initiated
              // streaming send mode.
              //
              // Yes, the need to invert the sense of queries in this case
              // represents a misdesign in this generation of the netsync
              // protocol. It still contains much less hair than it used to,
              // so I'm willing to accept it.

              send_synthetic_subquery(*our_node, slot);
            }

          // Finally: if they had an empty slot in either case, there's no
          // subtree exploration to perform; the response packet will inform
          // the peer of everything relevant know about this node: namely
          // that they're going to receive a complete subtree, we know
          // what's in it, and we'll tell them how many nodes to expect in
          // the aggregate count of the 'done' commane.

        }

      // Compare any subtrees, if we both have subtrees.
      if (their_node.get_slot_state(slot) == subtree_state
          && our_node->get_slot_state(slot) == subtree_state)
        {
          id our_slotval, their_slotval;
          their_node.get_raw_slot(slot, their_slotval);
          our_node->get_raw_slot(slot, our_slotval);

          // Always note when you share a subtree.
          if (their_slotval == our_slotval)
            note_subtree_shared_with_peer(*our_node, slot);

          // Send subqueries when you have a different subtree
          // and you're answering a query message.
          else if (ty == refinement_query)
            send_subquery(*our_node, slot);
        }
    }

  if (ty == refinement_response)
    {
      E((queries_in_flight > 0),
        F("underflow on query-in-flight counter"));
      --queries_in_flight;

      // Possibly this signals the end of refinement.
      if (voice == client_voice && queries_in_flight == 0)
        {
          string typestr;          
          netcmd_item_type_to_string(their_node.type, typestr);
          calculate_items_to_send();
          // L(FL("client sending [done %s %d]") % typestr % items_to_send.size());
          cb.queue_done_cmd(type, items_to_send.size());
        }
    }
  else
    {
      // Always reply to every query with the current node.
      I(ty == refinement_query);
      // L(FL("%s queueing response to query on %d\n") % voicestr() % our_node->level);
      cb.queue_refine_cmd(refinement_response, *our_node);
    }
}

#ifdef BUILD_UNIT_TESTS
#include "randomizer.hh"
#include "unit_tests.hh"

#include <deque>
#include <boost/shared_ptr.hpp>

using std::deque;
using boost::shared_ptr;

struct 
refiner_pair
{
  // This structure acts as a mock netsync session. It's only purpose is to
  // construct two refiners that are connected to one another, and route
  // refinement calls back and forth between them.

  struct 
  refiner_pair_callbacks : refiner_callbacks
  {
    refiner_pair & p;
    bool is_client;
    refiner_pair_callbacks(refiner_pair & p, bool is_client) 
      : p(p), is_client(is_client) 
    {}

    virtual void queue_refine_cmd(refinement_type ty,
                                  merkle_node const & our_node)
    {
      p.events.push_back(shared_ptr<msg>(new msg(is_client, ty, our_node)));
    }

    virtual void queue_done_cmd(netcmd_item_type ty,
                                size_t n_items)
    {
      p.events.push_back(shared_ptr<msg>(new msg(is_client, n_items)));
    }
    virtual ~refiner_pair_callbacks() {}
  };

  refiner_pair_callbacks client_cb;
  refiner_pair_callbacks server_cb;
  refiner client;
  refiner server;
  
  struct msg
  {
    msg(bool is_client, refinement_type ty, merkle_node const & node)
      : op(refine),
        ty(ty),
        send_to_client(!is_client),
        node(node)
    {}

    msg(bool is_client, size_t items) 
      : op(done),
        send_to_client(!is_client),
        n_items(items) 
    {}

    enum { refine, done } op;
    refinement_type ty;
    bool send_to_client;
    size_t n_items;
    merkle_node node;
  };

  deque<shared_ptr<msg> > events;
  size_t n_msgs;

  void crank() 
  {
    
    shared_ptr<msg> m = events.front();
    events.pop_front();
    ++n_msgs;

    switch (m->op) 
      {

      case msg::refine:
        if (m->send_to_client)
          client.process_refinement_command(m->ty, m->node);
        else
          server.process_refinement_command(m->ty, m->node);
        break;

      case msg::done:
        if (m->send_to_client)
          client.process_done_command(m->n_items);
        else
          server.process_done_command(m->n_items);
        break;
      }
  }

  refiner_pair(set<id> const & client_items,
               set<id> const & server_items) : 
    client_cb(*this, true),
    server_cb(*this, false),
    // The item type here really doesn't matter.
    client(file_item, client_voice, client_cb),
    server(file_item, server_voice, server_cb),
    n_msgs(0)
  {
    for (set<id>::const_iterator i = client_items.begin();
         i != client_items.end(); ++i)
      client.note_local_item(*i);

    for (set<id>::const_iterator i = server_items.begin();
         i != server_items.end(); ++i)
      server.note_local_item(*i);

    client.reindex_local_items();
    server.reindex_local_items();
    client.begin_refinement();

    while (! events.empty())
      crank();
    
    // Refinement should have completed by here.
    UNIT_TEST_CHECK(client.done);
    UNIT_TEST_CHECK(server.done);

    check_set_differences("client", client);
    check_set_differences("server", server);
    check_no_redundant_sends("client->server", 
                             client.items_to_send, 
                             server.get_local_items());
    check_no_redundant_sends("server->client", 
                             server.items_to_send, 
                             client.get_local_items());
    UNIT_TEST_CHECK(client.items_to_send.size() == server.items_to_receive);
    UNIT_TEST_CHECK(server.items_to_send.size() == client.items_to_receive);
    L(FL("stats: %d total, %d cs, %d sc, %d msgs") 
      % (server.items_to_send.size() + client.get_local_items().size())
      % client.items_to_send.size()
      % server.items_to_send.size()
      % n_msgs);
  }

  void print_if_unequal(char const * context,
                        char const * name1,
                        set<id> const & set1,
                        char const * name2,
                        set<id> const & set2)
  {
    if (set1 != set2)
      {
        L(FL("WARNING: Unequal sets in %s!") % context);
        for (set<id>::const_iterator i = set1.begin(); i != set1.end(); ++i)
          {
            L(FL("%s: %s") % name1 % *i);
          }

        for (set<id>::const_iterator i = set2.begin(); i != set2.end(); ++i)
          {
            L(FL("%s: %s") % name2 % *i);
          }
        L(FL("end of unequal sets"));
      }
  }

  void check_no_redundant_sends(char const * context, 
                                set<id> const & src,
                                set<id> const & dst)
  {
    for (set<id>::const_iterator i = src.begin(); i != src.end(); ++i)
      {
        set<id>::const_iterator j = dst.find(*i);
        if (j != dst.end()) 
          {
            L(FL("WARNING: %s transmission will send redundant item %s")
              % context % *i);
          }
        UNIT_TEST_CHECK(j == dst.end());
      }
  }

  void check_set_differences(char const * context, refiner const & r)
  {
    set<id> tmp;
    set_difference(r.get_local_items().begin(), r.get_local_items().end(),
                   r.get_peer_items().begin(), r.get_peer_items().end(),
                   inserter(tmp, tmp.begin()));
    print_if_unequal(context,
                     "diff(local,peer)", tmp, 
                     "items_to_send", r.items_to_send);

    UNIT_TEST_CHECK(tmp == r.items_to_send);
  }
};


void
check_combinations_of_sets(set<id> const & s0, 
                           set<id> const & a,
                           set<id> const & b)
{
  // Having composed our two input sets s0 and s1, we now construct the 2
  // auxilary union-combinations of them -- {} and {s0 U s1} -- giving 4
  // basic input sets. We then run 9 "interesting" pairwise combinations
  // of these input sets.

  set<id> e, u, v;
  set_union(s0.begin(), s0.end(), a.begin(), a.end(), inserter(u, u.begin()));
  set_union(s0.begin(), s0.end(), b.begin(), b.end(), inserter(v, v.begin()));

  { refiner_pair x(e, u); }   // a large initial transfer
  { refiner_pair x(u, e); }   // a large initial transfer

  { refiner_pair x(s0, u); }  // a mostly-shared superset/subset
  { refiner_pair x(u, s0); }  // a mostly-shared superset/subset

  { refiner_pair x(a, u); }   // a mostly-unshared superset/subset
  { refiner_pair x(u, a); }   // a mostly-unshared superset/subset

  { refiner_pair x(u, v); }   // things to send in both directions
  { refiner_pair x(v, u); }   // things to send in both directions

  { refiner_pair x(u, u); }   // a large no-op
}


void 
build_random_set(set<id> & s, size_t sz, bool clumpy, randomizer & rng)
{
  while (s.size() < sz)
    {
      string str(constants::merkle_hash_length_in_bytes, ' ');
      for (size_t i = 0; i < constants::merkle_hash_length_in_bytes; ++i)
        str[i] = static_cast<char>(rng.uniform(0xff));
      s.insert(id(str));
      if (clumpy && rng.flip())
        {
          size_t clumpsz = rng.uniform(7) + 1;
          size_t pos = rng.flip() ? str.size() - 1 : rng.uniform(str.size());
          for (size_t i = 0; s.size() < sz && i < clumpsz; ++i)
            {
              char c = str[pos];
              if (c == static_cast<char>(0xff))
                break;
              ++c;
              str[pos] = c;
              s.insert(id(str));
            }          
        }
    }
}

size_t 
perturbed(size_t n, randomizer & rng)
{
  // we sometimes perturb sizes to deviate a bit from natural word-multiple sizes
  if (rng.flip())
    return n + rng.uniform(5);
  return n;
}

size_t
modulated_size(size_t base_set_size, size_t i)
{
  if (i < 3)
    return i+1;
  else
    return static_cast<size_t>((static_cast<double>(i - 2) / 5.0)
                               * static_cast<double>(base_set_size));
}


void 
check_with_count(size_t base_set_size, randomizer & rng)
{
  if (base_set_size == 0) 
    return;

  L(FL("running refinement check with base set size %d") % base_set_size);

  // Our goal here is to construct a base set of a given size, and two
  // secondary sets which will be combined with the base set in various
  // ways. 
  //
  // The secondary sets will be built at the following sizes:
  //
  // 1 element
  // 2 elements
  // 3 elements
  // 0.2 * size of base set
  // 0.4 * size of base set
  // 0.8 * size of base set
  //
  // The base set is constructed in both clumpy and non-clumpy forms,
  // making 6 * 6 * 2 = 72 variations.
  // 
  // Since each group of sets creates 9 sync scenarios, each "size" creates
  // 648 sync scenarios.
  
  for (size_t c = 0; c < 2; ++c)
    {
      set<id> s0;
      build_random_set(s0, perturbed(base_set_size, rng), c == 0, rng);

      for (size_t a = 0; a < 6; ++a)
        {
          set<id> sa;
          build_random_set(sa, modulated_size(perturbed(base_set_size, rng), a), false, rng);
          
          for (size_t b = 0; b < 6; ++b)
            {
              set<id> sb;
              build_random_set(sb, modulated_size(perturbed(base_set_size, rng), b), false, rng);
              check_combinations_of_sets(s0, sa, sb);
            }
        }
    }          
}

UNIT_TEST(refiner, various_counts)
{
  { 
    // Once with zero-zero, for good measure.
    set<id> s0;
    refiner_pair x(s0, s0); 
  }

  // We run 3 primary counts, giving 1944 tests. Note that there is some
  // perturbation within the test, so we're not likely to feel side effects
  // of landing on such pleasant round numbers.

  randomizer rng;
  check_with_count(1, rng); 
  check_with_count(128, rng); 
  check_with_count(1024, rng); 
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
