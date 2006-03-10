
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include <boost/shared_ptr.hpp>

#include "refiner.hh"
#include "vocab.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"
#include "netsync.hh"

using std::string;
using std::set;
using std::make_pair;

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
  merkle_table::const_iterator j = table.find(std::make_pair(pref, level));  
  I(j != table.end());
  node = j->second;
}

bool
refiner::merkle_node_exists(size_t level,
                            prefix const & pref)
{
  merkle_table::const_iterator j = table.find(std::make_pair(pref, level));
  return (j != table.end());
}

void 
refiner::calculate_items_to_send()
{
  if (calculated_items_to_send)
    return;

  items_to_send.clear();
  items_to_receive = 0;

  std::set_difference(local_items.begin(), local_items.end(),
                      peer_items.begin(), peer_items.end(),
                      std::inserter(items_to_send, items_to_send.begin()));

  string typestr;
  netcmd_item_type_to_string(type, typestr);
  
  L(FL("determined %d %s items to send") % items_to_send.size() % typestr);  
  calculated_items_to_send = true;
}


void
refiner::send_subquery(merkle_node const & our_node, size_t slot)
{
  prefix subprefix;
  our_node.extended_raw_prefix(slot, subprefix);
  merkle_ptr our_subtree;
  load_merkle_node(our_node.level + 1, subprefix, our_subtree);
  cb.queue_refine_cmd(refinement_query, *our_subtree);
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
  {
    hexenc<id> hslotval;
    their_node.get_hex_slot(slot, hslotval);
        
    hexenc<prefix> hpref;
    their_node.get_hex_prefix(hpref);
    
    string typestr;
    netcmd_item_type_to_string(their_node.type, typestr);
    
    L(FL("peer has %s '%s' at slot %d (in node '%s', level %d)\n")
      % typestr % hslotval % slot % hpref % their_node.level);
  }
}


void 
refiner::begin_refinement()
{
  merkle_ptr root;
  load_merkle_node(0, prefix(""), root);
  cb.queue_refine_cmd(refinement_query, *root);
  ++queries_in_flight;
  sent_initial_query = true;
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("Beginning %s refinement.") % typestr);
}

void 
refiner::process_done_command(size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  calculate_items_to_send();
  items_to_receive = n_items;

  L(FL("finished %s refinement: %d to send, %d to receive")
    % typestr % items_to_send.size() % items_to_receive);

  if (voice == server_voice)
    cb.queue_done_cmd(type, items_to_send.size());

  done = true;
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
  
  L(FL("received refinement %s netcmd on %s node '%s', level %d") 
    % (ty == refinement_query ? "query" : "response") % typestr % hpref % lev);
  
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
        {
          note_item_in_peer(their_node, slot);
          // If we have their leaf somewhere in our subtree,
          // we need to tell them.
          if (our_node->get_slot_state(slot) == subtree_state)
            {
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
        }
      
      // Compare any subtrees, if we both have subtrees.
      if (our_node->get_slot_state(slot) == subtree_state
          && their_node.get_slot_state(slot) == subtree_state)
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

      // Note: if they had a leaf (or empty) where I had a subtree, I
      // will have noted the leaf and will not send it. They will not
      // have any of the *other* parts of my subtree, so it's ok if I
      // eventually wind up sending the subtree-minus-their-leaf.
    }
  
  if (ty == refinement_response)
    {
      E((queries_in_flight > 0),
        F("underflow on query-in-flight counter"));
      --queries_in_flight;

      // Possibly this signals the end of refinement.
      if (voice == client_voice && queries_in_flight == 0)
        {
          calculate_items_to_send();
          cb.queue_done_cmd(type, items_to_send.size());
        }
    }
  else
    {
      // Always reply to every query with the current node.
      I(ty == refinement_query);
      cb.queue_refine_cmd(refinement_response, *our_node);
    }
}



