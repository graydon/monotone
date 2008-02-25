#include "base.hh"
#include <sstream>

#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "database.hh"
#include "keys.hh"
#include "globish.hh"
#include "app_state.hh"
#include "transforms.hh"
#include "constants.hh"
#include "ssh_agent.hh"
#include "safe_map.hh"

#include "botan/botan.h"
#include "botan/rsa.h"
#include "botan/keypair.h"
#include "botan/pem.h"

using std::make_pair;
using std::istringstream;
using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::shared_dynamic_cast;

using Botan::RSA_PrivateKey;
using Botan::RSA_PublicKey;
using Botan::SecureVector;
using Botan::X509_PublicKey;
using Botan::PKCS8_PrivateKey;
using Botan::PK_Decryptor;
using Botan::PK_Signer;
using Botan::Pipe;

struct key_store_state
{
  system_path const key_dir;
  string const ssh_sign_mode;
  bool have_read;
  lua_hooks & lua;
  map<rsa_keypair_id, keypair> keys;
  map<id, rsa_keypair_id> hashes;

  // These are used to cache keys and signers (if the hook allows).
  map<rsa_keypair_id, shared_ptr<RSA_PrivateKey> > privkey_cache;
  map<rsa_keypair_id, shared_ptr<PK_Signer> > signer_cache;

  // Initialized when first required.
  scoped_ptr<ssh_agent> agent;

  key_store_state(app_state & app)
    : key_dir(app.opts.key_dir), ssh_sign_mode(app.opts.ssh_sign),
      have_read(false), lua(app.lua)
  {}

  // internal methods
  void get_key_file(rsa_keypair_id const & ident, system_path & file);
  void write_key(rsa_keypair_id const & ident, keypair const & kp);
  void maybe_read_key_dir();
  shared_ptr<RSA_PrivateKey> decrypt_private_key(rsa_keypair_id const & id,
                                                 bool force_from_user = false);

  // just like put_key_pair except that the key is _not_ written to disk.
  // for internal use in reading keys back from disk.
  bool put_key_pair_memory(rsa_keypair_id const & ident,
                           keypair const & kp);

  // wrapper around accesses to agent, initializes as needed
  ssh_agent & get_agent()
  {
    if (!agent)
      agent.reset(new ssh_agent);
    return *agent;
  }

  // duplicates of key_store interfaces for use by key_store_state methods
  // and the keyreader.
  bool maybe_get_key_pair(rsa_keypair_id const & ident,
                          keypair & kp);
  bool put_key_pair(rsa_keypair_id const & ident,
                    keypair const & kp);
  void migrate_old_key_pair(rsa_keypair_id const & id,
                            base64<old_arc4_rsa_priv_key> const & old_priv,
                            base64<rsa_pub_key> const & pub);
};

namespace
{
  struct keyreader : public packet_consumer
  {
    key_store_state & kss;

    keyreader(key_store_state & kss): kss(kss) {}
    virtual void consume_file_data(file_id const & ident,
                                   file_data const & dat)
    {E(false, F("Extraneous data in key store."));}
    virtual void consume_file_delta(file_id const & id_old,
                                    file_id const & id_new,
                                    file_delta const & del)
    {E(false, F("Extraneous data in key store."));}

    virtual void consume_revision_data(revision_id const & ident,
                                       revision_data const & dat)
    {E(false, F("Extraneous data in key store."));}
    virtual void consume_revision_cert(revision<cert> const & t)
    {E(false, F("Extraneous data in key store."));}


    virtual void consume_public_key(rsa_keypair_id const & ident,
                                    base64< rsa_pub_key > const & k)
    {E(false, F("Extraneous data in key store."));}

    virtual void consume_key_pair(rsa_keypair_id const & ident,
                                  keypair const & kp)
    {
      L(FL("reading key pair '%s' from key store") % ident);

      E(kss.put_key_pair_memory(ident, kp),
        F("Key store has multiple keys with id '%s'.") % ident);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }

