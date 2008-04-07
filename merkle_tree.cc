// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <sstream>

#include <boost/dynamic_bitset.hpp>

#include "botan/botan.h"
#include "botan/sha160.h"

#include "constants.hh"
#include "merkle_tree.hh"
#include "netio.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "transforms.hh"

using std::make_pair;
using std::ostream_iterator;
using std::ostringstream;
using std::set;
using std::string;

using boost::dynamic_bitset;

static void
bitset_to_prefix(dynamic_bitset<unsigned char> const & pref,
                 prefix & rawpref)
{
  string s(pref.num_blocks(), 0x00);
  to_block_range(pref, s.begin());
  rawpref = prefix(s);
}

void
netcmd_item_type_to_string(netcmd_item_type t, string & typestr)
{
  typestr.clear();
  switch (t)
    {
    case revision_item:
      typestr = "revision";
      break;
    case file_item:
      typestr = "file";
      break;
    case cert_item:
      typestr = "cert";
      break;
    case key_item:
      typestr = "key";
      break;
    case epoch_item:
      typestr = "epoch";
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
  Botan::SHA_160 hash;
  hash.update(reinterpret_cast<Botan::byte const *>(in.data()),
	      static_cast<unsigned int>(in.size()));
  char digest[constants::sha1_digest_length];
  hash.final(reinterpret_cast<Botan::byte *>(digest));
  string out(digest, constants::sha1_digest_length);
  return out;
}


merkle_node::merkle_node() : level(0), pref(0),
                             total_num_leaves(0),
                             bitmap(constants::merkle_bitmap_length_in_bits),
                             slots(constants::merkle_num_slots),
                             type(revision_item)
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
  bitset_to_prefix(this->pref, pref);
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
  check_invariants();
  i = idx(this->slots, slot);
}

void
merkle_node::set_raw_slot(size_t slot, id const & val)
{
  check_invariants();
  idx(this->slots, slot) = val;
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
  bitset_to_prefix(ext, extended);
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
        return leaf_state;
    }
  else
    {
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
  if (st == subtree_state || st == leaf_state)
    bitmap.set(2*n);
  if (st == subtree_state)
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
  outbuf.append(hash);
  outbuf.append(oss.str());
}

void
read_node(string const & inbuf, size_t & pos, merkle_node & out)
{
  string hash = extract_substring(inbuf, pos,
                                  constants::merkle_hash_length_in_bytes,
                                  "node hash");
  size_t begin_pos = pos;
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
          out.set_raw_slot(slot, id(slot_val));
        }
    }

  string checkhash = raw_sha1(inbuf.substr(begin_pos, pos - begin_pos));
  out.check_invariants();
  if (hash != checkhash)
    throw bad_decode(F("mismatched node hash value %s, expected %s")
		     % xform<Botan::Hex_Encoder>(checkhash) % xform<Botan::Hex_Encoder>(hash));
}


// returns the first hashsz bytes of the serialized node, which is
// the hash of its contents.

static id
hash_merkle_node(merkle_node const & node)
{
  string out;
  write_node(node, out);
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
recalculate_merkle_codes(merkle_table & tab,
                         prefix const & pref,
                         size_t level)
{
  merkle_table::const_iterator i = tab.find(make_pair(pref, level));
  I(i != tab.end());
  merkle_ptr node = i->second;

  for (size_t slotnum = 0; slotnum < constants::merkle_num_slots; ++slotnum)
    {
      slot_state st = node->get_slot_state(slotnum);
      if (st == subtree_state)
        {
          id slotval;
          node->get_raw_slot(slotnum, slotval);
          if (slotval().empty())
            {
              prefix extended;
              node->extended_raw_prefix(slotnum, extended);
              slotval = recalculate_merkle_codes(tab, extended, level+1);
              node->set_raw_slot(slotnum, slotval);
            }
        }
    }

  return hash_merkle_node(*node);
}

void
collect_items_in_subtree(merkle_table & tab,
                         prefix const & pref,
                         size_t level,
                         set<id> & items)
{
  merkle_table::const_iterator i = tab.find(make_pair(pref, level));
  merkle_ptr node;
  prefix ext;
  id item;
  if (i != tab.end())
    {
      node = i->second;
      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {
          switch (node->get_slot_state(slot))
            {
            case empty_state:
              break;

            case leaf_state:
              node->get_raw_slot(slot, item);
              items.insert(item);
              break;

            case subtree_state:
              node->extended_raw_prefix(slot, ext);
              collect_items_in_subtree(tab, ext, level+1, items);
              break;
            }
        }
    }
}

bool
locate_item(merkle_table & table,
            id const & val,
            size_t & slotnum,
            merkle_ptr & mp)
{
  mp.reset();
  boost::dynamic_bitset<unsigned char> pref;
  for (size_t l = 0; l < constants::merkle_num_tree_levels; ++l)
    {
      pick_slot_and_prefix_for_value(val, l, slotnum, pref);

      prefix rawpref;
      bitset_to_prefix(pref, rawpref);

      merkle_table::const_iterator i = table.find(make_pair(rawpref, l));
      if (i == table.end() ||
          i->second->get_slot_state(slotnum) == empty_state)
        return false;

      if (i->second->get_slot_state(slotnum) == leaf_state)
        {
          id slotval;
          i->second->get_raw_slot(slotnum, slotval);
          if (slotval == val)
            {
              mp = i->second;
              return true;
            }
          else
            return false;
        }
    }
  return false;
}

void
insert_into_merkle_tree(merkle_table & tab,
                        netcmd_item_type type,
                        id const & leaf,
                        size_t level)
{
  I(constants::merkle_hash_length_in_bytes == leaf().size());
  I(constants::merkle_fanout_bits * (level + 1)
    <= constants::merkle_hash_length_in_bits);

  size_t slotnum;
  dynamic_bitset<unsigned char> pref;
  pick_slot_and_prefix_for_value(leaf, level, slotnum, pref);

  prefix rawpref;
  bitset_to_prefix(pref, rawpref);

  merkle_table::const_iterator i = tab.find(make_pair(rawpref, level));
  merkle_ptr node;

  if (i != tab.end())
    {
      node = i->second;
      slot_state st = node->get_slot_state(slotnum);
      switch (st)
        {
        case leaf_state:
          {
            id slotval;
            node->get_raw_slot(slotnum, slotval);
            if (slotval == leaf)
              {
                // Do nothing, it's already present
              }
            else
              {
                insert_into_merkle_tree(tab, type, slotval, level+1);
                insert_into_merkle_tree(tab, type, leaf, level+1);
                id empty_subtree_hash;
                node->set_raw_slot(slotnum, empty_subtree_hash);
                node->set_slot_state(slotnum, subtree_state);
              }
          }
          break;

        case empty_state:
          node->total_num_leaves++;
          node->set_slot_state(slotnum, leaf_state);
          node->set_raw_slot(slotnum, leaf);
          break;

        case subtree_state:
          {
            insert_into_merkle_tree(tab, type, leaf, level+1);
            id empty_subtree_hash;
            node->set_raw_slot(slotnum, empty_subtree_hash);
            node->set_slot_state(slotnum, subtree_state);
          }
          break;
        }
    }
  else
    {
      node = merkle_ptr(new merkle_node());
      node->type = type;
      node->level = level;
      node->pref = pref;
      node->total_num_leaves = 1;
      node->set_slot_state(slotnum, leaf_state);
      node->set_raw_slot(slotnum, leaf);
      tab.insert(make_pair(make_pair(rawpref, level), node));
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
