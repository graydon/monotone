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

void 
netcmd_item_type_to_string(netcmd_item_type t, string & typestr)
{
  typestr.clear();
  switch (t)
    {
    case revision_item:
      typestr = "revision";
      break;
    case manifest_item:
      typestr = "manifest";
      break;
    case file_item:
      typestr = "file";
      break;
    case rcert_item:
      typestr = "rcert";
      break;
    case mcert_item:
      typestr = "mcert";
      break;
    case fcert_item:
      typestr = "fcert";
      break;
    case key_item:
      typestr = "key";
      break;
    }
  I(!typestr.empty());
}

// this is a *raw* SHA1, not the nice friendly hex-encoded type. it is half
// as many bytes. since merkle nodes are mostly nothing but SHA1 values,
// and we have to send them over the wire, we use a raw variant here
// for compactness.

string 
raw_sha1(string const & in)
{
  SHA hash;
  hash.Update(reinterpret_cast<byte const *>(in.data()), 
              static_cast<unsigned int>(in.size()));
  char digest[SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  string out(digest, SHA::DIGESTSIZE);
  return out;
}


merkle_node::merkle_node() : level(0), pref(0), 
                             total_num_leaves(0), 
                             bitmap(constants::merkle_bitmap_length_in_bits),
                             slots(constants::merkle_num_slots),
                             type(manifest_item) 
{}

bool 
merkle_node::operator==(merkle_node const & other) const
{
  return (level == other.level
          && pref == other.pref
          && total_num_leaves == other.total_num_leaves
          && bitmap == other.bitmap
          && slots == other.slots
          && type == other.type);
}

void 
merkle_node::check_invariants() const
{
  I(this->pref.size() == prefix_length_in_bits(this->level));
  I(this->level <= constants::merkle_num_tree_levels);
  I(this->slots.size() == constants::merkle_num_slots);
  I(this->bitmap.size() == constants::merkle_bitmap_length_in_bits);
}

void 
merkle_node::get_raw_prefix(prefix & pref) const
{
  check_invariants();
  ostringstream oss;
  to_block_range(this->pref, ostream_iterator<char>(oss));
  pref = prefix(oss.str());
}

void 
merkle_node::get_hex_prefix(hexenc<prefix> & hpref) const
{
  prefix pref;
  get_raw_prefix(pref);
  encode_hexenc(pref, hpref);
}

void 
merkle_node::get_raw_slot(size_t slot, id & i) const
{
  I(get_slot_state(slot) != empty_state);
  I(idx(this->slots, slot)() != "");
  check_invariants();
  i = idx(this->slots, slot);
}

void 
merkle_node::get_hex_slot(size_t slot, hexenc<id> & val) const
{
  id i;
  get_raw_slot(slot, i);
  encode_hexenc(i, val);
}

void 
merkle_node::set_raw_slot(size_t slot, id const & val)
{
  check_invariants();
  idx(this->slots, slot) = val;
}

void 
merkle_node::set_hex_slot(size_t slot, hexenc<id> const & val)
{
  id i;
  decode_hexenc(val, i);
  set_raw_slot(slot, i);
}

void 
merkle_node::extended_prefix(size_t slot, 
                             dynamic_bitset<unsigned char> & extended) const
{
  // remember, in a dynamic_bitset, bit size()-1 is most significant
  check_invariants();
  I(slot < constants::merkle_num_slots);
  extended = this->pref;
  for (size_t i = 0; i < constants::merkle_fanout_bits; ++i)
    extended.push_back(static_cast<bool>((slot >> i) & 1));
}

void 
merkle_node::extended_raw_prefix(size_t slot, 
                                 prefix & extended) const
{
  dynamic_bitset<unsigned char> ext;
  extended_prefix(slot, ext);
  ostringstream oss;
  to_block_range(ext, ostream_iterator<char>(oss));
  extended = prefix(oss.str());
}

void 
merkle_node::extended_hex_prefix(size_t slot, 
                                 hexenc<prefix> & extended) const
{
  prefix pref;
  extended_raw_prefix(slot, pref);
  encode_hexenc(pref, extended);
}

slot_state 
merkle_node::get_slot_state(size_t n) const
{
  check_invariants();
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

void 
merkle_node::set_slot_state(size_t n, slot_state st)
{
  check_invariants();
  I(n < constants::merkle_num_slots);
  I(2*n + 1 < bitmap.size());
  bitmap.reset(2*n);
  bitmap.reset(2*n+1);
  if (st == subtree_state || st == live_leaf_state)
    bitmap.set(2*n);
  if (st == subtree_state || st == dead_leaf_state)
    bitmap.set(2*n+1);
}    


size_t 
prefix_length_in_bits(size_t level)
{
  return level * constants::merkle_fanout_bits;
}

size_t 
prefix_length_in_bytes(size_t level)
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

void 
write_node(merkle_node const & in, string & outbuf)
{      
  ostringstream oss;
  oss.put(static_cast<u8>(in.type));
  I(in.pref.size() == in.level * constants::merkle_fanout_bits);

  string tmp;
  insert_datum_uleb128<size_t>(in.level, tmp);
  oss.write(tmp.data(), tmp.size());
  tmp.clear();

  to_block_range(in.pref, ostream_iterator<char>(oss));

  insert_datum_uleb128<size_t>(in.total_num_leaves, tmp);
  oss.write(tmp.data(), tmp.size());
  tmp.clear();

  to_block_range(in.bitmap, ostream_iterator<char>(oss));

  for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
    {
      if (in.get_slot_state(slot) != empty_state)
        {
          I(slot < in.slots.size());
          id slot_val;
          in.get_raw_slot(slot, slot_val);
          oss.write(slot_val().data(), slot_val().size());
        }
    }
  string hash = raw_sha1(oss.str());
  I(hash.size() == constants::merkle_hash_length_in_bytes);
  outbuf = hash + oss.str();
}
    
void 
read_node(string const & inbuf, merkle_node & out)
{
  size_t pos = 0;
  string hash = extract_substring(inbuf, pos, 
                                  constants::merkle_hash_length_in_bytes, 
                                  "node hash");
  out.type = static_cast<netcmd_item_type>(extract_datum_lsb<u8>(inbuf, pos, "node type"));
  out.level = extract_datum_uleb128<size_t>(inbuf, pos, "node level");

  if (out.level >= constants::merkle_num_tree_levels)
    throw bad_decode(F("node level is %d, exceeds maximum %d") 
                     % widen<u32,u8>(out.level)
                     % widen<u32,u8>(constants::merkle_num_tree_levels));

  size_t prefixsz = prefix_length_in_bytes(out.level);
  require_bytes(inbuf, pos, prefixsz, "node prefix");   
  out.pref.resize(prefix_length_in_bits(out.level));
  from_block_range(inbuf.begin() + pos, 
                   inbuf.begin() + pos + prefixsz,
                   out.pref);
  pos += prefixsz;

  out.total_num_leaves = extract_datum_uleb128<size_t>(inbuf, pos, "number of leaves");

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
          out.set_raw_slot(slot, slot_val);
        }
    }
    
  assert_end_of_buffer(inbuf, pos, "node");
  string checkhash = raw_sha1(inbuf.substr(constants::merkle_hash_length_in_bytes));
  out.check_invariants();
  if (hash != checkhash)
    throw bad_decode(F("mismatched node hash value %s, expected %s") 
                     % xform<HexEncoder>(checkhash) % xform<HexEncoder>(hash));
}