    // for backward compatibility
    virtual void consume_old_private_key(rsa_keypair_id const & ident,
                                         base64<old_arc4_rsa_priv_key> const & k)
    {
      W(F("converting old-format private key '%s'") % ident);

      base64<rsa_pub_key> dummy;
      kss.migrate_old_key_pair(ident, k, dummy);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }
  };
}

key_store::key_store(app_state & a)
  : s(new key_store_state(a))
{}

key_store::~key_store()
{}

system_path const &
key_store::get_key_dir()
{
  return s->key_dir;
}

void
key_store_state::maybe_read_key_dir()
{
  if (have_read)
    return;
  have_read = true;

  vector<path_component> key_files, dirs;
  if (directory_exists(key_dir))
    {
      L(FL("reading key dir '%s'") % key_dir);
      read_directory(key_dir, key_files, dirs);
    }
  else
    {
      L(FL("key dir '%s' does not exist") % key_dir);
      return;
    }

  keyreader kr(*this);
  for (vector<path_component>::const_iterator i = key_files.begin();
       i != key_files.end(); ++i)
    {
      L(FL("reading keys from file '%s'") % (*i));
      data dat;
      read_data(key_dir / *i, dat);
      istringstream is(dat());
      read_packets(is, kr);
    }
}

void
key_store::get_key_ids(globish const & pattern,
                       vector<rsa_keypair_id> & priv)
{
  s->maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = s->keys.begin(); i != s->keys.end(); ++i)
    if (pattern.matches((i->first)()))
      priv.push_back(i->first);
}

void
key_store::get_key_ids(vector<rsa_keypair_id> & priv)
{
  s->maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = s->keys.begin(); i != s->keys.end(); ++i)
    priv.push_back(i->first);
}

bool
key_store::key_pair_exists(rsa_keypair_id const & ident)
{
  s->maybe_read_key_dir();
  return s->keys.find(ident) != s->keys.end();
}

bool
key_store_state::maybe_get_key_pair(rsa_keypair_id const & ident,
                                    keypair & kp)
{
  maybe_read_key_dir();
  map<rsa_keypair_id, keypair>::const_iterator i = keys.find(ident);
  if (i == keys.end())
    return false;
  kp = i->second;
  return true;
}

bool
key_store::maybe_get_key_pair(rsa_keypair_id const & ident,
                              keypair & kp)
{
  return s->maybe_get_key_pair(ident, kp);
}

void
key_store::get_key_pair(rsa_keypair_id const & ident,
                        keypair & kp)
{
  bool found = maybe_get_key_pair(ident, kp);
  I(found);
}

bool
key_store::maybe_get_key_pair(id const & hash,
                              rsa_keypair_id & keyid,
                              keypair & kp)
{
  s->maybe_read_key_dir();
  map<id, rsa_keypair_id>::const_iterator hi = s->hashes.find(hash);
  if (hi == s->hashes.end())
    return false;

  map<rsa_keypair_id, keypair>::const_iterator ki = s->keys.find(hi->second);
  if (ki == s->keys.end())
    return false;
  keyid = hi->second;
  kp = ki->second;
  return true;
}

void
key_store_state::get_key_file(rsa_keypair_id const & ident,
                              system_path & file)
{
  // filename is the keypair id, except that some characters can't be put in
  // filenames (especially on windows).
  string leaf = ident();
  for (unsigned int i = 0; i < leaf.size(); ++i)
    if (leaf.at(i) == '+')
      leaf.at(i) = '_';

  file = key_dir / path_component(leaf);
}

void
key_store_state::write_key(rsa_keypair_id const & ident,
                           keypair const & kp)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_key_pair(ident, kp);
  data dat(oss.str());

  system_path file;
  get_key_file(ident, file);

  // Make sure the private key is not readable by anyone other than the user.
  L(FL("writing key '%s' to file '%s' in dir '%s'") % ident % file % key_dir);
  write_data_userprivate(file, dat, key_dir);
}

bool
key_store_state::put_key_pair(rsa_keypair_id const & ident,
                              keypair const & kp)
{
  maybe_read_key_dir();
  bool newkey = put_key_pair_memory(ident, kp);
  if (newkey)
    write_key(ident, kp);
  return newkey;
}

