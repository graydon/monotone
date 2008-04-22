#ifndef __KEYS_HH__
#define __KEYS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"

struct options;
class lua_hooks;
class key_store;
class database;
struct keypair;

// keys.{hh,cc} does all the "delicate" crypto (meaning: that which needs
// to read passphrases and manipulate raw, decrypted private keys). it
// could in theory be in transforms.cc too, but that file's already kinda
// big and this stuff "feels" different, imho.

void
get_passphrase(utf8 & phrase,
               rsa_keypair_id const & keyid,
               bool confirm_phrase,
               bool generating_key);

// N()'s out if there is no unique key for us to use
void get_user_key(options const & opts, lua_hooks & lua,
                  database & db, key_store & keys, 
                  rsa_keypair_id & key);

void cache_user_key(options const & opts, lua_hooks & lua,
                    database & db, key_store & keys);

void load_key_pair(key_store & keys,
                   rsa_keypair_id const & id);

void load_key_pair(key_store & keys,
                   rsa_keypair_id const & id,
                   keypair & kp);

// netsync stuff

void key_hash_code(rsa_keypair_id const & ident,
                   rsa_pub_key const & pub,
                   id & out);

void key_hash_code(rsa_keypair_id const & ident,
                   rsa_priv_key const & priv,
                   id & out);

bool keys_match(rsa_keypair_id const & id1,
                rsa_pub_key const & key1,
                rsa_keypair_id const & id2,
                rsa_pub_key const & key2);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __KEYS_HH__
