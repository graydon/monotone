// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <iostream>
#include <unistd.h>
#include <string.h>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "botan/botan.h"
#include "botan/rsa.h"
#include "botan/keypair.h"
#include "botan/pem.h"

#include "constants.hh"
#include "keys.hh"
#include "key_store.hh"
#include "lua_hooks.hh"
#include "netio.hh"
#include "platform.hh"
#include "safe_map.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "sanity.hh"
#include "ui.hh"
#include "cert.hh"
#include "charset.hh"
#include "ssh_agent.hh"
#include "database.hh"
#include "options.hh"

using std::cout;
using std::make_pair;
using std::map;
using std::string;
using std::vector;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::shared_dynamic_cast;

using Botan::byte;
using Botan::get_cipher;
using Botan::PKCS8_PrivateKey;
using Botan::PK_Decryptor;
using Botan::PK_Encryptor;
using Botan::PK_Signer;
using Botan::Pipe;
using Botan::RSA_PrivateKey;
using Botan::RSA_PublicKey;
using Botan::SecureVector;
using Botan::X509_PublicKey;

// there will probably forever be bugs in this file. it's very
// hard to get right, portably and securely. sorry about that.

// "raw" passphrase prompter; unaware of passphrase caching or the laziness
// hook.  KEYID is used only in prompts.  CONFIRM_PHRASE causes the user to
// be prompted to type the same thing twice, and will loop if they don't
// match.  Prompts are worded slightly differently if GENERATING_KEY is true.
void
get_passphrase(utf8 & phrase,
               rsa_keypair_id const & keyid,
               bool confirm_phrase,
               bool generating_key)
{
  string prompt1, prompt2;
  char pass1[constants::maxpasswd];
  char pass2[constants::maxpasswd];
  int i = 0;

  if (confirm_phrase && !generating_key)
    prompt1 = (F("enter new passphrase for key ID [%s]: ") % keyid).str();
  else
    prompt1 = (F("enter passphrase for key ID [%s]: ") % keyid).str();

  if (confirm_phrase)
    prompt2 = (F("confirm passphrase for key ID [%s]: ") % keyid).str();
  
  try
    {
      for (;;)
        {
          memset(pass1, 0, constants::maxpasswd);
          memset(pass2, 0, constants::maxpasswd);
          ui.ensure_clean_line();

          read_password(prompt1, pass1, constants::maxpasswd);
          if (!confirm_phrase)
            break;

          ui.ensure_clean_line();
          read_password(prompt2, pass2, constants::maxpasswd);
          if (strcmp(pass1, pass2) == 0)
            break;

          N(i++ < 2, F("too many failed passphrases"));
          P(F("passphrases do not match, try again"));
        }

      external ext_phrase(pass1);
      system_to_utf8(ext_phrase, phrase);
    }
  catch (...)
    {
      memset(pass1, 0, constants::maxpasswd);
      memset(pass2, 0, constants::maxpasswd);
      throw;
    }
  memset(pass1, 0, constants::maxpasswd);
  memset(pass2, 0, constants::maxpasswd);
}

// 'force_from_user' means that we don't use the passphrase cache, and we
// don't use the get_passphrase hook.
static void
get_passphrase(key_store & keys,
               utf8 & phrase,
               rsa_keypair_id const & keyid,
               bool force_from_user)
{
  // we permit the user to relax security here, by caching a passphrase (if
  // they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.
  bool persist_phrase = keys.hook_persist_phrase_ok();
  static map<rsa_keypair_id, utf8> phrases;
  if (!force_from_user && phrases.find(keyid) != phrases.end())
    {
      phrase = phrases[keyid];
      return;
    }

  string lua_phrase;
  if (!force_from_user && keys.hook_get_passphrase(keyid, lua_phrase))
    {
      N(!lua_phrase.empty(),
        F("got empty passphrase from get_passphrase() hook"));

      // user is being a slob and hooking lua to return his passphrase
      phrase = utf8(lua_phrase);
      return;
    }

  get_passphrase(phrase, keyid, false, false);

  // permit security relaxation. maybe.
  if (persist_phrase)
    {
      phrases.erase(keyid);
      safe_insert(phrases, make_pair(keyid, phrase));
    }
}

// Loads a key pair for a given key id, considering it a user error
// if that key pair is not available.

void
load_key_pair(key_store & keys, rsa_keypair_id const & id)
{
  N(keys.key_pair_exists(id),
    F("no key pair '%s' found in key store '%s'")
    % id % keys.get_key_dir());
}

void
load_key_pair(key_store & keys,
              rsa_keypair_id const & id,
              keypair & kp)
{
  load_key_pair(keys, id);
  keys.get_key_pair(id, kp);
}

