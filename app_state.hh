#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

class app_state;
class lua_hooks;

#include <boost/shared_ptr.hpp>
#include <botan/pubkey.h>
#include <botan/rsa.h>

#include "database.hh"
#include "lua.hh"
#include "work.hh"
#include "vocab.hh"

// this class is supposed to hold all (or.. well, most) of the state of the
// application, barring some unfortunate static objects like the debugging /
// logging system and the command objects, for the time being. the vague intent
// being to make life easier for anyone who wants to embed this program as a
// library someday

class app_state
{
public:
  rsa_keypair_id signing_key;
  utf8 branch_name;
  database db;
  lua_hooks lua;
  bool options_changed;
  options_map options;

  /* These are used to cache signers/verifiers (if the hook allows).
   * They can't be function-static variables in key.cc, since they must be
   * destroyed before the Botan deinitialize() function is called. */
  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Signer>, 
        boost::shared_ptr<Botan::RSA_PrivateKey> > > signers;
  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Verifier>,
        boost::shared_ptr<Botan::RSA_PublicKey> > > verifiers;

  void set_branch(utf8 const & name);
  void set_database(utf8 const & filename);
  void set_signing_key(utf8 const & key);
  
  void write_options(bool force = false);

  explicit app_state();
  ~app_state();
private:
};

#endif // __APP_STATE_HH__
