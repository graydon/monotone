// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <cstring>

#include "keys.hh"
#include "sanity.hh"
#include "ui.hh"
#include "constants.hh"
#include "platform.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "lua_hooks.hh"
#include "options.hh"
#include "key_store.hh"
#include "database.hh"

using std::string;
using std::vector;
using std::memset;

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
get_user_key(options const & opts, lua_hooks & lua,
             database & db, key_store & keys, rsa_keypair_id & key)
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
      N(!all_privkeys.empty(), 
        F("you have no private key to make signatures with\n"
          "perhaps you need to 'genkey <your email>'"));
      N(all_privkeys.size() < 2,
        F("you have multiple private keys\n"
          "pick one to use for signatures by adding "
          "'-k<keyname>' to your command"));

      key = all_privkeys[0];
    }

  // Ensure that the specified key actually exists.
  keypair priv_key;
  load_key_pair(keys, key, priv_key);
  
  if (db.database_specified())
    {
      // If the database doesn't have this public key, add it now; otherwise
      // make sure the database and key-store agree on the public key.
      if (!db.public_key_exists(key))
        db.put_key(key, priv_key.pub);
      else
        {
          rsa_pub_key pub_key;
          db.get_key(key, pub_key);
          E(keys_match(key, pub_key, key, priv_key.pub),
            F("The key '%s' stored in your database does\n"
              "not match the version in your local key store!") % key);
        }
    }

  // Decrypt and cache the key now.
  keys.cache_decrypted_key(key);
}

// As above, but does not report which key has been selected; for use when
// the important thing is to have selected one and cached the decrypted key.
void
cache_user_key(options const & opts, lua_hooks & lua,
               database & db, key_store & keys)
{
  rsa_keypair_id key;
  get_user_key(opts, lua, db, keys, key);
}

void
key_hash_code(rsa_keypair_id const & ident,
              rsa_pub_key const & pub,
              id & out)
{
  data tdat(ident() + ":" + remove_ws(encode_base64(pub)()));
  calculate_ident(tdat, out);
}

void
key_hash_code(rsa_keypair_id const & ident,
              rsa_priv_key const & priv,
              id & out)
{
  data tdat(ident() + ":" + remove_ws(encode_base64(priv)()));
  calculate_ident(tdat, out);
}

// helper to compare if two keys have the same hash
// (ie are the same key)
bool
keys_match(rsa_keypair_id const & id1,
           rsa_pub_key const & key1,
           rsa_keypair_id const & id2,
           rsa_pub_key const & key2)
{
  id hash1, hash2;
  key_hash_code(id1, key1, hash1);
  key_hash_code(id2, key2, hash2);
  return hash1 == hash2;
}

bool
keys_match(rsa_keypair_id const & id1,
           rsa_priv_key const & key1,
           rsa_keypair_id const & id2,
           rsa_priv_key const & key2)
{
  id hash1, hash2;
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
