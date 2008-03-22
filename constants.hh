#ifndef __CONSTANTS_HH__
#define __CONSTANTS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstddef>
#include <ctime>
#include "numeric_vocab.hh"

namespace constants
{
  // this file contains magic constants which you could, in theory, tweak.
  // probably best not to tweak them though.
  // all scalar constants are defined in this file so their values are
  // visible to the compiler; aggregate constants are defined in
  // constants.cc, which also has some static assertions to make sure
  // everything is consistent.

  // number of bits in an RSA key we use
  std::size_t const keylen = 1024;

  // number of characters in a SHA1 id
  std::size_t const idlen = 40;

  // number of binary bytes, ditto
  std::size_t const idlen_bytes = idlen / 2;

  // number of characters in an encoded epoch
  std::size_t const epochlen = idlen;

  // number of characters in a raw epoch
  std::size_t const epochlen_bytes = idlen_bytes;

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  std::time_t const cvs_window = 60 * 5;

  // number of bytes in a password buffer. further bytes will be dropped.
  std::size_t const maxpasswd = 0xfff;

  // number of bytes to use in buffers, for buffered i/o operations
  std::size_t const bufsz = 0x3ffff;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  std::size_t const db_log_line_sz = 70;

  // maximum size in bytes of the database xdelta version reconstruction
  // cache.  the value of 7 MB was determined as the optimal point after
  // timing various values with a pull of the monotone repository - it could
  // be tweaked further.
  std::size_t const db_version_cache_sz = 7 * (1 << 20);

  // maximum size in bytes of the write-back roster cache
  // the value of 7 MB was determined by blindly copying the line above and
  // not doing any testing at all - it could be tweaked further.
  std::size_t const db_roster_cache_sz = 7 * (1 << 20);

  // estimated number of bytes taken for a node_t and its corresponding
  // marking_t.  used to estimate the current size of the write-back roster
  // cache.    the calculation is:
  //   -- 40 bytes content hash
  //   -- a path component, maybe 10 or 15 bytes
  //   -- 40 bytes birth revision
  //   -- 40 bytes name marking hash
  //   -- 40 bytes content marking hash
  //   -- plus internal pointers, etc., for strings, sets, shared_ptrs, heap
  //      overhead, ...
  //   -- plus any space taken for attrs
  // so ~175 bytes for a file node, plus internal slop, plus attrs (another
  // 60 bytes per attr, or so), minus 80 bytes for dir nodes.  So this just
  // picks a number that seems a reasonable amount over 175.
  std::size_t const db_estimated_roster_node_sz = 210;

  // maximum number of bytes to be consumed with the delayed write cache
  unsigned long const db_max_delayed_file_bytes = 16 * (1 << 20);

  // size of a line of text in the log buffer, beyond which log lines will be
  // truncated.
  std::size_t const log_line_sz = 0x300;

  // assumed width of the terminal, when we can't query for it directly
  std::size_t const default_terminal_width = 72;

  // all the ASCII characters (bytes) which are legal in a base64 blob
  extern char const legal_base64_bytes[];

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  extern char const legal_id_bytes[];

  // all the ASCII characters (bytes) which can occur in cert names
  extern char const legal_cert_name_bytes[];

  // all the ASCII characters (bytes) which can occur in key names
  extern char const legal_key_name_bytes[];

  // remaining constants are related to netsync protocol

  // number of bytes in the hash used in netsync
  std::size_t const merkle_hash_length_in_bytes = 20;

  // number of bits of merkle prefix consumed by each level of tree
  std::size_t const merkle_fanout_bits = 4;

  // derived from hash_length_in_bytes
  std::size_t const merkle_hash_length_in_bits
  = merkle_hash_length_in_bytes * 8;

  // derived from fanout_bits
  std::size_t const merkle_num_tree_levels
  = merkle_hash_length_in_bits / merkle_fanout_bits;

  // derived from fanout_bits
  std::size_t const merkle_num_slots = 1 << merkle_fanout_bits;

  // derived from fanout_bits
  std::size_t const merkle_bitmap_length_in_bits = merkle_num_slots * 2;

  // derived from fanout_bits
  std::size_t const merkle_bitmap_length_in_bytes
  = merkle_bitmap_length_in_bits / 8;

  // the current netcmd/netsync protocol version
  u8 const netcmd_current_protocol_version = 6;

  // minimum size of any netcmd on the wire
  std::size_t const netcmd_minsz = (1     // version
                                    + 1   // cmd code
                                    + 1); // smallest uleb possible


  // largest command *payload* allowed in a netcmd
  // in practice, this sets the size of the largest compressed file
  std::size_t const netcmd_payload_limit = 2 << 27;

  // maximum size of any netcmd on the wire, including payload
  std::size_t const netcmd_maxsz = netcmd_minsz + netcmd_payload_limit;

  // netsync fragments larger than this are gzipped
  std::size_t const netcmd_minimum_bytes_to_bother_with_gzip = 0xfff;

  // TCP port to listen on / connect to when doing netsync
  std::size_t const netsync_default_port = 4691;

  // maximum number of simultaneous clients on a server
  std::size_t const netsync_connection_limit = 1024;

  // number of seconds a connection can be idle before it's dropped
  std::size_t const netsync_timeout_seconds = 21600; // 6 hours

  // netsync HMAC key length
  std::size_t const netsync_session_key_length_in_bytes = 20;

  // netsync HMAC value length
  std::size_t const netsync_hmac_value_length_in_bytes = 20;

  // how long a sha1 digest should be
  std::size_t const sha1_digest_length = 20; // 160 bits

  // netsync session key default initializer
  extern char const netsync_key_initializer[];

  // attributes
  extern char const encoding_attribute[];
  extern char const binary_encoding[];
  extern char const default_encoding[];
  extern char const manual_merge_attribute[];
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CONSTANTS_HH__