void 
load_merkle_node(app_state & app,
                 netcmd_item_type type,
                 utf8 const & collection,                       
                 size_t level,
                 hexenc<prefix> const & hpref,
                 merkle_node & node)
{
  base64<merkle> emerk;
  string typestr;
  merkle merk;  

  netcmd_item_type_to_string(type, typestr);
  app.db.get_merkle_node(typestr, collection, level, hpref, emerk);

  decode_base64(emerk, merk);
  read_node(merk(), node);
}

// returns the first hashsz bytes of the serialized node, which is 
// the hash of its contents.

id 
store_merkle_node(app_state & app,
                  utf8 const & collection,
                  merkle_node const & node)
{
  string out;
  string typestr;
  hexenc<prefix> hpref;
  base64<merkle> emerk;

  write_node(node, out);
  node.get_hex_prefix(hpref);
  encode_base64(merkle(out), emerk);
  netcmd_item_type_to_string(node.type, typestr);

  app.db.put_merkle_node(typestr, collection, node.level, hpref, emerk);

  I(out.size() >= constants::merkle_hash_length_in_bytes);
  return id(out.substr(0, constants::merkle_hash_length_in_bytes));
}

void 
pick_slot_and_prefix_for_value(id const & val, 
                               size_t level, 
                               size_t & slotnum, 
                               dynamic_bitset<unsigned char> & pref)
{
  pref.resize(val().size() * 8);
  from_block_range(val().begin(), val().end(), pref);

  // remember, in a dynamic_bitset, bit size()-1 is most significant

  slotnum = 0;
  for (size_t i = constants::merkle_fanout_bits; i > 0; --i)
    {
      slotnum <<= 1;
      if (pref[level * constants::merkle_fanout_bits + (i-1)])
        slotnum |= static_cast<size_t>(1);
      else
        slotnum &= static_cast<size_t>(~1);
    }
  pref.resize(level * constants::merkle_fanout_bits);
}

