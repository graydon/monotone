#ifndef __CONSTANTS_HH__
#define __CONSTANTS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains magic constants which you could, in theory, tweak.
// probably best not to tweak them though.

// number of bits in an RSA key we use
size_t const keylen = 1024; 

// number of seconds in window, in which to consider CVS commits equivalent
// if they have otherwise compatible contents (author, changelog)
size_t const cvs_window = 3600 * 3; 

// number of bytes accepted in a database row (also used as a file upload
// limit in the depot code).
size_t const maxbytes = 0xffffff;

// number of bytes to use in buffers, for buffered i/o operations
size_t const bufsz = 0xffff;

// size of a line of database traffic logging, beyond which lines will be
// truncated.
size_t const db_log_line_sz = 70;

// size of a line of text in the log buffer, beyond which log lines will be
// truncated.
size_t const log_line_sz = 0xff;

#endif // __CONSTANTS_HH__
