#ifndef __MERKLE_TREE_HH__
#define __MERKLE_TREE_HH__
// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include <boost/dynamic_bitset.hpp>

#include "app_state.hh"
#include "numeric_vocab.hh"
#include "vocab.hh"

// this file contains data structures and functions for managing merkle
// trees. a merkle tree is a general construction whereby a range of K data
// elements is divided up into buckets, the buckets are stored on disk,
// then hashed, and the hash values of the buckets are used as data
// elements for another iteration of the process. this terminates when you
// only have 1 bucket left.
//
// the result is a tree in which each node has N "slots", each of which
// summarizes (as a hashcode) the entire subtree beneath it. this makes a
// pair of merkle trees amenable to setwise operations such as union or
// difference while only inspecting D*log(K) nodes where D is the number of
// differences between trees.
//
// we build merkle trees over a few collections of objects in our database
// and use these to synchronize with remote hosts. see netsync.{cc,hh} for
// more details.

typedef enum
  {
    manifest_item = 1,
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
    live_leaf_state,
    dead_leaf_state,
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
void read_node(std::string const & inbuf, merkle_node & out);

std::string raw_sha1(std::string const & in);

void pick_slot_and_prefix_for_value(id const & val, size_t level, 
                                    size_t & slotnum, boost::dynamic_bitset<unsigned char> & pref);

// inserts an item into a tree

void 
insert_into_merkle_tree(merkle_table & tab,
                        netcmd_item_type type,
                        bool live_p,
                        id const & leaf,
                        size_t level);

// recalculates the hashes in the given tree.  must be called after
// insert_into_merkle_tree, and before using tree (but you can batch up
// multiple calls to insert_into_merkle_tree and then only call this once).

id 
recalculate_merkle_codes(merkle_table & tab,
                         prefix const & pref, 
                         size_t level);
  
#endif // __MERKLE_TREE_HH__
