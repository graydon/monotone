#ifndef __UNIX_HH__
#define __UNIX_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// some utilities useful when dealing with the posix api

#include "vocab.hh"

// get a string version of latest posix error
// always use this instead of strerror; this function handles the charset
// correctly.
utf8 last_error();

#endif
