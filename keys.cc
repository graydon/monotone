#include <iostream>
#include <string>
#include <map>

#include <unistd.h>
#include <string.h>

#include <boost/shared_ptr.hpp>

#include "cryptopp/arc4.h"
#include "cryptopp/base64.h"
#include "cryptopp/hex.h"
#include "cryptopp/cryptlib.h"
#include "cryptopp/osrng.h"
#include "cryptopp/sha.h"
#include "cryptopp/rsa.h"

#include "constants.hh"
#include "keys.hh"
#include "lua.hh"
#include "netio.hh"
#include "platform.hh"
#include "transforms.hh"
#include "sanity.hh"
#include "ui.hh"

// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// there will probably forever be bugs in this file. it's very
// hard to get right, portably and securely. sorry about that.

using namespace CryptoPP;
using namespace std;

using boost::shared_ptr;

static void 
do_arc4(SecByteBlock & phrase,
        SecByteBlock & payload)
{
  L(F("running arc4 process on %d bytes of data\n") % payload.size());
  ARC4 a4(phrase.data(), phrase.size());
  a4.ProcessString(payload.data(), payload.size());
}

// 'force_from_user' means that we don't use the passphrase cache, and we
// don't use the get_passphrase hook.
static void 
get_passphrase(lua_hooks & lua,
               rsa_keypair_id const & keyid,
               SecByteBlock & phrase,
               bool confirm_phrase = false,
               bool force_from_user = false,
               string prompt_beginning = "enter passphrase")
{
  string lua_phrase;

  // we permit the user to relax security here, by caching a passphrase (if
  // they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.
  bool persist_phrase = lua.hook_persist_phrase_ok();
  static std::map<rsa_keypair_id, string> phrases;
  
  if (!force_from_user && phrases.find(keyid) != phrases.end())
    {
      string phr = phrases[keyid];
      phrase.Assign(reinterpret_cast<byte const *>(phr.data()), phr.size());
      return;
    }

  if (!force_from_user && lua.hook_get_passphrase(keyid, lua_phrase))
    {
      // user is being a slob and hooking lua to return his passphrase
      phrase.Assign(reinterpret_cast<const byte *>(lua_phrase.data()), 
                    lua_phrase.size());
      N(lua_phrase != "",
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
          read_password(prompt_beginning + " for key ID [" + keyid() + "]: ",
                        pass1, constants::maxpasswd);
          cout << endl;
          N(pass1[0],
            F("empty passphrase not allowed"));
          if (confirm_phrase)
            {
              ui.ensure_clean_line();
              read_password(string("confirm passphrase for key ID [") + keyid() + "]: ",
                            pass2, constants::maxpasswd);
              cout << endl;
              if (strlen(pass1) == 0 || strlen(pass2) == 0)
                {
                  P(F("empty passphrases not allowed, try again\n"));
                  N(i < 2, F("too many failed passphrases\n"));
                }
              else if (strcmp(pass1, pass2) == 0)
                break;
              else
                {
                  P(F("passphrases do not match, try again\n"));
                  N(i < 2, F("too many failed passphrases\n"));
                }
            }
          else
            break;
        }

      try 
        {
          phrase.Assign(reinterpret_cast<byte const *>(pass1), strlen(pass1));

          // permit security relaxation. maybe.
          if (persist_phrase)
            {
              phrases.insert(make_pair(keyid,string(pass1)));
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

template <typename T>
static void 
write_der(T & val, SecByteBlock & sec)
{
  // FIXME: this helper is *wrong*. I don't see how to DER-encode into a
  // SecByteBlock, so we may well wind up leaving raw key bytes in malloc
  // regions if we're not lucky. but we want to. maybe muck with
  // AllocatorWithCleanup<T>?  who knows..  please fix!
  string der_encoded;
  try
    {
      StringSink der_sink(der_encoded);
      val.DEREncode(der_sink);
      der_sink.MessageEnd();
      sec.Assign(reinterpret_cast<byte const *>(der_encoded.data()), 
                 der_encoded.size());
      L(F("wrote %d bytes of DER-encoded data\n") % der_encoded.size());
    }
  catch (...)
    {
      for (size_t i = 0; i < der_encoded.size(); ++i)
        der_encoded[i] = '\0';
      throw;
    }
  for (size_t i = 0; i < der_encoded.size(); ++i)
    der_encoded[i] = '\0';
}  

static bool
blocking_rng(lua_hooks & lua)
{
  if (!lua.hook_non_blocking_rng_ok())
    {
#ifndef BLOCKING_RNG_AVAILABLE
      throw oops("no blocking RNG available and non-blocking RNG rejected");
#else
      return true;
#endif
    };
  return false;
}


void 
generate_key_pair(lua_hooks & lua,              // to hook for phrase
                  rsa_keypair_id const & id,    // to prompting user for phrase
                  base64<rsa_pub_key> & pub_out,
                  base64< arc4<rsa_priv_key> > & priv_out,
                  string const unit_test_passphrase)
{
  // we will panic here if the user doesn't like urandom and we can't give
  // them a real entropy-driven random.  
  AutoSeededRandomPool rng(blocking_rng(lua));
  SecByteBlock phrase, pubkey, privkey;
  rsa_pub_key raw_pub_key;
  arc4<rsa_priv_key> raw_priv_key;
  
  // generate private key (and encrypt it)
  RSAES_OAEP_SHA_Decryptor priv(rng, constants::keylen);
  write_der(priv, privkey);

  if (unit_test_passphrase.empty())
    get_passphrase(lua, id, phrase, true, true);
  else
    phrase.Assign(reinterpret_cast<byte const *>(unit_test_passphrase.c_str()),
                  unit_test_passphrase.size());
  do_arc4(phrase, privkey); 
  raw_priv_key = string(reinterpret_cast<char const *>(privkey.data()), 
                        privkey.size());
  
  // generate public key
  RSAES_OAEP_SHA_Encryptor pub(priv);
  write_der(pub, pubkey);
  raw_pub_key = string(reinterpret_cast<char const *>(pubkey.data()), 
                       pubkey.size());
  
  // if all that worked, we can return our results to caller
  encode_base64(raw_priv_key, priv_out);
  encode_base64(raw_pub_key, pub_out);
  L(F("generated %d-byte public key\n") % pub_out().size());
  L(F("generated %d-byte (encrypted) private key\n") % priv_out().size());
}

void
change_key_passphrase(lua_hooks & lua,
                      rsa_keypair_id const & id,
                      base64< arc4<rsa_priv_key> > & encoded_key)
{
  SecByteBlock phrase;
  get_passphrase(lua, id, phrase, false, true, "enter old passphrase");

  arc4<rsa_priv_key> decoded_key;
  SecByteBlock key_block;
  decode_base64(encoded_key, decoded_key);
  key_block.Assign(reinterpret_cast<byte const *>(decoded_key().data()),
                   decoded_key().size());
  do_arc4(phrase, key_block);

  try
    {
      L(F("building signer from %d-byte decrypted private key\n") % key_block.size());
      StringSource keysource(key_block.data(), key_block.size(), true);
      shared_ptr<RSASSA_PKCS1v15_SHA_Signer> signer;
      signer = shared_ptr<RSASSA_PKCS1v15_SHA_Signer>
        (new RSASSA_PKCS1v15_SHA_Signer(keysource));
    }
  catch (...)
    {
      throw informative_failure("failed to decrypt private RSA key, "
                                "probably incorrect passphrase");
    }

  get_passphrase(lua, id, phrase, true, true, "enter new passphrase");
  do_arc4(phrase, key_block);
  decoded_key = string(reinterpret_cast<char const *>(key_block.data()),
                       key_block.size());
  encode_base64(decoded_key, encoded_key);
}

void 
make_signature(lua_hooks & lua,           // to hook for phrase
               rsa_keypair_id const & id, // to prompting user for phrase
               base64< arc4<rsa_priv_key> > const & priv,
               string const & tosign,
               base64<rsa_sha1_signature> & signature)
{
  arc4<rsa_priv_key> decoded_key;
  SecByteBlock decrypted_key;
  SecByteBlock phrase;
  string sig_string;

  // we will panic here if the user doesn't like urandom and we can't give
  // them a real entropy-driven random.  
  AutoSeededRandomPool rng(blocking_rng(lua));

  // we permit the user to relax security here, by caching a decrypted key
  // (if they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.

  static std::map<rsa_keypair_id, shared_ptr<RSASSA_PKCS1v15_SHA_Signer> > signers;
  bool persist_phrase = (!signers.empty()) || lua.hook_persist_phrase_ok();
  bool force = false;

  shared_ptr<RSASSA_PKCS1v15_SHA_Signer> signer;
  if (persist_phrase && signers.find(id) != signers.end())
    signer = signers[id];

  else
    {
      for (int i = 0; i < 3; ++i)
        {
          L(F("base64-decoding %d-byte private key\n") % priv().size());
          decode_base64(priv, decoded_key);
          decrypted_key.Assign(reinterpret_cast<byte const *>(decoded_key().data()), 
                               decoded_key().size());
          get_passphrase(lua, id, phrase, false, force);
          
          try 
            {
              do_arc4(phrase, decrypted_key);
              L(F("building signer from %d-byte decrypted private key\n") % decrypted_key.size());
              StringSource keysource(decrypted_key.data(), decrypted_key.size(), true);
              signer = shared_ptr<RSASSA_PKCS1v15_SHA_Signer>
                (new RSASSA_PKCS1v15_SHA_Signer(keysource));
            }
          catch (...)
            {
              if (i >= 2)
                throw informative_failure("failed to decrypt private RSA key, "
                                          "probably incorrect passphrase");
              // don't use the cache bad one next time
              force = true;
              continue;
            }
          
          if (persist_phrase)
            signers.insert(make_pair(id,signer));
          break;
        }
    }

  StringSource tmp(tosign, true, 
                   new SignerFilter
                   (rng, *signer, 
                    new StringSink(sig_string)));  
  
  L(F("produced %d-byte signature\n") % sig_string.size());
  encode_base64(rsa_sha1_signature(sig_string), signature);
}

bool 
check_signature(lua_hooks & lua,           
                rsa_keypair_id const & id, 
                base64<rsa_pub_key> const & pub_encoded,
                string const & alleged_text,
                base64<rsa_sha1_signature> const & signature)
{
  // examine pubkey

  static std::map<rsa_keypair_id, shared_ptr<RSASSA_PKCS1v15_SHA_Verifier> > verifiers;
  bool persist_phrase = (!verifiers.empty()) || lua.hook_persist_phrase_ok();

  shared_ptr<RSASSA_PKCS1v15_SHA_Verifier> verifier;
  if (persist_phrase 
      && verifiers.find(id) != verifiers.end())
    verifier = verifiers[id];

  else
    {
      rsa_pub_key pub;
      decode_base64(pub_encoded, pub);
      SecByteBlock pub_block;
      pub_block.Assign(reinterpret_cast<byte const *>(pub().data()), pub().size());
      StringSource keysource(pub_block.data(), pub_block.size(), true);
      L(F("building verifier for %d-byte pub key\n") % pub_block.size());
      verifier = shared_ptr<RSASSA_PKCS1v15_SHA_Verifier>
        (new RSASSA_PKCS1v15_SHA_Verifier(keysource));
      
      if (persist_phrase)
        verifiers.insert(make_pair(id, verifier));
    }

  // examine signature
  rsa_sha1_signature sig_decoded;
  decode_base64(signature, sig_decoded);
  if (sig_decoded().size() != verifier->SignatureLength())
    return false;

  // check the text+sig against the key
  L(F("checking %d-byte (%d decoded) signature\n") % 
    signature().size() % sig_decoded().size());
  VerifierFilter * vf = NULL;

  // crypto++ likes to use pointers in ways which boost and std:: smart
  // pointers aren't really good with, unfortunately.
  try 
    {
      vf = new VerifierFilter(*verifier);
      vf->Put(reinterpret_cast<byte const *>(sig_decoded().data()), sig_decoded().size());
    } 
  catch (...)
    {
      if (vf)
        delete vf;
      throw;
    }

  I(vf);
  StringSource tmp(alleged_text, true, vf);
  return vf->GetLastResult();
}

void encrypt_rsa(lua_hooks & lua,
                 rsa_keypair_id const & id,
                 base64<rsa_pub_key> & pub_encoded,
                 std::string const & plaintext,
                 rsa_oaep_sha_data & ciphertext)
{
  AutoSeededRandomPool rng(blocking_rng(lua));

  rsa_pub_key pub;
  decode_base64(pub_encoded, pub);
  SecByteBlock pub_block;
  pub_block.Assign(reinterpret_cast<byte const *>(pub().data()), pub().size());
  StringSource keysource(pub_block.data(), pub_block.size(), true);

  shared_ptr<RSAES_OAEP_SHA_Encryptor> encryptor;
  encryptor = shared_ptr<RSAES_OAEP_SHA_Encryptor>
    (new RSAES_OAEP_SHA_Encryptor(keysource));

  string ciphertext_string;
  StringSource tmp(plaintext, true,
                   encryptor->CreateEncryptionFilter
                   (rng, new StringSink(ciphertext_string)));

  ciphertext = rsa_oaep_sha_data(ciphertext_string);
}

void decrypt_rsa(lua_hooks & lua,
                 rsa_keypair_id const & id,
                 base64< arc4<rsa_priv_key> > const & priv,
                 rsa_oaep_sha_data const & ciphertext,
                 std::string & plaintext)
{
  AutoSeededRandomPool rng(blocking_rng(lua));
  arc4<rsa_priv_key> decoded_key;
  SecByteBlock decrypted_key;
  SecByteBlock phrase;
  shared_ptr<RSAES_OAEP_SHA_Decryptor> decryptor;

  for (int i = 0; i < 3; i++)
    {
      bool force = false;
      decode_base64(priv, decoded_key);
      decrypted_key.Assign(reinterpret_cast<byte const *>(decoded_key().data()), 
                           decoded_key().size());
      get_passphrase(lua, id, phrase, false, force);

      try 
        {
          do_arc4(phrase, decrypted_key);
          StringSource keysource(decrypted_key.data(), decrypted_key.size(), true);
          decryptor = shared_ptr<RSAES_OAEP_SHA_Decryptor>
            (new RSAES_OAEP_SHA_Decryptor(keysource));
        }
      catch (...)
        {
          if (i >= 2)
            throw informative_failure("failed to decrypt private RSA key, "
                                      "probably incorrect passphrase");
          // don't use the cache bad one next time
          force = true;
          continue;
        }
    }

  StringSource tmp(ciphertext(), true,
                   decryptor->CreateDecryptionFilter
                   (rng, new StringSink(plaintext)));
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
  id = tmp_id;
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
key_hash_code(rsa_keypair_id const & id,
              base64<rsa_pub_key> const & pub,
              hexenc<id> & out)
{
  data tdat(id() + ":" + remove_ws(pub()));
  calculate_ident(tdat, out);  
}

void 
key_hash_code(rsa_keypair_id const & id,
              base64< arc4<rsa_priv_key> > const & priv,
              hexenc<id> & out)
{
  data tdat(id() + ":" + remove_ws(priv()));
  calculate_ident(tdat, out);
}

void
require_password(rsa_keypair_id const & key,
                 app_state & app)
{
  N(priv_key_exists(app, key),
    F("no private key '%s' found in database or get_priv_key hook") % key);
  N(app.db.public_key_exists(key),
    F("no public key '%s' found in database") % key);
  base64<rsa_pub_key> pub;
  app.db.get_key(key, pub);
  base64< arc4<rsa_priv_key> > priv;
  load_priv_key(app, key, priv);
  if (app.lua.hook_persist_phrase_ok())
    {
      string plaintext("hi maude");
      base64<rsa_sha1_signature> sig;
      make_signature(app.lua, key, priv, plaintext, sig);
      N(check_signature(app.lua, key, pub, plaintext, sig),
        F("passphrase for '%s' is incorrect") % key);
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void 
signature_round_trip_test()
{
  lua_hooks lua;
  lua.add_std_hooks();
  lua.add_test_hooks();
  
  BOOST_CHECKPOINT("generating key pairs");
  rsa_keypair_id key("bob123@test.com");
  base64<rsa_pub_key> pubkey;
  base64< arc4<rsa_priv_key> > privkey;
  generate_key_pair(lua, key, pubkey, privkey, "bob123@test.com");

  BOOST_CHECKPOINT("signing plaintext");
  string plaintext("test string to sign");
  base64<rsa_sha1_signature> sig;
  make_signature(lua, key, privkey, plaintext, sig);
  
  BOOST_CHECKPOINT("checking signature");
  BOOST_CHECK(check_signature(lua, key, pubkey, plaintext, sig));
  
  string broken_plaintext = plaintext + " ...with a lie";
  BOOST_CHECKPOINT("checking non-signature");
  BOOST_CHECK(!check_signature(lua, key, pubkey, broken_plaintext, sig));
}

static void 
osrng_test()
{
  AutoSeededRandomPool rng_random(true), rng_urandom(false);

  for (int round = 0; round < 20; ++round)
    {
      MaurerRandomnessTest t_blank, t_urandom, t_random;
      int i = 0;

      while (t_blank.BytesNeeded() != 0)
        {
          t_blank.Put(static_cast<byte>(0));
          i++;
        }
      L(F("%d bytes blank input -> tests as %f randomness\n") 
        % i % t_blank.GetTestValue());


      i = 0;
      while (t_urandom.BytesNeeded() != 0)
        {
          t_urandom.Put(rng_urandom.GenerateByte());
          i++;
        }
      L(F("%d bytes urandom-seeded input -> tests as %f randomness\n") 
        % i % t_urandom.GetTestValue());


      i = 0;
      while (t_random.BytesNeeded() != 0)
        {
          t_random.Put(rng_random.GenerateByte());
          i++;
        }

      L(F("%d bytes random-seeded input -> tests as %f randomness\n") 
        % i % t_random.GetTestValue());

      BOOST_CHECK(t_blank.GetTestValue() == 0.0);
      BOOST_CHECK(t_urandom.GetTestValue() > 0.95);
      BOOST_CHECK(t_random.GetTestValue() > 0.95);
    }
}

void 
add_key_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&osrng_test));
  suite->add(BOOST_TEST_CASE(&signature_round_trip_test));
}

#endif // BUILD_UNIT_TESTS
