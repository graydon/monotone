#ifndef __REFINER_HH__
#define __REFINER_HH__

// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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
  virtual void queue_refine_cmd(refinement_type ty,
				merkle_node const & our_node) = 0;
  virtual void queue_done_cmd(netcmd_item_type ty,
			      size_t n_items) = 0;
  virtual ~refiner_callbacks() {}
};

class
refiner
{
  netcmd_item_type type;
  protocol_voice voice;
  refiner_callbacks & cb;

  bool sent_initial_query;
  size_t queries_in_flight;
  bool calculated_items_to_send;

  std::set<id> local_items;
  std::set<id> peer_items;
  merkle_table table;

  void note_subtree_shared_with_peer(merkle_node const & our_node, size_t slot);
  void send_subquery(merkle_node const & our_node, size_t slot);
  void send_synthetic_subquery(merkle_node const & our_node, size_t slot);
  void note_item_in_peer(merkle_node const & their_node, size_t slot);
  void load_merkle_node(size_t level, prefix const & pref,
                        merkle_ptr & node);
  bool merkle_node_exists(size_t level, prefix const & pref);
  void calculate_items_to_send();
  std::string voicestr() 
  { 
    return voice == server_voice ? "server" : "client"; 
  }  

public:

  refiner(netcmd_item_type type, protocol_voice voice, refiner_callbacks & cb);
  void note_local_item(id const & item);
  void reindex_local_items();
  void begin_refinement();
  void process_done_command(size_t n_items);
  void process_refinement_command(refinement_type ty, merkle_node const & their_node);
  bool local_item_exists(id const & ident)
  {
    return local_items.find(ident) != local_items.end();
  }

  std::set<id> const & get_local_items() const { return local_items; }
  std::set<id> const & get_peer_items() const { return peer_items; }

  // These are populated as the 'done' packets arrive.
  bool done;
  std::set<id> items_to_send;
  size_t items_to_receive;
};


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __REFINER_H__
