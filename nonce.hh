#ifndef __NONCE_HH__
#define __NONCE_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include "manifest.hh"
#include "vocab.hh"

extern std::string const nonce_file_name;

struct app_state;
struct work_set;

void
make_nonce(app_state & app, 
	   std::string & nonce);

void
merge_nonces(file_data const & left,
	     file_data const & right,
	     file_data & merged);

void
merge_nonces(file_data const & ancestor,
	     file_data const & left,
	     file_data const & right,
	     file_data & merged);

#endif // __NONCE_HH__
