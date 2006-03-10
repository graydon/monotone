#ifndef __MERKLE_TREE_HH__
#define __MERKLE_TREE_HH__
// copyright (C) 2004, 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include <boost/dynamic_bitset.hpp>

#include "app_state.hh"
#include "numeric_vocab.hh"
#include "vocab.hh"
#include "transforms.hh"

// This file contains data structures and functions for managing merkle
// trees. A merkle tree is, conceptually, a general recursive construction
// whereby a range of K data elements is divided up into buckets. Each
// bucket is then hashed, and the hash values of the buckets at level N of
// the tree are used as data elements held in buckets at level N-1. At
// level 0 there is only one bucket.
//
// The result is a tree in which each node has J "slots", each of which
// summarizes (as a hashcode) the entire subtree beneath it. this makes a
// pair of merkle trees amenable to setwise operations such as union or
// difference while only inspecting D*log_base_J(K) nodes where D is the
// number of differences between trees.
//
// We build merkle trees over a few collections of objects in our database
// and use these to synchronize with remote hosts. See netsync.{cc,hh} and
// refiner.{cc,hh} for more details.

typedef enum
  {
    file_item = 2,
    key_item = 3,    
    revision_item = 4,
    cert_item = 5,
    epoch_item = 6
  }
netcmd_item_type;

void netcmd_item_type_to_string(netcmd_item_type t, std::string & typestr);

typedef enum
  {
    empty_state,
    leaf_state,
    subtree_state
  }
slot_state;

struct merkle_node
{    
  size_t level;
  boost::dynamic_bitset<unsigned char> pref;
  size_t total_num_leaves;
  boost::dynamic_bitset<unsigned char> bitmap;
  std::vector<id> slots;
  netcmd_item_type type;

  merkle_node();
  bool operator==(merkle_node const & other) const;
  void check_invariants() const;

  void get_raw_prefix(prefix & pref) const;
  void get_hex_prefix(hexenc<prefix> & hpref) const;

  void get_raw_slot(size_t slot, id & val) const;
  void get_hex_slot(size_t slot, hexenc<id> & val) const;

  void set_raw_slot(size_t slot, id const & val);
  void set_hex_slot(size_t slot, hexenc<id> const & val);

  void extended_prefix(size_t slot, boost::dynamic_bitset<unsigned char> & extended) const;
  void extended_raw_prefix(size_t slot, prefix & extended) const;
  void extended_hex_prefix(size_t slot, hexenc<prefix> & extended) const;

  slot_state get_slot_state(size_t n) const;
  void set_slot_state(size_t n, slot_state st);
};

typedef boost::shared_ptr<merkle_node> merkle_ptr;
typedef std::map<std::pair<prefix,size_t>, merkle_ptr> merkle_table;

size_t prefix_length_in_bits(size_t level);
size_t prefix_length_in_bytes(size_t level);
void write_node(merkle_node const & in, std::string & outbuf);
void read_node(std::string const & inbuf, size_t & pos, merkle_node & out);

std::string raw_sha1(std::string const & in);


bool
locate_item(merkle_table & table,
            id const & val,
            size_t & slotnum,
            merkle_ptr & mp);


void 
pick_slot_and_prefix_for_value(id const & val,
                               size_t level,
                               size_t & slotnum,
                               boost::dynamic_bitset<unsigned char> & pref);

// Collect the items inside a subtree.

void
collect_items_in_subtree(merkle_table & tab,
                         prefix const & pref,
                         size_t level,
                         std::set<id> & items);

// Insert an item into a tree.

void 
insert_into_merkle_tree(merkle_table & tab,
                        netcmd_item_type type,
                        id const & leaf,
                        size_t level);

inline void
insert_into_merkle_tree(merkle_table & tab,
                        netcmd_item_type type,
                        hexenc<id> const & hex_leaf,
                        size_t level)
{
  id leaf;
  decode_hexenc(hex_leaf, leaf);
  insert_into_merkle_tree(tab, type, leaf, level);
}

// Recalculate the hashes in the given tree. Must be called after
// insert_into_merkle_tree, and before using tree (but you can batch up
// multiple calls to insert_into_merkle_tree and then only call this once).

id 
recalculate_merkle_codes(merkle_table & tab,
                         prefix const & pref, 
                         size_t level);
  
#endif // __MERKLE_TREE_HH__
