#include <iostream>
#include <string>
#include <map>

#include <termios.h>
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
#include "transforms.hh"
#include "sanity.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// there will probably forever be bugs in this file. it's very
// hard to get right, portably and securely. sorry about that.

using namespace CryptoPP;
using namespace std;

using boost::shared_ptr;

static void do_arc4(SecByteBlock & phrase,
		    SecByteBlock & payload)
{
  L(F("running arc4 process on %d bytes of data\n") % payload.size());
  ARC4 a4(phrase.data(), phrase.size());
  a4.ProcessString(payload.data(), payload.size());
}

static void read_passphrase(lua_hooks & lua,
			    rsa_keypair_id const & keyid,
			    SecByteBlock & phrase)
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
      phrase.Assign(reinterpret_cast<byte const *>(phr.data()), phr.size());
      return;
    }


  if (lua.hook_get_passphrase(keyid, lua_phrase))
    {
      // user is being a slob and hooking lua to return his passphrase
      phrase.Assign(reinterpret_cast<const byte *>(lua_phrase.data()), 
		    lua_phrase.size());
    }
  else
    { 
      // FIXME: we will drop extra bytes of their phrase
      size_t bufsz = 4096;
      char buf[bufsz];

      // out to the console for us!
      // FIXME: this is *way* non-portable at the moment.
      cout << "enter passphrase for key ID [" << keyid() <<  "] : ";
      cout.flush();
      
      int cin_fd = 0;
      struct termios t, t_saved;
      tcgetattr(cin_fd, &t);
      t_saved = t;
      t.c_lflag &= ~ECHO;
      tcsetattr(cin_fd, TCSANOW, &t);

      try 
	{
	  tcsetattr(cin_fd, TCSANOW, &t);
	  cin.getline(buf, bufsz, '\n');
	  phrase.Assign(reinterpret_cast<byte const *>(buf), strlen(buf));

	  // permit security relaxation. maybe.
	  if (persist_phrase)
	    {
	      phrases.insert(make_pair(keyid,string(buf)));
	    }
	} 
      catch (...)
	{
	  memset(buf, 0, bufsz);
	  tcsetattr(cin_fd, TCSANOW, &t_saved);
	  cout << endl;
	  throw;
	}
      cout << endl;
      memset(buf, 0, bufsz);
      tcsetattr(cin_fd, TCSANOW, &t_saved);
    }
}  

template <typename T>
static void write_der(T & val, SecByteBlock & sec)
{
  // FIXME: this helper is *wrong*. I don't see now to DER-encode into a
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


void generate_key_pair(lua_hooks & lua,           // to hook for phrase
		       rsa_keypair_id const & id, // to prompting user for phrase
		       base64<rsa_pub_key> & pub_out,
		       base64< arc4<rsa_priv_key> > & priv_out)
{
  // we will panic here if the user doesn't like urandom and we can't give
  // them a real entropy-driven random.  
  bool request_blocking_rng = false;
  if (!lua.hook_non_blocking_rng_ok())
    {
#ifndef BLOCKING_RNG_AVAILABLE 
      throw oops("no blocking RNG available and non-blocking RNG rejected");
#else
      request_blocking_rng = true;
#endif
    }
  
  AutoSeededRandomPool rng(request_blocking_rng);
  SecByteBlock phrase, pubkey, privkey;
  rsa_pub_key raw_pub_key;
  arc4<rsa_priv_key> raw_priv_key;
  
  // generate private key (and encrypt it)
  RSAES_OAEP_SHA_Decryptor priv(rng, constants::keylen);
  write_der(priv, privkey);
  read_passphrase(lua, id, phrase);
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

void make_signature(lua_hooks & lua,           // to hook for phrase
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
  bool request_blocking_rng = false;
  if (!lua.hook_non_blocking_rng_ok())
    {
#ifndef BLOCKING_RNG_AVAILABLE 
      throw oops("no blocking RNG available and non-blocking RNG rejected");
#else
      request_blocking_rng = true;
#endif
    }  
  AutoSeededRandomPool rng(request_blocking_rng);

  // we permit the user to relax security here, by caching a decrypted key
  // (if they permit it) through the life of a program run. this helps when
  // you're making a half-dozen certs during a commit or merge or
  // something.

  static std::map<rsa_keypair_id, shared_ptr<RSASSA_PKCS1v15_SHA_Signer> > signers;
  bool persist_phrase = (!signers.empty()) || lua.hook_persist_phrase_ok();;

  shared_ptr<RSASSA_PKCS1v15_SHA_Signer> signer;
  if (persist_phrase 
      && signers.find(id) != signers.end())
    signer = signers[id];

  else
    {
      L(F("base64-decoding %d-byte private key\n") % priv().size());
      decode_base64(priv, decoded_key);
      decrypted_key.Assign(reinterpret_cast<byte const *>(decoded_key().data()), 
			   decoded_key().size());
      read_passphrase(lua, id, phrase);
      do_arc4(phrase, decrypted_key);

      try 
	{
	  L(F("building signer from %d-byte decrypted private key\n") % decrypted_key.size());
	  StringSource keysource(decrypted_key.data(), decrypted_key.size(), true);
	  signer = shared_ptr<RSASSA_PKCS1v15_SHA_Signer>
	    (new RSASSA_PKCS1v15_SHA_Signer(keysource));
	}
      catch (...)
	{
	  throw informative_failure("failed to decrypt private RSA key, "
				    "probably incorrect passphrase");
	}

      if (persist_phrase)
	signers.insert(make_pair(id,signer));
    }

  StringSource tmp(tosign, true, 
		   new SignerFilter
		   (rng, *signer, 
		    new StringSink(sig_string)));  
  
  L(F("produced %d-byte signature\n") % sig_string.size());
  encode_base64(rsa_sha1_signature(sig_string), signature);
}

bool check_signature(lua_hooks & lua,           
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


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void signature_round_trip_test()
{
  lua_hooks lua;
  lua.add_std_hooks();
  lua.add_test_hooks();
  
  BOOST_CHECKPOINT("generating key pairs");
  rsa_keypair_id key("bob123@test.com");
  base64<rsa_pub_key> pubkey;
  base64< arc4<rsa_priv_key> > privkey;
  generate_key_pair(lua, key, pubkey, privkey);

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

void add_key_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&signature_round_trip_test));
}

#endif // BUILD_UNIT_TESTS
