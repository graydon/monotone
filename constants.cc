
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains magic constants which you could, in theory, tweak.
// probably best not to tweak them though.

#include "constants.hh"

namespace constants
{

  // number of bits in an RSA key we use
  size_t const keylen = 1024; 

  // number of bytes of random noise we seed vcheck packets with
  size_t const vchecklen = 32;

  // number of characters in a SHA1 id
  size_t const idlen = 40;

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  size_t const cvs_window = 3600 * 3; 

  // number of bytes accepted in a database row (also used as a file upload
  // limit in the depot code).
  size_t const maxbytes = 0xffffff;

  // advisory number of bytes sent in a single network transmission; not a
  // strict limit (single packets beyond this size will post as a unit) but a
  // "suggested maximum size" for each posting.
  size_t const postsz = 0xffff;

  // number of bytes to use in buffers, for buffered i/o operations
  size_t const bufsz = 0xfff;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  size_t const db_log_line_sz = 70;

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
  
  // all the ASCII characters (bytes) which can occur in URLs
  char const * const legal_url_bytes =
  // alphanumerics
  "abcdefghijklmnopqrstuvwxyz"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  // mark chars
  "-_.!~*'()"
  // extra path chars
  ":@&=+$,"
  // path separator
  "/"
  // escape char
  "%"
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
  // label and component separators
  ".@"
  ;
  
  // all the ASCII characters (bytes) which are illegal in a (file|local)_path

  char const illegal_path_bytes_arr[32] = 
    { 
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 
      0x7f
    }
  ;

  char const * const illegal_path_bytes =
  illegal_path_bytes_arr
  ;

}