// Find the key to be used for signing certs.  If possible, ensure the
// database and the key_store agree on that key, and cache it in decrypted
// form, so as not to bother the user for their passphrase later.

void
get_user_key(rsa_keypair_id & key,
             options const & opts, lua_hooks & lua,
             key_store & keys, database & db)
{
  if (!keys.signing_key().empty())
    {
      key = keys.signing_key;
      return;
    }

  if (!opts.signing_key().empty())
    key = opts.signing_key;
  else if (lua.hook_get_branch_key(opts.branchname, key))
    ; // the lua hook sets the key
  else
    {
      vector<rsa_keypair_id> all_privkeys;
      keys.get_key_ids(all_privkeys);
      N(all_privkeys.size() > 0, 
        F("you have no private key to make signatures with\n"
          "perhaps you need to 'genkey <your email>'"));
      N(all_privkeys.size() < 2,
        F("you have multiple private keys\n"
          "pick one to use for signatures by adding "
          "'-k<keyname>' to your command"));

      key = all_privkeys[0];
    }

  keys.signing_key = key;

  // Ensure that the specified key actually exists.
  keypair priv_key;
  load_key_pair(keys, key, priv_key);
  
  // we can only do the steps below if we have a database.
  if (!db.database_specified())
    return;

  // If the database doesn't have this public key, add it now; otherwise
  // make sure the database and key-store agree on the public key.
  if (!db.public_key_exists(key))
    db.put_key(key, priv_key.pub);
  else
    {
      base64<rsa_pub_key> pub_key;
      db.get_key(key, pub_key);
      E(keys_match(key, pub_key, key, priv_key.pub),
        F("The key '%s' stored in your database does\n"
          "not match the version in your local key store!") % key);
    }

  // If permitted, decrypt and cache the key now.
  if (keys.hook_persist_phrase_ok())
    {
      string plaintext("hi maude");
      base64<rsa_sha1_signature> sig;
      keys.make_signature(db, key, plaintext, sig);
    }
}

// As above, but does not report which key has been selected; for use when
// the important thing is to have selected one and cached the decrypted key.
void
cache_user_key(options const & opts, lua_hooks & lua,
               key_store & keys, database & db)
{
  rsa_keypair_id key;
  get_user_key(key, opts, lua, keys, db);
}

