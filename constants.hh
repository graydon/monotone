#ifndef __CONSTANTS_HH__
#define __CONSTANTS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <unistd.h>

namespace constants
{

  // this file contains magic constants which you could, in theory, tweak.
  // probably best not to tweak them though.

  // number of bits in an RSA key we use
  extern size_t const keylen; 

  // number of bytes of random noise we seed vcheck packets with
  extern size_t const vchecklen;

  // number of characters in a SHA1 id
  extern size_t const idlen; 

  // number of seconds in window, in which to consider CVS commits equivalent
  // if they have otherwise compatible contents (author, changelog)
  extern size_t const cvs_window; 

  // number of bytes accepted in a database row (also used as a file upload
  // limit in the depot code).
  extern size_t const maxbytes;

  // advisory number of bytes sent in a single network transmission; not a
  // strict limit (single packets beyond this size will post as a unit) but a
  // "suggested maximum size" for each posting.
  extern size_t const postsz;

  // number of bytes to use in buffers, for buffered i/o operations
  extern size_t const bufsz;

  // size of a line of database traffic logging, beyond which lines will be
  // truncated.
  extern size_t const db_log_line_sz;

  // size of a line of text in the log buffer, beyond which log lines will be
  // truncated.
  extern size_t const log_line_sz;

  // all the ASCII characters (bytes) which are legal in a packet
  extern char const * const legal_packet_bytes;

  // all the ASCII characters (bytes) which are legal in a SHA1 hex id
  extern char const * const legal_id_bytes;

  // all the ASCII characters (bytes) which can occur in URLs
  extern char const * const legal_url_bytes;

  // all the ASCII characters (bytes) which can occur in cert names
  extern char const * const legal_cert_name_bytes;

  // all the ASCII characters (bytes) which can occur in key names
  extern char const * const legal_key_name_bytes;

  // all the ASCII characters (bytes) which are illegal in a (file|local)_path
  extern char const * const illegal_path_bytes;

  
}

#endif // __CONSTANTS_HH__
