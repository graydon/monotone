// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file contains magic constants which you could, in theory, tweak.
// probably best not to tweak them though.
//
// style notes: (1) scalar constants should be defined in constants.hh so
// their values are visible to the compiler; (2) do not use std::string or
// any other non-POD type for aggregate constants defined in this file, as
// this tends to cause unnecessary copying and unconditionally-executed
// initialization code for constants that don't get used always; (3) use
// "char const foo[]" instead of "char const * const foo" -- it's less
// typing and it saves an indirection.

#include "base.hh"
#include "constants.hh"

namespace constants
{
  // all the ASCII characters (bytes) which are legal in a sequence of
  // base64-encoded data.  note that botan doesn't count \v or \f as
  // whitespace (unlike <ctype.h>) and so neither do we.
  char const legal_base64_bytes[] =
  // base64 data characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789+/="
  // whitespace
  " \r\n\t"
  ;

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  char const legal_id_bytes[] =
  "0123456789abcdef"
  ;

  // all the ASCII characters (bytes) which are legal in an ACE string
  char const legal_ace_bytes[] =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // label separators
  ".@"
  ;

  // all the ASCII characters (bytes) which can occur in cert names
  char const legal_cert_name_bytes[] =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  ;

  // all the ASCII characters (bytes) which can occur in key names
  char const legal_key_name_bytes[] =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // label and component separators
  ".@"
  // other non-shell, non-selector metacharacters allowed in (unquoted) local
  // parts by RFC2821/RFC2822.  The full list is !#$%&'*+-/=?^_`|{}~.
  "+_"
  ;

  // merkle tree / netcmd / netsync related stuff
  char const netsync_key_initializer[netsync_session_key_length_in_bytes]
  = { 0 };

  // attributes
  char const encoding_attribute[] = "mtn:encoding";
  char const manual_merge_attribute[] = "mtn:manual_merge";
  char const binary_encoding[] = "binary";
  char const default_encoding[] = "default";

  // consistency checks - inside the namespace so we don't have to sprinkle
  // constants:: all over them.
}

#include <boost/static_assert.hpp>

// constraint checks for fundamental types
// n.b. sizeof([unsigned] char) is *defined* to be 1 by the C++ standard.
BOOST_STATIC_ASSERT(std::numeric_limits<unsigned char>::digits == 8);
BOOST_STATIC_ASSERT(sizeof(u8) == 1);
BOOST_STATIC_ASSERT(sizeof(u16) == 2);
BOOST_STATIC_ASSERT(sizeof(u32) == 4);
BOOST_STATIC_ASSERT(sizeof(u64) == 8);

// constraint checks for relations between constants above
using namespace constants;
BOOST_STATIC_ASSERT(merkle_num_tree_levels > 0);
BOOST_STATIC_ASSERT(merkle_num_tree_levels < 256);
BOOST_STATIC_ASSERT(merkle_fanout_bits > 0);
BOOST_STATIC_ASSERT(merkle_fanout_bits < 32);
BOOST_STATIC_ASSERT(merkle_hash_length_in_bits > 0);
BOOST_STATIC_ASSERT((merkle_hash_length_in_bits % merkle_fanout_bits) == 0);
BOOST_STATIC_ASSERT(merkle_bitmap_length_in_bits > 0);
BOOST_STATIC_ASSERT((merkle_bitmap_length_in_bits % 8) == 0);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