void
generate_key_pair(keypair & kp_out,
                  utf8 const phrase)
{
  SecureVector<Botan::byte> pubkey, privkey;
  rsa_pub_key raw_pub_key;
  rsa_priv_key raw_priv_key;

  // generate private key (and encrypt it)
  RSA_PrivateKey priv(constants::keylen);

  Pipe p;
  p.start_msg();
  if (phrase().length()) {
    Botan::PKCS8::encrypt_key(priv,
                              p,
                              phrase(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                              Botan::RAW_BER);
  } else {
    Botan::PKCS8::encode(priv, p);
  }
  raw_priv_key = rsa_priv_key(p.read_all_as_string());

  // generate public key
  Pipe p2;
  p2.start_msg();
  Botan::X509::encode(priv, p2, Botan::RAW_BER);
  raw_pub_key = rsa_pub_key(p2.read_all_as_string());

  // if all that worked, we can return our results to caller
  encode_base64(raw_priv_key, kp_out.priv);
  encode_base64(raw_pub_key, kp_out.pub);
  L(FL("generated %d-byte public key\n"
      "generated %d-byte (encrypted) private key\n")
    % kp_out.pub().size()
    % kp_out.priv().size());
}

// ask for passphrase then decrypt a private key.
shared_ptr<RSA_PrivateKey>
get_private_key(key_store & keys,
                rsa_keypair_id const & id,
                base64< rsa_priv_key > const & priv,
                bool force_from_user)
{
  rsa_priv_key decoded_key;
  utf8 phrase;
  bool force = force_from_user;

  L(FL("base64-decoding %d-byte private key") % priv().size());
  decode_base64(priv, decoded_key);
  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  try //with empty passphrase
    {
      Pipe p;
      p.process_msg(decoded_key());
      pkcs8_key = shared_ptr<PKCS8_PrivateKey>(Botan::PKCS8::load_key(p, phrase()));
    }
  catch (...)
    {
      L(FL("failed to decrypt key with no passphrase"));
    }
  if (!pkcs8_key)
    {
      for (int i = 0; i < 3; ++i)
        {
          get_passphrase(keys, phrase, id, force);
          L(FL("have %d-byte encrypted private key") % decoded_key().size());

          try
            {
              Pipe p;
              p.process_msg(decoded_key());
              pkcs8_key = shared_ptr<PKCS8_PrivateKey>(Botan::PKCS8::load_key(p, phrase()));
              break;
            }
          catch (...)
            {
              if (i >= 2)
                throw informative_failure("failed to decrypt private RSA key, "
                                          "probably incorrect passphrase");
              // don't use the cached bad one next time
              force = true;
              continue;
            }
        }
    }
  if (pkcs8_key)
    {
      shared_ptr<RSA_PrivateKey> priv_key;
      priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
      if (!priv_key)
        throw informative_failure("Failed to get RSA signing key");

      return priv_key;
    }
  I(false);
}

void
change_key_passphrase(key_store & keys,
                      rsa_keypair_id const & id,
                      base64< rsa_priv_key > & encoded_key)
{
  shared_ptr<RSA_PrivateKey> priv
    = get_private_key(keys, id, encoded_key, true);

  utf8 new_phrase;
  get_passphrase(new_phrase, id, true, false);

  Pipe p;
  p.start_msg();
  Botan::PKCS8::encrypt_key(*priv, p, new_phrase(),
                            "PBE-PKCS5v20(SHA-1,TripleDES/CBC)", Botan::RAW_BER);
  rsa_priv_key decoded_key = rsa_priv_key(p.read_all_as_string());

  encode_base64(decoded_key, encoded_key);
}

void encrypt_rsa(key_store & keys,
                 rsa_keypair_id const & id,
                 base64<rsa_pub_key> & pub_encoded,
                 string const & plaintext,
                 rsa_oaep_sha_data & ciphertext)
{
  rsa_pub_key pub;
  decode_base64(pub_encoded, pub);
  SecureVector<Botan::byte> pub_block;
  pub_block.set(reinterpret_cast<Botan::byte const *>(pub().data()), pub().size());

  shared_ptr<X509_PublicKey> x509_key = shared_ptr<X509_PublicKey>(Botan::X509::load_key(pub_block));
  shared_ptr<RSA_PublicKey> pub_key = shared_dynamic_cast<RSA_PublicKey>(x509_key);
  if (!pub_key)
    throw informative_failure("Failed to get RSA encrypting key");

  shared_ptr<PK_Encryptor> encryptor;
  encryptor = shared_ptr<PK_Encryptor>(get_pk_encryptor(*pub_key, "EME1(SHA-1)"));

  SecureVector<Botan::byte> ct;
  ct = encryptor->encrypt(
          reinterpret_cast<Botan::byte const *>(plaintext.data()), plaintext.size());
  ciphertext = rsa_oaep_sha_data(string(reinterpret_cast<char const *>(ct.begin()), ct.size()));
}

void decrypt_rsa(key_store & keys,
                 rsa_keypair_id const & id,
                 base64< rsa_priv_key > const & priv,
                 rsa_oaep_sha_data const & ciphertext,
                 string & plaintext)
{
  shared_ptr<RSA_PrivateKey> priv_key = get_private_key(keys, id, priv);

  shared_ptr<PK_Decryptor> decryptor;
  decryptor = shared_ptr<PK_Decryptor>(get_pk_decryptor(*priv_key, "EME1(SHA-1)"));

  SecureVector<Botan::byte> plain;
  plain = decryptor->decrypt(
        reinterpret_cast<Botan::byte const *>(ciphertext().data()), ciphertext().size());
  plaintext = string(reinterpret_cast<char const*>(plain.begin()), plain.size());
}

void
key_hash_code(rsa_keypair_id const & ident,
              base64<rsa_pub_key> const & pub,
              hexenc<id> & out)
{
  data tdat(ident() + ":" + remove_ws(pub()));
  calculate_ident(tdat, out);
}

void
key_hash_code(rsa_keypair_id const & ident,
              base64< rsa_priv_key > const & priv,
              hexenc<id> & out)
{
  data tdat(ident() + ":" + remove_ws(priv()));
  calculate_ident(tdat, out);
}

// helper to compare if two keys have the same hash
// (ie are the same key)
bool
keys_match(rsa_keypair_id const & id1,
           base64<rsa_pub_key> const & key1,
           rsa_keypair_id const & id2,
           base64<rsa_pub_key> const & key2)
{
  hexenc<id> hash1, hash2;
  key_hash_code(id1, key1, hash1);
  key_hash_code(id2, key2, hash2);
  return hash1 == hash2;
}

bool
keys_match(rsa_keypair_id const & id1,
           base64< rsa_priv_key > const & key1,
           rsa_keypair_id const & id2,
           base64< rsa_priv_key > const & key2)
{
  hexenc<id> hash1, hash2;
  key_hash_code(id1, key1, hash1);
  key_hash_code(id2, key2, hash2);
  return hash1 == hash2;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
