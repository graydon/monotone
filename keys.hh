#ifndef __KEYS_HH__
#define __KEYS_HH__

// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
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
                       base64< arc4<rsa_priv_key> > & priv,
                       // Used for unit tests only:
                       std::string const unit_test_passphrase = std::string());

void change_key_passphrase(lua_hooks & lua,       // to hook for phrase
                           rsa_keypair_id const & id, // to prompting user for phrase
                           base64< arc4<rsa_priv_key> > & encoded_key);

void make_signature(app_state & app,           // to hook for phrase
                    rsa_keypair_id const & id, // to prompting user for phrase
                    base64< arc4<rsa_priv_key> > const & priv,
                    std::string const & tosign,
                    base64<rsa_sha1_signature> & signature);

bool check_signature(app_state & app,
                     rsa_keypair_id const & id,
                     base64<rsa_pub_key> const & pub,
                     std::string const & alleged_text,
                     base64<rsa_sha1_signature> const & signature);

void require_password(rsa_keypair_id const & id,
                      app_state & app);

void encrypt_rsa(lua_hooks & lua,
                 rsa_keypair_id const & id,
                 base64<rsa_pub_key> & pub,
                 std::string const & plaintext,
                 rsa_oaep_sha_data & ciphertext);

void decrypt_rsa(lua_hooks & lua,
                 rsa_keypair_id const & id,
                 base64< arc4<rsa_priv_key> > const & priv,
                 rsa_oaep_sha_data const & ciphertext,
                 std::string & plaintext);

// netsync stuff

void read_pubkey(std::string const & in, 
                 rsa_keypair_id & id,
                 base64<rsa_pub_key> & pub);

void write_pubkey(rsa_keypair_id const & id,
                  base64<rsa_pub_key> const & pub,
                  std::string & out);

void key_hash_code(rsa_keypair_id const & id,
                   base64<rsa_pub_key> const & pub,
                   hexenc<id> & out);

void key_hash_code(rsa_keypair_id const & id,
                   base64< arc4<rsa_priv_key> > const & priv,
                   hexenc<id> & out);

bool keys_match(rsa_keypair_id const & id1,
                base64<rsa_pub_key> const & key1,
                rsa_keypair_id const & id2,
                base64<rsa_pub_key> const & key2);

bool keys_match(rsa_keypair_id const & id1,
                base64< arc4<rsa_priv_key> > const & key1,
                rsa_keypair_id const & id2,
                base64< arc4<rsa_priv_key> > const & key2);


#endif // __KEYS_HH__
