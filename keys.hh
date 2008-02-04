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
#include <boost/shared_ptr.hpp>

struct options;
class lua_hooks;
class key_store;
class database;
namespace Botan { class RSA_PrivateKey; }

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
void get_user_key(rsa_keypair_id & key, options const & opts, lua_hooks & lua,
                  key_store & keys, database & db);

void cache_user_key(options const & opts, lua_hooks & lua,
                    key_store & keys, database & db);

void load_key_pair(key_store & keys,
                   rsa_keypair_id const & id);

void load_key_pair(key_store & keys,
                   rsa_keypair_id const & id,
                   keypair & kp);

void encrypt_rsa(key_store & keys,
                 rsa_keypair_id const & id,
                 base64<rsa_pub_key> & pub,
                 std::string const & plaintext,
                 rsa_oaep_sha_data & ciphertext);

void decrypt_rsa(key_store & keys,
                 rsa_keypair_id const & id,
                 base64< rsa_priv_key > const & priv,
                 rsa_oaep_sha_data const & ciphertext,
                 std::string & plaintext);

boost::shared_ptr<Botan::RSA_PrivateKey>
get_private_key(key_store & keys,
                rsa_keypair_id const & id,
                base64< rsa_priv_key > const & priv,
                bool force_from_user = false);

// netsync stuff

void key_hash_code(rsa_keypair_id const & ident,
                   base64<rsa_pub_key> const & pub,
                   hexenc<id> & out);

void key_hash_code(rsa_keypair_id const & ident,
                   base64< rsa_priv_key > const & priv,
                   hexenc<id> & out);

bool keys_match(rsa_keypair_id const & id1,
                base64<rsa_pub_key> const & key1,
                rsa_keypair_id const & id2,
                base64<rsa_pub_key> const & key2);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __KEYS_HH__