id 
insert_into_merkle_tree(app_state & app,
                        bool live_p,
                        netcmd_item_type type,
                        utf8 const & collection,
                        id const & leaf,
                        size_t level)
{
  I(constants::merkle_hash_length_in_bytes == leaf().size());
  I(constants::merkle_fanout_bits * (level + 1) 
    <= constants::merkle_hash_length_in_bits);
  
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  hexenc<id> hleaf;
  encode_hexenc(leaf, hleaf);

  size_t slotnum;
  dynamic_bitset<unsigned char> pref;
  pick_slot_and_prefix_for_value(leaf, level, slotnum, pref);

  ostringstream oss;
  to_block_range(pref, ostream_iterator<char>(oss));
  hexenc<prefix> hpref;
  encode_hexenc(prefix(oss.str()), hpref);

  if (level == 0)
    L(F("-- beginning top level insert --\n"));
        
  L(F("inserting %s leaf %s into slot 0x%x at %s node with prefix %s, level %d\n") 
    % (live_p ? "live" : "dead") % hleaf % slotnum % typestr % hpref % level);
  
  merkle_node node;
  if (app.db.merkle_node_exists(typestr, collection, level, hpref))
    {
      load_merkle_node(app, type, collection, level, hpref, node);
      slot_state st = node.get_slot_state(slotnum);
      switch (st)
        {
        case live_leaf_state:
        case dead_leaf_state:
          {
            id slotval;
            node.get_raw_slot(slotnum, slotval);
            if (slotval == leaf)
              {
                L(F("found existing entry for %s at slot 0x%x of %s node %s, level %d\n") 
                  % hleaf % slotnum % typestr % hpref % level);
                if (st == dead_leaf_state && live_p)
                  {
                    L(F("changing setting from dead to live, for %s at slot 0x%x of %s node %s, level %d\n") 
                      % hleaf % slotnum % typestr % hpref % level);
                    node.set_slot_state(slotnum, live_leaf_state);
                  }
                else if (st == live_leaf_state && !live_p)
                  {
                    L(F("changing setting from live to dead, for %s at slot 0x%x of %s node %s, level %d\n") 
                      % hleaf % slotnum % typestr % hpref % level);
                    node.set_slot_state(slotnum, dead_leaf_state);
                  }
              }
            else
              {
                hexenc<id> existing_hleaf;
                encode_hexenc(slotval, existing_hleaf);
                L(F("pushing existing leaf %s in slot 0x%x of %s node %s, level %d into subtree\n")
                  % existing_hleaf % slotnum % typestr % hpref % level);
                insert_into_merkle_tree(app, (st == live_leaf_state ? true : false),
                                        type, collection, slotval, level+1);
                id subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
                hexenc<id> hsub;
                encode_hexenc(subtree_hash, hsub);
                L(F("changing setting to subtree, with %s at slot 0x%x of node %s, level %d\n") 
                  % hsub % slotnum % hpref % level);
                node.set_raw_slot(slotnum, subtree_hash);
                node.set_slot_state(slotnum, subtree_state);      
              }
          }
          break;

        case empty_state:
          L(F("placing leaf %s in previously empty slot 0x%x of %s node %s, level %d\n")
            % hleaf % slotnum % typestr % hpref % level);
          node.total_num_leaves++;
          node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
          node.set_raw_slot(slotnum, leaf);
          break;

        case subtree_state:
          {
            L(F("taking %s to subtree in slot 0x%x of %s node %s, level %d\n")
              % hleaf % slotnum % typestr % hpref % level);
            id subtree_hash = insert_into_merkle_tree(app, live_p, type, collection, leaf, level+1);
            hexenc<id> hsub;
            encode_hexenc(id(subtree_hash), hsub);
            L(F("updating subtree setting to %s at slot 0x%x of node %s, level %d\n") 
              % hsub % slotnum % hpref % level);
            node.set_raw_slot(slotnum, subtree_hash);
            node.set_slot_state(slotnum, subtree_state);
          }
          break;
        }
    }
  else
    {
      L(F("creating new %s node with prefix %s, level %d, holding %s at slot 0x%x\n")
        % typestr % hpref % level % hleaf % slotnum);
      node.type = type;
      node.level = level;
      node.pref = pref;
      node.total_num_leaves = 1;
      node.set_slot_state(slotnum, (live_p ? live_leaf_state : dead_leaf_state));
      node.set_raw_slot(slotnum, leaf);
    }

  if (level == 0)
      L(F("-- finished top level insert --\n"));

  return store_merkle_node(app, collection, node);
}
