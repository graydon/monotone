// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include <boost/dynamic_bitset.hpp>

#include "cryptopp/sha.h"

#include "constants.hh"
#include "merkle_tree.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace boost;
using namespace std;
using namespace CryptoPP;

// this is a *raw* SHA1, not the nice friendly hex-encoded type. it is half
// as many bytes. since merkle nodes are mostly nothing but SHA1 values,
// and we have to send them over the wire, we use a raw variant here
// for compactness.

string raw_sha1(string const & in)
{
  SHA hash;
  hash.Update(reinterpret_cast<byte const *>(in.data()), 
	      static_cast<unsigned int>(in.size()));
  char digest[SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  string out(digest, SHA::DIGESTSIZE);
  return out;
}


merkle_node::merkle_node() : level(0), prefix(0), 
			     total_num_leaves(0), 
			     bitmap(constants::merkle_bitmap_length_in_bits) {}

bool merkle_node::operator==(merkle_node const & other) const
{
  return (level == other.level
	  && prefix == other.prefix
	  && total_num_leaves == other.total_num_leaves
	  && bitmap == other.bitmap
	  && slots == other.slots);
}

string merkle_node::node_identifier() const
{
  ostringstream oss;
  oss.put(level);
  to_block_range(prefix, ostream_iterator<char>(oss));
  return oss.str();
}

dynamic_bitset<char> merkle_node::extended_prefix(size_t subtree) const
{
  I(subtree < constants::merkle_num_slots);
  dynamic_bitset<char> new_prefix = prefix;
  for (size_t i = constants::merkle_fanout_bits; i > 0; --i)
    new_prefix.push_back(subtree & (1 << (i-1)));
  return new_prefix;
}

slot_state merkle_node::get_slot_state(size_t n) const
{
  I(n < constants::merkle_num_slots);
  I(2*n + 1 < bitmap.size());
  if (bitmap[2*n])
    {
      if (bitmap[2*n+1])
	return subtree_state;
      else
	return live_leaf_state;
    }
  else
    {
      if (bitmap[2*n+1])
	return dead_leaf_state;
      else
	return empty_state;
    }      
}

void merkle_node::set_slot_state(size_t n, slot_state st)
{
  I(n < constants::merkle_num_slots);
  I(2*n + 1 < bitmap.size());
  bitmap.reset(2*n);
  bitmap.reset(2*n+1);
  if (st == subtree_state || st == live_leaf_state)
    bitmap.set(2*n);
  if (st == subtree_state || st == dead_leaf_state)
    bitmap.set(2*n+1);
}    


size_t prefix_length_in_bits(size_t level)
{
  return level * constants::merkle_fanout_bits;
}

size_t prefix_length_in_bytes(size_t level)
{
  // level is the number of levels in tree this prefix has.
  // the prefix's binary *length* is the number of bytes used
  // to represent it, rounded up to a byte.
  size_t num_bits = prefix_length_in_bits(level);
  if (num_bits % 8 == 0)
    return num_bits / 8;
  else
    return (num_bits / 8) + 1;
}

void write_node(merkle_node const & in, string & outbuf)
{      
  ostringstream oss;
  oss.put(in.level);
  to_block_range(in.prefix, ostream_iterator<char>(oss));

  string tmp;
  write_datum_msb<u64>(in.total_num_leaves, tmp);
  oss.write(tmp.data(), tmp.size());

  to_block_range(in.bitmap, ostream_iterator<char>(oss));

  for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
    {
      if (in.get_slot_state(slot) != empty_state)
	{
	  I(in.slots.find(slot) != in.slots.end());
	  string slot_val = in.slots.find(slot)->second;
	  oss.write(slot_val.data(), slot_val.size());
	}
    }
  string hash = raw_sha1(oss.str());
  I(hash.size() == constants::merkle_hash_length_in_bytes);
  outbuf = hash + oss.str();
}
    
void read_node(string const & inbuf, merkle_node & out)
{
  size_t pos = 0;
  string hash = extract_substring(inbuf, pos, constants::merkle_hash_length_in_bytes, "node hash");
  out.level = extract_datum_msb<u8>(inbuf, pos, "node level");

  if (out.level >= constants::merkle_num_tree_levels)
    throw bad_decode(F("node level is %d, exceeds maximum %d") 
		     % static_cast<int>(out.level)
		     % static_cast<int>(constants::merkle_num_tree_levels));

  size_t prefixsz = prefix_length_in_bytes(out.level);
  require_bytes(inbuf, pos, prefixsz, "node prefix");   
  out.prefix.resize(prefix_length_in_bits(out.level));
  from_block_range(inbuf.begin() + pos, 
		   inbuf.begin() + pos + prefixsz,
		   out.prefix);
  pos += prefixsz;

  out.total_num_leaves = extract_datum_msb<u64>(inbuf, pos, "number of leaves");

  require_bytes(inbuf, pos, constants::merkle_bitmap_length_in_bytes, "bitmap");
  out.bitmap.resize(constants::merkle_bitmap_length_in_bits);
  from_block_range(inbuf.begin() + pos, 
		   inbuf.begin() + pos + constants::merkle_bitmap_length_in_bytes,
		   out.bitmap);
  pos += constants::merkle_bitmap_length_in_bytes;

  for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
    {
      if (out.get_slot_state(slot) != empty_state)
	{
	  string slot_val = extract_substring(inbuf, pos, 
					      constants::merkle_hash_length_in_bytes, 
					      "slot value");
	  out.slots.insert(make_pair(slot, slot_val));
	}
    }
    
  assert_end_of_buffer(inbuf, pos, "node");
  string checkhash = raw_sha1(inbuf.substr(constants::merkle_hash_length_in_bytes));
  if (hash != checkhash)
    throw bad_decode(F("mismatched node hash value %s, expected %s") 
		     % xform<HexEncoder>(checkhash) % xform<HexEncoder>(hash));
}


void load_merkle_node(app_state & app,
		      string const & type,
		      utf8 const & collection,			
		      size_t level,
		      hexenc<prefix> const & hpref,
		      merkle_node & node)
{
  base64<merkle> emerk;
  merkle merk;  
  app.db.get_merkle_node(type, collection, level, hpref, emerk);
  decode_base64(emerk, merk);
  read_node(merk(), node);
}

// returns the first hashsz bytes of the serialized node, which is 
// the hash of its contents.

string store_merkle_node(app_state & app,
			 string const & type,
			 utf8 const & collection,
			 merkle_node & node)
{
  string out;
  ostringstream oss;
  to_block_range(node.prefix, ostream_iterator<char>(oss));
  hexenc<prefix> hpref;
  encode_hexenc(prefix(oss.str()), hpref);
  write_node(node, out);
  base64<merkle> emerk;
  encode_base64(merkle(out), emerk);
  app.db.put_merkle_node(type, collection, node.level, hpref, emerk);
  I(out.size() >= constants::merkle_hash_length_in_bytes);
  return out.substr(0, constants::merkle_hash_length_in_bytes);
}

string insert_into_merkle_tree(app_state & app,
			       bool live_p,
			       string const & type,
			       utf8 const & collection,
			       string const & leaf,
			       size_t level)
{
  I(constants::merkle_hash_length_in_bytes == leaf.size());
  I(constants::merkle_fanout_bits * (level + 1) 
    <= constants::merkle_hash_length_in_bits);
  
  hexenc<id> hleaf;
  encode_hexenc(id(leaf), hleaf);
  
  dynamic_bitset<char> pref;
  pref.resize(leaf.size() * 8);
  from_block_range(leaf.begin(), leaf.end(), pref);

  size_t slotnum = 0;
  for (size_t i = constants::merkle_fanout_bits; i > 0; --i)
    {
      slotnum <<= 1;
      if (pref[level * constants::merkle_fanout_bits + (i-1)])
	slotnum |= static_cast<size_t>(1);
      else
	slotnum &= static_cast<size_t>(~1);
    }

  pref.resize(level * constants::merkle_fanout_bits);
  ostringstream oss;
  to_block_range(pref, ostream_iterator<char>(oss));
  hexenc<prefix> hpref;
  encode_hexenc(prefix(oss.str()), hpref);

  L(F("inserting %s leaf %s into slot 0x%x at %s node with prefix %s, level %d\n") 
    % (live_p ? "live" : "dead") % hleaf % slotnum % type % hpref % level);
  
  merkle_node node;
  if (app.db.merkle_node_exists(type, collection, level, hpref))
    {
      load_merkle_node(app, type, collection, level, hpref, node);
      slot_state st = node.get_slot_state(slotnum);
      switch (st)
	{
	case live_leaf_state:
	case dead_leaf_state:
	  if (node.slots[slotnum] == leaf)
	    {
	      L(F("found existing entry for %s at slot 0x%x of %s node %s, level %d\n") 
		% hleaf % slotnum % type % hpref % level);
	      if (st == dead_leaf_state && live_p)
		{
		  L(F("changing setting from dead to live, for %s at slot 0x%x of %s node %s, level %d\n") 
		    % hleaf % slotnum % type % hpref % level);
		  node.set_slot_state(slotnum, live_leaf_state);
		}
	      else if (st == live_leaf_state && !live_p)
		{
		  L(F("changing setting from live to dead, for %s at slot 0x%x of %s node %s, level %d\n") 
		    % hleaf % slotnum % type % hpref % level);
		  node.set_slot_state(slotnum, dead_leaf_state);
		}
	    }
	  else
	    {
	      L(F("pushing existing leaf %s in slot 0x%x of %s node %s, level %d into subtree\n")
		% hleaf % slotnum % type % hpref % level);
	      insert_into_merkle_tree(app, (st == live_leaf_state ? true : false),
				      type, collection, node.slots[slotnum], level+1);
	      string subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
	      hexenc<id> hsub;
	      encode_hexenc(id(subtree_hash), hsub);
	      L(F("changing setting to subtree, with %s at slot 0x%x of node %s, level %d\n") 
		% hsub % slotnum % hpref % level);
	      node.slots[slotnum] = subtree_hash;
	      node.set_slot_state(slotnum, subtree_state);      
	    }
	  break;

	case empty_state:
	  L(F("placing leaf %s in previously empty slot 0x%x of %s node %s, level %d\n")
	    % hleaf % slotnum % type % hpref % level);
	  node.total_num_leaves++;
	  node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
	  node.slots[slotnum] = leaf;
	  break;

	case subtree_state:
	  {
	    L(F("placing leaf %s in previously empty slot 0x%x of %s node %s, level %d\n")
	      % hleaf % slotnum % type % hpref % level);
	    string subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
	    hexenc<id> hsub;
	    encode_hexenc(id(subtree_hash), hsub);
	    L(F("updating subtree setting to %s at slot 0x%x of node %s, level %d\n") 
	      % hsub % slotnum % hpref % level);
	    node.slots[slotnum] = subtree_hash;
	    node.set_slot_state(slotnum, subtree_state);
	  }
	  break;
	}
    }
  else
    {
      L(F("creating new %s node with prefix %s, level %d, holding %s at slot 0x%x\n")
	% type % hpref % level % hleaf % slotnum);
      node.level = level;
      node.prefix = pref;
      node.total_num_leaves = 1;
      node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
      node.slots[slotnum] = leaf;
    }
  return store_merkle_node(app, type, collection, node);
}