bool
key_store::put_key_pair(rsa_keypair_id const & ident,
                        keypair const & kp)
{
  return s->put_key_pair(ident, kp);
}

bool
key_store_state::put_key_pair_memory(rsa_keypair_id const & ident,
                                     keypair const & kp)
{
  L(FL("putting key pair '%s'") % ident);
  pair<map<rsa_keypair_id, keypair>::iterator, bool> res;
  res = keys.insert(make_pair(ident, kp));
  if (res.second)
    {
      id hash;
      key_hash_code(ident, kp.pub, hash);
      I(hashes.insert(make_pair(hash, ident)).second);
      return true;
    }
  else
    {
      E(keys_match(ident, res.first->second.pub, ident, kp.pub),
        F("Cannot store key '%s': a different key by that name exists.")
          % ident);
      L(FL("skipping existing key pair %s") % ident);
      return false;
    }
}

void
key_store::delete_key(rsa_keypair_id const & ident)
{
  s->maybe_read_key_dir();
  map<rsa_keypair_id, keypair>::iterator i = s->keys.find(ident);
  if (i != s->keys.end())
    {
      id hash;
      key_hash_code(ident, i->second.pub, hash);
      map<id, rsa_keypair_id>::iterator j = s->hashes.find(hash);
      I(j != s->hashes.end());
      s->hashes.erase(j);
      s->keys.erase(i);
      s->signer_cache.erase(ident);
      s->privkey_cache.erase(ident);
    }
  system_path file;
  s->get_key_file(ident, file);
  delete_file(file);
}

//
// Crypto operations
//

shared_ptr<RSA_PrivateKey>
key_store_state::decrypt_private_key(rsa_keypair_id const & id,
                                     bool force_from_user)
{
  rsa_priv_key decoded_key;

  // See if we have this key in the decrypted key cache.
  map<rsa_keypair_id, shared_ptr<RSA_PrivateKey> >::const_iterator
    cpk = privkey_cache.find(id);
  if (cpk != privkey_cache.end())
    return cpk->second;

  keypair kp;
  N(maybe_get_key_pair(id, kp),
    F("no key pair '%s' found in key store '%s'") % id % key_dir);

  L(FL("base64-decoding %d-byte private key") % kp.priv().size());
  decode_base64(kp.priv, decoded_key);

  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  try // with empty passphrase
    {
      Pipe p;
      p.process_msg(decoded_key());
      pkcs8_key.reset(Botan::PKCS8::load_key(p, ""));
    }
  catch (Botan::Exception & e)
    {
      L(FL("failed to load key with no passphrase: %s") % e.what());

      utf8 phrase;
      string lua_phrase;
          // See whether a lua hook will tell us the passphrase.
      if (!force_from_user && lua.hook_get_passphrase(id, lua_phrase))
        phrase = utf8(lua_phrase);
      else
        get_passphrase(phrase, id, false, false);

      int cycles = 1;
      for (;;)
        try
          {
            Pipe p;
            p.process_msg(decoded_key());
            pkcs8_key.reset(Botan::PKCS8::load_key(p, phrase()));
            break;
          }
        catch (Botan::Exception & e)
          {
            L(FL("decrypt_private_key: failure %d to load encrypted key: %s")
              % cycles % e.what());
            E(cycles <= 3,
              F("failed to decrypt old private RSA key, "
                "probably incorrect passphrase"));

            get_passphrase(phrase, id, false, false);
            cycles++;
            continue;
          }
    }

  I(pkcs8_key);

  shared_ptr<RSA_PrivateKey> priv_key;
  priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
  E(priv_key,
    F("failed to extract RSA private key from PKCS#8 keypair"));

  // Cache the decrypted key if we're allowed.
  if (lua.hook_persist_phrase_ok())
    safe_insert(privkey_cache, make_pair(id, priv_key));

  return priv_key;
}

void
key_store::cache_decrypted_key(const rsa_keypair_id & id)
{
  signing_key = id;
  if (s->lua.hook_persist_phrase_ok())
    s->decrypt_private_key(id);
}

