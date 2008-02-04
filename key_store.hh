#ifndef __KEY_STORE_H__
#define __KEY_STORE_H__

#include "vector.hh"
#include "vocab.hh"
#include "paths.hh"

class app_state;
struct globish;
class database;

class key_store_state;

class key_store
{
private:
  key_store_state * s;

  void get_key_file(rsa_keypair_id const & ident, system_path & file);
  void write_key(rsa_keypair_id const & ident);
  void read_key_dir();
  void maybe_read_key_dir();

public:
  rsa_keypair_id signing_key;

  key_store(app_state & a);
  ~key_store();

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
  bool maybe_get_key_pair(hexenc<id> const & hash,
                          rsa_keypair_id & ident,
                          keypair & kp);

  bool put_key_pair(rsa_keypair_id const & ident,
                    keypair const & kp);

  void delete_key(rsa_keypair_id const & ident);

  // Crypto operations

  void make_signature(database & db, rsa_keypair_id const & id,
                      std::string const & tosign,
                      base64<rsa_sha1_signature> & signature);

  // Migration from old databases

  void migrate_old_key_pair(rsa_keypair_id const & id,
                            base64<old_arc4_rsa_priv_key> const & old_priv,
                            base64<rsa_pub_key> const & pub);

  // FIXME: quick hack to make these hooks and options available via
  //        the key_store context
  bool hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase);
  bool hook_persist_phrase_ok();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
