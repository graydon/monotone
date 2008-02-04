#ifndef __KEY_STORE_H__
#define __KEY_STORE_H__

#include <map>
#include "vector.hh"
#include <string>

#include <boost/shared_ptr.hpp>

#include "vocab.hh"
#include "paths.hh"

class app_state;
struct globish;
class ssh_agent;

namespace Botan
{
  class PK_Signer;
  class RSA_PrivateKey;
  class PK_Verifier;
  class RSA_PublicKey;
};

class key_store
{
private:
  system_path key_dir;
  bool have_read;
  app_state & app;
  std::map<rsa_keypair_id, keypair> keys;
  std::map<id, rsa_keypair_id> hashes;

  void get_key_file(rsa_keypair_id const & ident, system_path & file);
  void write_key(rsa_keypair_id const & ident);
  void read_key_dir();
  void maybe_read_key_dir();
public:
  key_store(app_state & a);
  void set_key_dir(system_path const & kd);
  system_path const & get_key_dir();

  void get_key_ids(std::vector<rsa_keypair_id> & priv);
  void get_key_ids(globish const & pattern,
                   std::vector<rsa_keypair_id> & priv);

  bool key_pair_exists(rsa_keypair_id const & ident);

  void get_key_pair(rsa_keypair_id const & ident,
                    keypair & kp);
  bool maybe_get_key_pair(rsa_keypair_id const & ident,
                          keypair & kp);
  bool maybe_get_key_pair(id const & hash,
                          rsa_keypair_id & ident,
                          keypair & kp);

  bool put_key_pair(rsa_keypair_id const & ident,
                    keypair const & kp);

  // just like put_key_pair except that the key is _not_ written to disk.
  // primarily for internal use in reading keys back from disk.
  bool put_key_pair_memory(rsa_keypair_id const & ident,
                           keypair const & kp);
                         

  void delete_key(rsa_keypair_id const & ident);

  // These are used to cache signers/verifiers (if the hook allows).
  // They can't be function-static variables in key.cc, since they
  // must be destroyed before the Botan deinitialize() function is
  // called.

  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Signer>,
        boost::shared_ptr<Botan::RSA_PrivateKey> > > signers;

  // FIXME: quick hack to make these hooks and options available via
  //        the key_store context
  bool hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase);
  bool hook_persist_phrase_ok();
  bool hook_get_current_branch_key(rsa_keypair_id & k);
  bool has_opt_signing_key();
  rsa_keypair_id get_opt_signing_key();
  std::string const & get_opt_ssh_sign();
  ssh_agent & get_agent();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
