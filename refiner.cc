
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <set>
#include <string>

#include <boost/shared_ptr.hpp>

#include "refiner.hh"
#include "vocab.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"

using std::string;
using std::set;

// The previous incarnation of this algorithm had code related to sending
// decisions (and skippable transmissions) mixed in with the refinement
// actions.
//
// This incarnation is much simpler: our goal is only to learn the complete
// set of items in our peer's tree, and inform our peer of every item we
// have in our tree. To do this we must perform a complete refinement, and
// record the results in an in-memory table. We will decide what to send
// elsewhere, based on this knowledge.

void 
refiner::note_local_item(id const & item)
{
  insert_into_merkle_tree(table, type, item, 0);
}


void
refiner::reindex_local_items()
{
  recalculate_merkle_codes(table, prefix(""), 0);
}


void
refiner::refine_synthetic_empty_subtree(merkle_node const & their_node,
                                        size_t slot)
{
  // Our peer has a subtree, we have nothing. We want to explore their
  // subtree but we have nothing real to send, so we synthesize an empty
  // node and send it as a refinement request.
  merkle_node our_fake_node;
  their_node.extended_prefix(slot, our_fake_node.pref);
  our_fake_node.level = their_node.level + 1;
  our_fake_node.type = their_node.type;
  cb.queue_refine_cmd(our_fake_node);
}


void
refiner::refine_synthetic_singleton_subtree(merkle_node const & their_node,
                                            merkle_node const & our_node,
                                            size_t slot)
{
  // Our peer has a subtree, we have a single leaf. We want to explore
  // their subtree but we have nothing real to send, so we synthesize an
  // empty subtree and push our leaf into it, sending a refinement request
  // on the new (fake) subtree.
  size_t subslot;
  id our_slotval;
  merkle_node our_fake_subtree;
  our_node.get_raw_slot(slot, our_slotval);  
  pick_slot_and_prefix_for_value(our_slotval, our_node.level + 1, 
                                 subslot, our_fake_subtree.pref);
  our_fake_subtree.type = their_node.type;
  our_fake_subtree.level = our_node.level + 1;
  our_fake_subtree.set_raw_slot(subslot, our_slotval);
  our_fake_subtree.set_slot_state(subslot, our_node.get_slot_state(slot));
  cb.queue_refine_cmd(our_fake_subtree);
}