void
key_store::create_key_pair(database & db,
                           rsa_keypair_id const & id,
                           utf8 const * maybe_passphrase,
                           id * maybe_pubhash,
                           id * maybe_privhash)
{
  conditional_transaction_guard guard(db);

  bool exists = key_pair_exists(id);
  if (db.database_specified())
    {
      guard.acquire();
      exists = exists || db.public_key_exists(id);
    }
  N(!exists, F("key '%s' already exists") % id);

  utf8 prompted_passphrase;
  if (!maybe_passphrase)
    {
      get_passphrase(prompted_passphrase, id, true, true);
      maybe_passphrase = &prompted_passphrase;
    }

  // okay, now we can create the key
  P(F("generating key-pair '%s'") % id);
  RSA_PrivateKey priv(constants::keylen);

  // serialize and maybe encrypt the private key
  SecureVector<Botan::byte> pubkey, privkey;
  Pipe p;
  p.start_msg();
  if ((*maybe_passphrase)().length())
    Botan::PKCS8::encrypt_key(priv, p,
                              (*maybe_passphrase)(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                              Botan::RAW_BER);
  else
    Botan::PKCS8::encode(priv, p);
  rsa_priv_key raw_priv_key(p.read_all_as_string());

  // serialize the public key
  Pipe p2;
  p2.start_msg();
  Botan::X509::encode(priv, p2, Botan::RAW_BER);
  rsa_pub_key raw_pub_key(p2.read_all_as_string());

  // convert to storage format
  keypair kp;
  encode_base64(raw_priv_key, kp.priv);
  encode_base64(raw_pub_key, kp.pub);
  L(FL("generated %d-byte public key\n"
      "generated %d-byte (encrypted) private key\n")
    % kp.pub().size()
    % kp.priv().size());

  // and save it.
  P(F("storing key-pair '%s' in %s/") % id % get_key_dir());
  put_key_pair(id, kp);

  if (db.database_specified())
    {
      P(F("storing public key '%s' in %s") % id % db.get_filename());
      db.put_key(id, kp.pub);
      guard.commit();
    }

  if (maybe_pubhash)
    key_hash_code(id, kp.pub, *maybe_pubhash);
  if (maybe_privhash)
    key_hash_code(id, kp.priv, *maybe_privhash);
}

void
key_store::change_key_passphrase(rsa_keypair_id const & id)
{
  keypair kp;
  load_key_pair(*this, id, kp);
  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id, true);

  utf8 new_phrase;
  get_passphrase(new_phrase, id, true, false);

  Pipe p;
  p.start_msg();
  Botan::PKCS8::encrypt_key(*priv, p, new_phrase(),
                            "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                            Botan::RAW_BER);
  rsa_priv_key decoded_key = rsa_priv_key(p.read_all_as_string());

  encode_base64(decoded_key, kp.priv);
  delete_key(id);
  put_key_pair(id, kp);
}

void
key_store::decrypt_rsa(rsa_keypair_id const & id,
                       rsa_oaep_sha_data const & ciphertext,
                       string & plaintext)
{
  keypair kp;
  load_key_pair(*this, id, kp);
  shared_ptr<RSA_PrivateKey> priv_key = s->decrypt_private_key(id);

  shared_ptr<PK_Decryptor>
    decryptor(get_pk_decryptor(*priv_key, "EME1(SHA-1)"));

  SecureVector<Botan::byte> plain = decryptor->decrypt(
          reinterpret_cast<Botan::byte const *>(ciphertext().data()),
          ciphertext().size());
  plaintext = string(reinterpret_cast<char const*>(plain.begin()),
                     plain.size());
}

void
key_store::make_signature(database & db,
                          rsa_keypair_id const & id,
                          string const & tosign,
                          base64<rsa_sha1_signature> & signature)
{
  keypair key;
  get_key_pair(id, key);

  // If the database doesn't have this public key, add it now.
  if (!db.public_key_exists(id))
    db.put_key(id, key.pub);

  string sig_string;
  ssh_agent & agent = s->get_agent();

  //sign with ssh-agent (if connected)
  N(agent.connected() || s->ssh_sign_mode != "only",
    F("You have chosen to sign only with ssh-agent but ssh-agent"
      " does not seem to be running."));
  if (s->ssh_sign_mode == "yes"
      || s->ssh_sign_mode == "check"
      || s->ssh_sign_mode == "only")
    {
      if (agent.connected()) {
        //grab the monotone public key as an RSA_PublicKey
        rsa_pub_key pub;
        decode_base64(key.pub, pub);
        SecureVector<Botan::byte> pub_block;
        pub_block.set(reinterpret_cast<Botan::byte const *>(pub().data()),
                      pub().size());
        L(FL("make_signature: building %d-byte pub key") % pub_block.size());
        shared_ptr<X509_PublicKey> x509_key =
          shared_ptr<X509_PublicKey>(Botan::X509::load_key(pub_block));
        shared_ptr<RSA_PublicKey> pub_key = shared_dynamic_cast<RSA_PublicKey>(x509_key);

        if (!pub_key)
          throw informative_failure("Failed to get monotone RSA public key");

        agent.sign_data(*pub_key, tosign, sig_string);
      }
      if (sig_string.length() <= 0)
        L(FL("make_signature: monotone and ssh-agent keys do not match, will"
             " use monotone signing"));
    }

  string ssh_sig = sig_string;

  N(ssh_sig.length() > 0 || s->ssh_sign_mode != "only",
    F("You don't seem to have your monotone key imported "));

  if (ssh_sig.length() <= 0
      || s->ssh_sign_mode == "check"
      || s->ssh_sign_mode == "no")
    {
      SecureVector<Botan::byte> sig;

      // we permit the user to relax security here, by caching a decrypted key
      // (if they permit it) through the life of a program run. this helps when
      // you're making a half-dozen certs during a commit or merge or
      // something.

      bool persist_phrase = (!s->signer_cache.empty()
                             || s->lua.hook_persist_phrase_ok());

      shared_ptr<PK_Signer> signer;
      shared_ptr<RSA_PrivateKey> priv_key;
      if (persist_phrase && s->signer_cache.find(id) != s->signer_cache.end())
        signer = s->signer_cache[id];

      else
        {
          priv_key = s->decrypt_private_key(id);
          if (agent.connected()
              && s->ssh_sign_mode != "only"
              && s->ssh_sign_mode != "no") {
            L(FL("make_signature: adding private key (%s) to ssh-agent") % id());
            agent.add_identity(*priv_key, id());
          }
          signer = shared_ptr<PK_Signer>(get_pk_signer(*priv_key, "EMSA3(SHA-1)"));

          /* If persist_phrase is true, the RSA_PrivateKey object is
             cached in s->active_keys and will survive as long as the
             PK_Signer object does.  */
          if (persist_phrase)
            s->signer_cache.insert(make_pair(id, signer));
        }

      sig = signer->sign_message(reinterpret_cast<Botan::byte const *>(tosign.data()), tosign.size());
      sig_string = string(reinterpret_cast<char const*>(sig.begin()), sig.size());
    }

  if (s->ssh_sign_mode == "check" && ssh_sig.length() > 0)
    {
      E(ssh_sig == sig_string,
        F("make_signature: ssh signature (%i) != monotone signature (%i)\n"
          "ssh signature     : %s\n"
          "monotone signature: %s")
        % ssh_sig.length()
        % sig_string.length()
        % encode_hexenc(ssh_sig)
        % encode_hexenc(sig_string));
      L(FL("make_signature: signatures from ssh-agent and monotone"
           " are the same"));
    }

  L(FL("make_signature: produced %d-byte signature") % sig_string.size());
  encode_base64(rsa_sha1_signature(sig_string), signature);

  cert_status s = db.check_signature(id, tosign, signature);
  I(s != cert_unknown);
  E(s == cert_ok, F("make_signature: signature is not valid"));
}

//
// Interoperation with ssh-agent (see also above)
//

void
key_store::add_key_to_agent(rsa_keypair_id const & id)
{
  ssh_agent & agent = s->get_agent();
  N(agent.connected(),
    F("no ssh-agent is available, cannot add key '%s'") % id);

  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id);
  agent.add_identity(*priv, id());
}

