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

static void
do_arc4(SecureVector<Botan::byte> & sym_key,
        SecureVector<Botan::byte> & payload)
{
  L(FL("running arc4 process on %d bytes of data") % payload.size());
  Pipe enc(get_cipher("ARC4", sym_key, Botan::ENCRYPTION));
  enc.process_msg(payload);
  payload = enc.read_all();
}

// 'force_from_user' means that we don't use the passphrase cache, and we
// don't use the get_passphrase hook.
void
get_passphrase(key_store & keys,
               rsa_keypair_id const & keyid,
               utf8 & phrase,
               bool confirm_phrase,
               bool force_from_user,
               bool generating_key)
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
      // user is being a slob and hooking lua to return his passphrase
      phrase = utf8(lua_phrase);
      N(phrase != utf8(""),
        F("got empty passphrase from get_passphrase() hook"));
    }
  else
    {
      char pass1[constants::maxpasswd];
      char pass2[constants::maxpasswd];
      for (int i = 0; i < 3; ++i)
        {
          memset(pass1, 0, constants::maxpasswd);
          memset(pass2, 0, constants::maxpasswd);
          ui.ensure_clean_line();
          string prompt1 = ((confirm_phrase && !generating_key
                             ? F("enter new passphrase for key ID [%s]: ")
                             : F("enter passphrase for key ID [%s]: "))
                            % keyid()).str();

          read_password(prompt1, pass1, constants::maxpasswd);
          if (confirm_phrase)
            {
              ui.ensure_clean_line();
              read_password((F("confirm passphrase for key ID [%s]: ")
                             % keyid()).str(),
                            pass2, constants::maxpasswd);
              if (strcmp(pass1, pass2) == 0)
                break;
              else
                {
                  P(F("passphrases do not match, try again"));
                  N(i < 2, F("too many failed passphrases"));
                }
            }
          else
            break;
        }

      try
        {
          external ext_phrase(pass1);
          system_to_utf8(ext_phrase, phrase);

          // permit security relaxation. maybe.
          if (persist_phrase)
            {
              phrases.erase(keyid);
              safe_insert(phrases, make_pair(keyid, phrase));
            }
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
}


void
generate_key_pair(key_store & keys,             // to hook for phrase
                  rsa_keypair_id const & id,    // to prompting user for phrase
                  keypair & kp_out)
{
  utf8 phrase;
  get_passphrase(keys, id, phrase, true, true, true);
  generate_key_pair(kp_out, phrase);
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
          get_passphrase(keys, id, phrase, false, force);
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

// converts an oldstyle arc4 encrypted key into a newstyle pkcs#8 encoded
// key. the public key is also included
void
migrate_private_key(key_store & keys,
                    rsa_keypair_id const & id,
                    base64< old_arc4_rsa_priv_key > const & old_priv,
                    keypair & new_kp)
{
  old_arc4_rsa_priv_key decoded_key;
  SecureVector<Botan::byte> decrypted_key;
  utf8 phrase;

  bool force = false;

  // need to decrypt the old key
  shared_ptr<RSA_PrivateKey> priv_key;
  L(FL("base64-decoding %d-byte old private key") % old_priv().size());
  decode_base64(old_priv, decoded_key);
  for (int i = 0; i < 3; ++i)
    {
      decrypted_key.set(reinterpret_cast<Botan::byte const *>(decoded_key().data()),
                           decoded_key().size());
      get_passphrase(keys, id, phrase, false, force);
      SecureVector<Botan::byte> sym_key;
      sym_key.set(reinterpret_cast<Botan::byte const *>(phrase().data()), phrase().size());
      do_arc4(sym_key, decrypted_key);

      L(FL("building signer from %d-byte decrypted private key") % decrypted_key.size());

      shared_ptr<PKCS8_PrivateKey> pkcs8_key;
      try
        {
          Pipe p;
          p.process_msg(Botan::PEM_Code::encode(decrypted_key, "PRIVATE KEY"));
          pkcs8_key = shared_ptr<PKCS8_PrivateKey>(Botan::PKCS8::load_key(p));
        }
      catch (...)
        {
          if (i >= 2)
            throw informative_failure("failed to decrypt old private RSA key, "
                                      "probably incorrect passphrase");
          // don't use the cache bad one next time
          force = true;
          continue;
        }

      priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
      if (!priv_key)
          throw informative_failure("Failed to get old RSA key");
    }

  I(priv_key);

  // now we can write out the new key
  Pipe p;
  p.start_msg();
  Botan::PKCS8::encrypt_key(*priv_key, p, phrase(),
                     "PBE-PKCS5v20(SHA-1,TripleDES/CBC)", Botan::RAW_BER);
  rsa_priv_key raw_priv = rsa_priv_key(p.read_all_as_string());
  encode_base64(raw_priv, new_kp.priv);

  // also the public portion
  Pipe p2;
  p2.start_msg();
  Botan::X509::encode(*priv_key, p2, Botan::RAW_BER);
  rsa_pub_key raw_pub = rsa_pub_key(p2.read_all_as_string());
  encode_base64(raw_pub, new_kp.pub);
}

void
change_key_passphrase(key_store & keys,
                      rsa_keypair_id const & id,
                      base64< rsa_priv_key > & encoded_key)
{
  shared_ptr<RSA_PrivateKey> priv = get_private_key(keys, id, encoded_key, true);

  utf8 new_phrase;
  get_passphrase(keys, id, new_phrase, true, true, "enter new passphrase");

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
read_pubkey(string const & in,
            rsa_keypair_id & id,
            base64<rsa_pub_key> & pub)
{
  string tmp_id, tmp_key;
  size_t pos = 0;
  extract_variable_length_string(in, tmp_id, pos, "pubkey id");
  extract_variable_length_string(in, tmp_key, pos, "pubkey value");
  id = rsa_keypair_id(tmp_id);
  encode_base64(rsa_pub_key(tmp_key), pub);
}

void
write_pubkey(rsa_keypair_id const & id,
             base64<rsa_pub_key> const & pub,
             string & out)
{
  rsa_pub_key pub_tmp;
  decode_base64(pub, pub_tmp);
  insert_variable_length_string(id(), out);
  insert_variable_length_string(pub_tmp(), out);
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

void
require_password(rsa_keypair_id const & key,
                 key_store & keys,
                 database & db)
{
  N(keys.key_pair_exists(key),
    F("no key pair '%s' found in key store '%s'")
    % key % keys.get_key_dir());

  if (keys.hook_persist_phrase_ok())
    {
      string plaintext("hi maude");
      base64<rsa_sha1_signature> sig;
      keys.make_signature(db, key, plaintext, sig);
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

// This is not much of a unit test, but there is no point in strengthening
// it, because the only thing we still use arc4 for is migrating *really*
// old in-database private keys out to pkcs#8 files in the keystore.
UNIT_TEST(key, arc4)
{
  static Botan::byte const pt[] = "new fascist tidiness regime in place";
  static Botan::byte const phr[] = "still spring water";

  SecureVector<Botan::byte> phrase(phr, sizeof phr - 1);
  SecureVector<Botan::byte> orig(pt, sizeof pt - 1);
  SecureVector<Botan::byte> data(orig);

  UNIT_TEST_CHECKPOINT("encrypting data");
  do_arc4(phrase, data);

  UNIT_TEST_CHECK(data != orig);

  UNIT_TEST_CHECKPOINT("decrypting data");
  do_arc4(phrase, data);

  UNIT_TEST_CHECK(data == orig);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
