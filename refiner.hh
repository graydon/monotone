#ifndef __REFINER_HH__
#define __REFINER_HH__

// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include "vocab.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"

// This file defines the "refiner" class, which is a helper encapsulating
// the main tricky part of the netsync algorithm. You must construct a
// refiner for every merkle trie you wish to refine, and pass it a
// reference to a refiner_callbacks object, such as the netsync session
// object. Refinement proceeds in stages.
// 
// 1. Add local items.
//
// 2. Call reindex_local_items to index the merkle table. 
//
// 3. Call begin_refinement, and process the queue_refine_cmd callback
//    this will generate.
//
// 4. Call process_peer_node repeatedly as nodes arrive from your peer,
//    processing the callbacks each such call generates.
//
// 5. When done, stop refining and examine the sets of local and peer
//    items you've determined the existence of during refinement.


struct
refiner_callbacks
{
  virtual void queue_refine_cmd(merkle_node const & our_node) = 0;
  virtual void queue_note_item_cmd(netcmd_item_type ty, id item) = 0;
  virtual void queue_note_shared_subtree_cmd(netcmd_item_type ty, 
                                             prefix const & pref,
                                             size_t level) = 0;                              
  virtual void queue_done_cmd(size_t level,
                              netcmd_item_type ty) = 0;
  virtual ~refiner_callbacks() {}
};

class
refiner
{
  netcmd_item_type type;
  refiner_callbacks & cb;
  bool exchanged_data_since_last_done_cmd;
  size_t finished_refinement;
  bool calculated_items_to_send_and_receive;

  std::set<id> local_items;
  std::set<id> peer_items;
  merkle_table table;

  void refine_synthetic_empty_subtree(merkle_node const & their_node,
                                      size_t slot);
  void refine_synthetic_singleton_subtree(merkle_node const & their_node,
                                          merkle_node const & our_node,
                                          size_t slot);
  void inform_peer_of_item_in_slot(merkle_node const & our_node, size_t slot);
  void inform_peer_of_subtree_in_slot(merkle_node const & our_node, size_t slot);
  void note_subtree_shared_with_peer(merkle_node const & our_subtree);
  void compare_subtrees_and_maybe_refine(merkle_node const & their_node,
                                         merkle_node const & our_node,
                                         size_t slot);
  void note_item_in_peer(merkle_node const & their_node, size_t slot);
  void load_merkle_node(size_t level, prefix const & pref,
                        merkle_ptr & node);
  bool merkle_node_exists(size_t level, prefix const & pref);
  void calculate_items_to_send_and_receive();

public:

  refiner(netcmd_item_type type, refiner_callbacks & cb);
  void note_item_in_peer(id const & item);
  void note_subtree_shared_with_peer(prefix const & pref, size_t lev);
  void note_local_item(id const & item);
  void reindex_local_items();
  void begin_refinement();
  bool done() const;
  void process_done_command(size_t level);
  void process_peer_node(merkle_node const & their_node);

  // These are populated as the 'done' packets arrive.
  std::set<id> items_to_send;
  std::set<id> items_to_receive;  
};


#endif // __REFINER_H__
