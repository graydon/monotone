#include <iostream>
#include <string>
#include <map>

#include <unistd.h>
#include <string.h>

#include <boost/shared_ptr.hpp>

#include "botan/botan.h"
#include "botan/rsa.h"
#include "botan/keypair.h"

#include "constants.hh"
#include "keys.hh"
#include "lua.hh"
#include "netio.hh"
#include "platform.hh"
#include "transforms.hh"
#include "sanity.hh"

// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// there will probably forever be bugs in this file. it's very
// hard to get right, portably and securely. sorry about that.

using namespace Botan;
using namespace std;

using boost::shared_ptr;
using boost::shared_dynamic_cast;

static void 
do_arc4(SecureVector<byte> & phrase,
	SecureVector<byte> & payload)
{
  L(F("running arc4 process on %d bytes of data\n") % payload.size());
  Pipe enc(get_cipher("ARC4", phrase, ENCRYPTION));
  enc.process_msg(payload);
  payload = enc.read_all();
}

static void 
get_passphrase(lua_hooks & lua,
	       rsa_keypair_id const & keyid,
	       SecureVector<byte> & phrase,
	       bool confirm_phrase = false)
{
  string lua_phrase;

  // we permit the user to relax security here, by caching a passphrase (if
  // they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.
  bool persist_phrase = lua.hook_persist_phrase_ok();;
  static std::map<rsa_keypair_id, string> phrases;
  
  if (persist_phrase && phrases.find(keyid) != phrases.end())
    {
      string phr = phrases[keyid];
	  phrase.set(reinterpret_cast<byte const *>(phr.data()), phr.size());
      return;
    }

  if (lua.hook_get_passphrase(keyid, lua_phrase))
    {
      // user is being a slob and hooking lua to return his passphrase
      phrase.set(reinterpret_cast<const byte *>(lua_phrase.data()), 
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
	  read_password(string("enter passphrase for key ID [") + keyid() + "]: ", 
			pass1, constants::maxpasswd);
	  cout << endl;
	  if (confirm_phrase)
	    {
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
	  phrase.set(reinterpret_cast<byte const *>(pass1), strlen(pass1));

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


void 
generate_key_pair(lua_hooks & lua,           // to hook for phrase
		  rsa_keypair_id const & id, // to prompting user for phrase
		  base64<rsa_pub_key> & pub_out,
		  base64< arc4<rsa_priv_key> > & priv_out)
{
  
  SecureVector<byte> phrase, pubkey, privkey;
  rsa_pub_key raw_pub_key;
  arc4<rsa_priv_key> raw_priv_key;
  
  // generate private key (and encrypt it)
  RSA_PrivateKey priv(constants::keylen);

  Pipe p;
  p.start_msg();
  PKCS8::encode(priv, p, RAW_BER);
  privkey = p.read_all();
  
  get_passphrase(lua, id, phrase, true);
  do_arc4(phrase, privkey); 
  raw_priv_key = string(reinterpret_cast<char const *>(privkey.begin()), privkey.size());
  
  // generate public key
  Pipe p2;
  p2.start_msg();
  X509::encode(priv, p2, RAW_BER);
  pubkey = p2.read_all();
  raw_pub_key = string(reinterpret_cast<char const *>(pubkey.begin()), pubkey.size());
  
  // if all that worked, we can return our results to caller
  encode_base64(raw_priv_key, priv_out);
  encode_base64(raw_pub_key, pub_out);
  L(F("generated %d-byte public key\n") % pub_out().size());
  L(F("generated %d-byte (encrypted) private key\n") % priv_out().size());
}

void 
make_signature(app_state & app,           // to hook for phrase
	       rsa_keypair_id const & id, // to prompting user for phrase
	       base64< arc4<rsa_priv_key> > const & priv,
	       string const & tosign,
	       base64<rsa_sha1_signature> & signature)
{
  arc4<rsa_priv_key> decoded_key;
  SecureVector<byte> decrypted_key;
  SecureVector<byte> phrase;
  SecureVector<byte> sig;
  string sig_string;

  // we permit the user to relax security here, by caching a decrypted key
  // (if they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.

  bool persist_phrase = (!app.signers.empty()) || app.lua.hook_persist_phrase_ok();

  shared_ptr<PK_Signer> signer;
  shared_ptr<RSA_PrivateKey> priv_key;
  if (persist_phrase && app.signers.find(id) != app.signers.end())
    signer = app.signers[id].first;

  else
    {
      for (int i = 0; i < 3; ++i)
	{
	  L(F("base64-decoding %d-byte private key\n") % priv().size());
	  decode_base64(priv, decoded_key);
	  decrypted_key.set(reinterpret_cast<byte const *>(decoded_key().data()), 
			       decoded_key().size());
	  get_passphrase(app.lua, id, phrase);
	  do_arc4(phrase, decrypted_key);


	  L(F("building signer from %d-byte decrypted private key\n") % decrypted_key.size());

          shared_ptr<PKCS8_PrivateKey> pkcs8_key;
	  try 
            {
              Pipe p;
              p.process_msg(decrypted_key);
              pkcs8_key = shared_ptr<PKCS8_PrivateKey>(PKCS8::load_key(p));
            }
	  catch (...)
	    {
	      throw informative_failure("failed to decrypt private RSA key, "
					  "probably incorrect passphrase");
	    }

          priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
          if (!priv_key)
              throw informative_failure("Failed to get RSA signing key");

	  signer = shared_ptr<PK_Signer>(get_pk_signer(*priv_key, "EMSA3(SHA-1)"));
	  
          /* XXX This is ugly. We need to keep the key around as long
           * as the signer is around, but the shared_ptr for the key will go
           * away after we leave this scope. Hence we store a pair of
           * <verifier,key> so they both exist. */
	  if (persist_phrase)
	    app.signers.insert(make_pair(id,make_pair(signer,priv_key)));
	}
    }

  sig = signer->sign_message(reinterpret_cast<byte const *>(tosign.data()), tosign.size());
  sig_string = string(reinterpret_cast<char const*>(sig.begin()), sig.size());
  
  L(F("produced %d-byte signature\n") % sig_string.size());
  encode_base64(rsa_sha1_signature(sig_string), signature);
}

bool 
check_signature(app_state &app,
		rsa_keypair_id const & id, 
		base64<rsa_pub_key> const & pub_encoded,
		string const & alleged_text,
		base64<rsa_sha1_signature> const & signature)
{
  // examine pubkey
  bool persist_phrase = (!app.verifiers.empty()) || app.lua.hook_persist_phrase_ok();

  shared_ptr<PK_Verifier> verifier;
  shared_ptr<RSA_PublicKey> pub_key;
  if (persist_phrase 
      && app.verifiers.find(id) != app.verifiers.end())
    verifier = app.verifiers[id].first;

  else
    {
      rsa_pub_key pub;
      decode_base64(pub_encoded, pub);
      SecureVector<byte> pub_block;
      pub_block.set(reinterpret_cast<byte const *>(pub().data()), pub().size());

      L(F("building verifier for %d-byte pub key\n") % pub_block.size());

      shared_ptr<X509_PublicKey> x509_key =
          shared_ptr<X509_PublicKey>(X509::load_key(pub_block));
      pub_key = shared_dynamic_cast<RSA_PublicKey>(x509_key);
      if (!pub_key)
          throw informative_failure("Failed to get RSA verifying key");

      verifier = shared_ptr<PK_Verifier>(get_pk_verifier(*pub_key, "EMSA3(SHA-1)"));

      /* XXX This is ugly. We need to keep the key around 
       * as long as the verifier is around, but the shared_ptr will go 
       * away after we leave this scope. Hence we store a pair of
       * <verifier,key> so they both exist. */
      if (persist_phrase)
	app.verifiers.insert(make_pair(id, make_pair(verifier, pub_key)));
    }

  // examine signature
  rsa_sha1_signature sig_decoded;
  decode_base64(signature, sig_decoded);

  // check the text+sig against the key
  L(F("checking %d-byte (%d decoded) signature\n") % 
    signature().size() % sig_decoded().size());

  bool valid_sig = verifier->verify_message(
	  reinterpret_cast<byte const*>(alleged_text.data()), alleged_text.size(),
	  reinterpret_cast<byte const*>(sig_decoded().data()), sig_decoded().size());

  return valid_sig;
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
require_password(app_state & app,
		 rsa_keypair_id const & key,
		 base64<rsa_pub_key> const & pubkey,
		 base64< arc4<rsa_priv_key> > const & privkey)
{
  if (app.lua.hook_persist_phrase_ok())
    {
      string plaintext("hi maude");
      base64<rsa_sha1_signature> sig;
      make_signature(app, key, privkey, plaintext, sig);
      N(check_signature(app, key, pubkey, plaintext, sig),
	F("passphrase for '%s' is incorrect") % key);
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void
arc4_test()
{

  string pt("new fascist tidiness regime in place");
  string phr("still spring water");

  SecureVector<byte> phrase(reinterpret_cast<byte const*>(phr.data()),
    phr.size());

  SecureVector<byte> orig(reinterpret_cast<byte const*>(pt.data()),
    pt.size());

  SecureVector<byte> data(orig);

  BOOST_CHECKPOINT("encrypting data");
  do_arc4(phrase, data);

  BOOST_CHECK(data != orig);

  BOOST_CHECKPOINT("decrypting data");
  do_arc4(phrase, data);

  BOOST_CHECK(data == orig);

}

static void 
signature_round_trip_test()
{
  app_state app;
  app.lua.add_std_hooks();
  app.lua.add_test_hooks();

  BOOST_CHECKPOINT("generating key pairs");
  rsa_keypair_id key("bob123@test.com");
  base64<rsa_pub_key> pubkey;
  base64< arc4<rsa_priv_key> > privkey;
  generate_key_pair(app.lua, key, pubkey, privkey);

  BOOST_CHECKPOINT("signing plaintext");
  string plaintext("test string to sign");
  base64<rsa_sha1_signature> sig;
  make_signature(app, key, privkey, plaintext, sig);
  
  BOOST_CHECKPOINT("checking signature");
  BOOST_CHECK(check_signature(app, key, pubkey, plaintext, sig));
  
  string broken_plaintext = plaintext + " ...with a lie";
  BOOST_CHECKPOINT("checking non-signature");
  BOOST_CHECK(!check_signature(app, key, pubkey, broken_plaintext, sig));
}

void 
add_key_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&arc4_test));
  suite->add(BOOST_TEST_CASE(&signature_round_trip_test));
}

#endif // BUILD_UNIT_TESTS