void
refiner::inform_peer_of_item_in_slot(merkle_node const & our_node,
                                     size_t slot)
{
  id slotval;
  string tmp;
  our_node.get_raw_slot(slot, slotval);
  cb.queue_note_item_cmd(type, slotval);
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
refiner::calculate_items_to_send_and_receive()
{
  items_to_send.clear();
  items_to_receive.clear();

  std::set_difference(local_items.begin(), local_items.end(),
                      peer_items.begin(), peer_items.end(),
                      std::inserter(items_to_send, items_to_send.begin()));

  std::set_difference(peer_items.begin(), peer_items.end(),
                      local_items.begin(), local_items.end(),
                      std::inserter(items_to_receive, items_to_receive.begin()));
}


void
refiner::inform_peer_of_subtree_in_slot(merkle_node const & our_node,
                                        size_t slot)
{
  prefix subprefix;
  our_node.extended_raw_prefix(slot, subprefix);
  merkle_ptr our_subtree;
  load_merkle_node(our_node.level + 1, subprefix, our_subtree);
  cb.queue_refine_cmd(*our_subtree);
}

void
refiner::note_subtree_shared_with_peer(merkle_node const & our_subtree)
{
  prefix pref;
  our_subtree.get_raw_prefix(pref);
  collect_items_in_subtree(table, pref, our_subtree.level, peer_items);
}

void 
refiner::note_subtree_shared_with_peer(prefix const & pref, size_t lev)
{
  collect_items_in_subtree(table, pref, lev, peer_items);
}


void
refiner::compare_subtrees_and_maybe_refine(merkle_node const & their_node,
                                           merkle_node const & our_node,
                                           size_t slot)
{
  // Our peer has a subtree at slot, and so do we.
  //
  // There are three things to do here:
  //
  //  1. If we have the same subtree as the peer, for every item in our
  //     subtree, make a note to ourself that the peer has that item too.
  //
  //  2. If we have the same subtree, make sure our peer knows it, so 
  //     they can perform #1 for themselves.
  //
  //  3. If we have different subtrees, refine.

  id our_slotval, their_slotval;
  their_node.get_raw_slot(slot, their_slotval);
  our_node.get_raw_slot(slot, our_slotval);

  prefix pref;
  our_node.extended_raw_prefix(slot, pref);
  merkle_ptr our_subtree;
  size_t level = our_node.level + 1;
  load_merkle_node(level, pref, our_subtree);
  
  if (their_slotval == our_slotval)
    {
      cb.queue_note_shared_subtree_cmd(type, pref, level);
      note_subtree_shared_with_peer(*our_subtree);
    }
  else
    cb.queue_refine_cmd(*our_subtree);
}


refiner::refiner(netcmd_item_type type, refiner_callbacks & cb) 
  : type(type), cb(cb), 
    exchanged_data_since_last_done_cmd(false), 
    finished_refinement(false)
{}

void
refiner::note_item_in_peer(id const & item)
{
  peer_items.insert(item);
}


void
refiner::note_item_in_peer(merkle_node const & their_node,
                           size_t slot)
{
  I(slot < constants::merkle_num_slots);
  id slotval;
  their_node.get_raw_slot(slot, slotval);  

  note_item_in_peer(slotval);

  // Write a debug message
  {
    hexenc<id> hslotval;
    their_node.get_hex_slot(slot, hslotval);
    
    size_t lev = static_cast<size_t>(their_node.level);
    
    hexenc<prefix> hpref;
    their_node.get_hex_prefix(hpref);
    
    string typestr;
    netcmd_item_type_to_string(their_node.type, typestr);
    
    L(boost::format("peer has %s '%s' at slot %d "
                    "(in node '%s', level %d)\n")
      % typestr % hslotval % slot % hpref % lev);
  }
}


void 
refiner::begin_refinement()
{
  merkle_ptr root;
  load_merkle_node(0, prefix(""), root);
  cb.queue_refine_cmd(*root);
  cb.queue_done_cmd(0, type);
}


void 
refiner::process_done_command(size_t level)
{
  if (!exchanged_data_since_last_done_cmd 
      || level >= 0xff)
    {
      // Echo 'done' if we're shutting down
      if (!finished_refinement)
        cb.queue_done_cmd(level+1, type);
      
      // Mark ourselves shut down
      finished_refinement = true;

      // And prepare for queries from our host
      calculate_items_to_send_and_receive();
    }
  else if (exchanged_data_since_last_done_cmd 
           && !finished_refinement)
    {
      // Echo 'done', we're still active.
      cb.queue_done_cmd(level+1, type);
    }
  
  // Reset exchanged_data_since_last_done_cmd
  exchanged_data_since_last_done_cmd = false;
}

bool 
refiner::done() const
{
  return finished_refinement;
}


void 
refiner::process_peer_node(merkle_node const & their_node)
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
  
  if (!merkle_node_exists(their_node.level, pref))
    {
      L(F("no corresponding %s merkle node for prefix '%s', level %d\n")
        % typestr % hpref % lev);
      
      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {
          switch (their_node.get_slot_state(slot))
            {
            case empty_state:
              // We agree, this slot is empty.
              break;

            case leaf_state:
              note_item_in_peer(their_node, slot);
              break;

            case subtree_state:
              refine_synthetic_empty_subtree(their_node, slot);
              break;
            }
        }
    }
  else
    {
      // We have a corresponding merkle node. There are 9 branches
      // to the following switch condition. It is awful. Sorry.
      L(F("found corresponding %s merkle node for prefix '%s', level %d\n")
        % typestr % hpref % lev);
      merkle_ptr our_node;
      load_merkle_node(their_node.level, pref, our_node);

      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {         
          switch (their_node.get_slot_state(slot))
            {
            case empty_state:
              switch (our_node->get_slot_state(slot))
                {

                case empty_state:
                  // 1: theirs == empty, ours == empty 
                  break;

                case leaf_state:
                  // 2: theirs == empty, ours == leaf 
                  inform_peer_of_item_in_slot(*our_node, slot);
                  break;

                case subtree_state:
                  // 3: theirs == empty, ours == subtree 
                  inform_peer_of_subtree_in_slot(*our_node, slot);
                  break;

                }
              break;


            case leaf_state:
              switch (our_node->get_slot_state(slot))
                {

                case empty_state:
                  // 4: theirs == leaf, ours == empty
                  note_item_in_peer(their_node, slot);
                  break;

                case leaf_state:
                  // 5: theirs == leaf, ours == leaf
                  note_item_in_peer(their_node, slot);
                  inform_peer_of_item_in_slot(*our_node, slot);
                  break;

                case subtree_state:
                  // 6: theirs == leaf, ours == subtree
                  note_item_in_peer(their_node, slot);
                  inform_peer_of_subtree_in_slot(*our_node, slot);
                  break;
                }
              break;

            case subtree_state:
              switch (our_node->get_slot_state(slot))
                {
                case empty_state:                  
                  // 7: theirs == subtree, ours == empty 
                  refine_synthetic_empty_subtree(their_node, slot);
                  break;

                case leaf_state:
                  // 14: theirs == subtree, ours == leaf 
                  refine_synthetic_singleton_subtree(their_node, 
                                                     *our_node, slot);
                  break;
                  
                case subtree_state:
                  // 16: theirs == subtree, ours == subtree 
                  compare_subtrees_and_maybe_refine(their_node, 
                                                    *our_node, slot);
                  break;                  
                }
              break;
            }
        }
    }
}



