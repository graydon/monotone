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
    empty_state,
    live_leaf_state,
    dead_leaf_state,
    subtree_state
  }
slot_state;

struct merkle_node
{    
  u8 level;
  boost::dynamic_bitset<char> prefix;
  u64 total_num_leaves;
  boost::dynamic_bitset<char> bitmap;
  std::map<size_t, std::string> slots;

  merkle_node();
  bool operator==(merkle_node const & other) const;
  std::string node_identifier() const;
  boost::dynamic_bitset<char> extended_prefix(size_t subtree) const;
  slot_state get_slot_state(size_t n) const;
  void set_slot_state(size_t n, slot_state st);
};

size_t prefix_length_in_bits(size_t level);
size_t prefix_length_in_bytes(size_t level);
void write_node(merkle_node const & in, std::string & outbuf);
void read_node(std::string const & inbuf, merkle_node & out);

std::string raw_sha1(std::string const & in);

// these operate against the database
 
void load_merkle_node(app_state & app,
		      std::string const & type,
		      utf8 const & collection,			
		      size_t level,
		      hexenc<prefix> const & hpref,
		      merkle_node & node);

// returns the first hashsz bytes of the serialized node, which is 
// the hash of its contents.

std::string store_merkle_node(app_state & app,
			      std::string const & type,
			      utf8 const & collection,
			      merkle_node & node);

// this inserts a leaf into the appropriate position in a merkle
// tree, writing it to the db and updating any other nodes in the
// tree which are affected by the insertion.

std::string insert_into_merkle_tree(app_state & app,
				    bool live_p,
				    std::string const & type,
				    utf8 const & collection,
				    std::string const & leaf,
				    size_t level);

#endif // __MERKLE_TREE_HH__
