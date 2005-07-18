
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains magic constants which you could, in theory, tweak.
// probably best not to tweak them though.

#include "constants.hh"
#include "numeric_vocab.hh"

#include <boost/static_assert.hpp>

namespace constants
{

  // block size in bytes for "automate stdio" output
  size_t const automate_stdio_size = 1024;

  // number of bits in an RSA key we use
  size_t const keylen = 1024; 

  // number of characters in a SHA1 id
  size_t const idlen = 40;

  // number of characters in an epoch_data
  size_t const epochlen = idlen;

  // number of characters in an epoch_data
  size_t const epochlen_bytes = epochlen / 2;

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  time_t const cvs_window = 60 * 5; 

  // number of bytes in a password buffer. further bytes will be dropped.
  size_t const maxpasswd = 0xfff;

  // number of bytes to use in buffers, for buffered i/o operations
  size_t const bufsz = 0x3ffff;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  size_t const db_log_line_sz = 70;

  // size in bytes of the database xdelta version reconstruction cache.
  // the value of 7 MB was determined as the optimal point after timing
  // various values with a pull of the monotone repository - it could
  // be tweaked further.
  size_t const db_version_cache_sz = 7 * (1 << 20);

  // size of a line of text in the log buffer, beyond which log lines will be
  // truncated.
  size_t const log_line_sz = 0xff;

  // all the ASCII characters (bytes) which are legal in a packet.
  char const * const legal_packet_bytes = 
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"   
  "-"
  // extra base64 codes
  "+/="
  // separators
  ".@[]"
  // whitespace
  " \r\n\t" 
  ;

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  char const * const legal_id_bytes =
  "0123456789abcdef"
  ;

  // all the ASCII characters (bytes) which are legal in an ACE string
  char const * const legal_ace_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "-"
  // label separators
  ".@"
  ;
    
  // all the ASCII characters (bytes) which can occur in cert names
  char const * const legal_cert_name_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"   
  "-"
  ;
  
  // all the ASCII characters (bytes) which can occur in key names
  char const * const legal_key_name_bytes =
  // LDH characters
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"   
  "-"
  // other non-shell, non-selector metacharacters allowed in (unquoted) local
  // parts by RFC2821/RFC2822.  The full list is !#$%&'*+-/=?^_`|{}~.
  "+_."
  // label and component separators
  ".@"
  ;
  
  // all the ASCII characters (bytes) which are illegal in a (file|local)_path

  char const illegal_path_bytes_arr[33] = 
    { 
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 
      0x7f, 0x00
    }
  ;

  char const * const illegal_path_bytes =
  illegal_path_bytes_arr
  ;

  // how many revisions back to verify the sanity of revisions before allowing
  // them into the database
  int const verify_depth = 4;

  // merkle tree / netcmd / netsync related stuff

  size_t const merkle_hash_length_in_bytes = 20;
  size_t const merkle_fanout_bits = 4;

  // all other merkle constants are derived
  size_t const merkle_hash_length_in_bits = merkle_hash_length_in_bytes * 8;
  size_t const merkle_num_tree_levels = merkle_hash_length_in_bits / merkle_fanout_bits;
  size_t const merkle_num_slots = 1 << merkle_fanout_bits;
  size_t const merkle_bitmap_length_in_bits = merkle_num_slots * 2;
  size_t const merkle_bitmap_length_in_bytes = merkle_bitmap_length_in_bits / 8;

  BOOST_STATIC_ASSERT(sizeof(char) == 1);
  BOOST_STATIC_ASSERT(CHAR_BIT == 8);
  BOOST_STATIC_ASSERT(merkle_num_tree_levels > 0);
  BOOST_STATIC_ASSERT(merkle_num_tree_levels < 256);
  BOOST_STATIC_ASSERT(merkle_fanout_bits > 0);
  BOOST_STATIC_ASSERT(merkle_fanout_bits < 32);
  BOOST_STATIC_ASSERT(merkle_hash_length_in_bits > 0);
  BOOST_STATIC_ASSERT((merkle_hash_length_in_bits % merkle_fanout_bits) == 0);
  BOOST_STATIC_ASSERT(merkle_bitmap_length_in_bits > 0);
  BOOST_STATIC_ASSERT((merkle_bitmap_length_in_bits % 8) == 0);

  u8 const netcmd_current_protocol_version = 5;
  size_t const netcmd_minsz = (1     // version
                               + 1   // cmd code
                               + 1   // smallest uleb possible
                               + 4); // adler32    
  
  // allow payloads up to 256 megs (this sets the in-practice maximum size of
  // a compressed file/manifest).
  size_t const netcmd_payload_limit = 2 << 27;
  size_t const netcmd_maxsz = netcmd_minsz + netcmd_payload_limit;
  size_t const netcmd_minimum_bytes_to_bother_with_gzip = 0xfff;

  size_t const netsync_default_port = 5253;
  size_t const netsync_connection_limit = 1024; 
  size_t const netsync_timeout_seconds = 21600; // 6 hours
  size_t const netsync_session_key_length_in_bytes = 20;     // 160 bits
  size_t const netsync_hmac_value_length_in_bytes = 20;      // 160 bits

  std::string const & netsync_key_initializer = std::string(netsync_session_key_length_in_bytes, 0);

}
