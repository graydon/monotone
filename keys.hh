#ifndef __KEYS_HH__
#define __KEYS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include <string>

// keys.{hh,cc} does all the "delicate" crypto (meaning: that which needs
// to read passphrases and manipulate raw, decrypted private keys). it 
// could in theory be in transforms.cc too, but that file's already kinda
// big and this stuff "feels" different, imho.

struct lua_hooks;

void generate_key_pair(lua_hooks & lua,           // to hook for phrase
		       rsa_keypair_id const & id, // to prompting user for phrase
		       base64<rsa_pub_key> & pub,
		       base64< arc4<rsa_priv_key> > & priv);

void make_signature(lua_hooks & lua,           // to hook for phrase
		    rsa_keypair_id const & id, // to prompting user for phrase
		    base64< arc4<rsa_priv_key> > const & priv,
		    std::string const & tosign,
		    base64<rsa_sha1_signature> & signature);

bool check_signature(lua_hooks & lua,
		     rsa_keypair_id const & id,
		     base64<rsa_pub_key> const & pub,
		     std::string const & alleged_text,
		     base64<rsa_sha1_signature> const & signature);

#endif // __KEYS_HH__
