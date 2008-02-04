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

class key_store;
class database;
namespace Botan { class RSA_PrivateKey; }

// keys.{hh,cc} does all the "delicate" crypto (meaning: that which needs
// to read passphrases and manipulate raw, decrypted private keys). it
// could in theory be in transforms.cc too, but that file's already kinda
// big and this stuff "feels" different, imho.

void generate_key_pair(key_store & keys,             // to hook for phrase
                       rsa_keypair_id const & id,    // to prompting user for phrase
                       keypair & kp_out);

void generate_key_pair(keypair & kp_out,
                       utf8 const phrase);

void change_key_passphrase(key_store & keys,          // to hook for phrase
                           rsa_keypair_id const & id, // to prompting user for phrase
                           base64< rsa_priv_key > & encoded_key);

void migrate_private_key(key_store & keys,
                         rsa_keypair_id const & id,
                         base64< old_arc4_rsa_priv_key > const & old_priv,
                         keypair & kp);

void make_signature(key_store & keys,
                    database & db,
                    rsa_keypair_id const & id,
                    base64< rsa_priv_key > const & priv,
                    std::string const & tosign,
                    base64<rsa_sha1_signature> & signature);

bool check_signature(key_store & keys,
                     rsa_keypair_id const & id,
                     base64<rsa_pub_key> const & pub,
                     std::string const & alleged_text,
                     base64<rsa_sha1_signature> const & signature);

void require_password(rsa_keypair_id const & id,
                      key_store & keys, database & db);

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

void
get_passphrase(key_store & keys,
               rsa_keypair_id const & keyid,
               utf8 & phrase,
               bool confirm_phrase = false,
               bool force_from_user = false,
               bool generating_key = false);

boost::shared_ptr<Botan::RSA_PrivateKey>
get_private_key(key_store & keys,
                rsa_keypair_id const & id,
                base64< rsa_priv_key > const & priv,
                bool force_from_user = false);

// netsync stuff

void read_pubkey(std::string const & in,
                 rsa_keypair_id & id,
                 base64<rsa_pub_key> & pub);

void write_pubkey(rsa_keypair_id const & id,
                  base64<rsa_pub_key> const & pub,
                  std::string & out);

void key_hash_code(rsa_keypair_id const & ident,
                   base64<rsa_pub_key> const & pub,
                   id & out);

void key_hash_code(rsa_keypair_id const & ident,
                   base64< rsa_priv_key > const & priv,
                   id & out);

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