void
key_store::export_key_for_agent(rsa_keypair_id const & id,
                                std::ostream & os)
{
  shared_ptr<RSA_PrivateKey> priv = s->decrypt_private_key(id);
  utf8 new_phrase;
  get_passphrase(new_phrase, id, true, false);

  Pipe p(new Botan::DataSink_Stream(os));
  p.start_msg();
  if (new_phrase().length())
    Botan::PKCS8::encrypt_key(*priv,
                              p,
                              new_phrase(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
  else
    Botan::PKCS8::encode(*priv, p);
  p.end_msg();
}


//
// Migration from old databases
//

void
key_store_state::migrate_old_key_pair
    (rsa_keypair_id const & id,
     base64<old_arc4_rsa_priv_key> const & old_priv,
     base64<rsa_pub_key> const & pub)
{
  keypair kp;
  SecureVector<Botan::byte> arc4_key;
  utf8 phrase;
  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  shared_ptr<RSA_PrivateKey> priv_key;

  // See whether a lua hook will tell us the passphrase.
  string lua_phrase;
  if (lua.hook_get_passphrase(id, lua_phrase))
    phrase = utf8(lua_phrase);
  else
    get_passphrase(phrase, id, false, false);

  int cycles = 1;
  for (;;)
    try
      {
        arc4_key.set(reinterpret_cast<Botan::byte const *>(phrase().data()),
                     phrase().size());

        Pipe arc4_decryptor(new Botan::Base64_Decoder,
                            get_cipher("ARC4", arc4_key, Botan::DECRYPTION));
        arc4_decryptor.process_msg(old_priv());

        // This is necessary because PKCS8::load_key() cannot currently
        // recognize an unencrypted, raw-BER blob as such, but gets it
        // right if it's PEM-coded.
        SecureVector<Botan::byte> arc4_decrypt(arc4_decryptor.read_all());
        Pipe p;
        p.process_msg(Botan::PEM_Code::encode(arc4_decrypt, "PRIVATE KEY"));

        pkcs8_key.reset(Botan::PKCS8::load_key(p));
        break;
      }
    catch (Botan::Exception & e)
      {
        L(FL("migrate_old_key_pair: failure %d to load old private key: %s")
          % cycles % e.what());

        E(cycles <= 3,
          F("failed to decrypt old private RSA key, "
            "probably incorrect passphrase"));

        get_passphrase(phrase, id, false, false);
        cycles++;
        continue;
      }

  priv_key = shared_dynamic_cast<RSA_PrivateKey>(pkcs8_key);
  I(priv_key);

  // now we can write out the new key
  Pipe p;
  p.start_msg();
  Botan::PKCS8::encrypt_key(*priv_key, p, phrase(),
                            "PBE-PKCS5v20(SHA-1,TripleDES/CBC)",
                            Botan::RAW_BER);
  rsa_priv_key raw_priv = rsa_priv_key(p.read_all_as_string());
  encode_base64(raw_priv, kp.priv);

  // also the public key (which is derivable from the private key; asking
  // Botan for the X.509 encoding of the private key implies that we want
  // it to derive and produce the public key)
  Pipe p2;
  p2.start_msg();
  Botan::X509::encode(*priv_key, p2, Botan::RAW_BER);
  rsa_pub_key raw_pub = rsa_pub_key(p2.read_all_as_string());
  encode_base64(raw_pub, kp.pub);

  // if the database had a public key entry for this key, make sure it
  // matches what we derived from the private key entry, but don't abort the
  // whole migration if it doesn't.
  if (!pub().empty() && !keys_match(id, pub, id, kp.pub))
    W(F("public and private keys for %s don't match") % id);

  put_key_pair(id, kp);
}

void
key_store::migrate_old_key_pair
    (rsa_keypair_id const & id,
     base64<old_arc4_rsa_priv_key> const & old_priv,
     base64<rsa_pub_key> const & pub)
{
  s->migrate_old_key_pair(id, old_priv, pub);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
