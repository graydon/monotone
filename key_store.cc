#include "base.hh"
#include <sstream>

#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "keys.hh"
#include "globish.hh"
#include "app_state.hh"
#include "transforms.hh"
#include "constants.hh"

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

using boost::shared_ptr;
using boost::shared_dynamic_cast;

using Botan::RSA_PrivateKey;
using Botan::RSA_PublicKey;
using Botan::SecureVector;
using Botan::X509_PublicKey;
using Botan::PKCS8_PrivateKey;
using Botan::PK_Signer;
using Botan::Pipe;

class key_store_state
{
  friend class key_store;

  system_path key_dir;
  bool have_read;
  app_state & app;
  map<rsa_keypair_id, keypair> keys;
  map<hexenc<id>, rsa_keypair_id> hashes;

  // These are used to cache signers (if the hook allows).
  map<rsa_keypair_id,
    pair<shared_ptr<PK_Signer>,
         shared_ptr<RSA_PrivateKey> > > signers;

  key_store_state(app_state & app)
    : have_read(false), app(app)
  {}

public:  
  // just like put_key_pair except that the key is _not_ written to disk.
  // primarily for internal use in reading keys back from disk.  public so
  // keyreader (which is in an anonymous namespace and so can't be friended)
  // can get at it.
  bool put_key_pair_memory(rsa_keypair_id const & ident,
                           keypair const & kp);
};

namespace
{
  struct keyreader : public packet_consumer
  {
    key_store & ks;
    key_store_state & kss;

    keyreader(key_store & ks, key_store_state & kss): ks(ks), kss(kss) {}
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
      ks.migrate_old_key_pair(ident, k, dummy);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }
  };
}

key_store::key_store(app_state & a)
  : s(new key_store_state(a))
{
}

key_store::~key_store()
{
  delete s;
}

void
key_store::set_key_dir(system_path const & kd)
{
  s->key_dir = kd;
}

system_path const &
key_store::get_key_dir()
{
  return s->key_dir;
}

void
key_store::read_key_dir()
{
  vector<path_component> key_files, dirs;
  if (directory_exists(s->key_dir))
    {
      L(FL("reading key dir '%s'") % s->key_dir);
      read_directory(s->key_dir, key_files, dirs);
    }
  else
    {
      L(FL("key dir '%s' does not exist") % s->key_dir);
      return;
    }

  keyreader kr(*this, *s);
  for (vector<path_component>::const_iterator i = key_files.begin();
       i != key_files.end(); ++i)
    {
      L(FL("reading keys from file '%s'") % (*i));
      data dat;
      read_data(s->key_dir / *i, dat);
      istringstream is(dat());
      read_packets(is, kr);
    }
}

void
key_store::maybe_read_key_dir()
{
  if (s->have_read)
    return;
  s->have_read = true;
  read_key_dir();
}

void
key_store::get_key_ids(globish const & pattern,
                       vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = s->keys.begin(); i != s->keys.end(); ++i)
    if (pattern.matches((i->first)()))
      priv.push_back(i->first);
}

void
key_store::get_key_ids(vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = s->keys.begin(); i != s->keys.end(); ++i)
    priv.push_back(i->first);
}

bool
key_store::key_pair_exists(rsa_keypair_id const & ident)
{
  maybe_read_key_dir();
  return s->keys.find(ident) != s->keys.end();
}

bool
key_store::maybe_get_key_pair(rsa_keypair_id const & ident,
                              keypair & kp)
{
  maybe_read_key_dir();
  map<rsa_keypair_id, keypair>::const_iterator i = s->keys.find(ident);
  if (i == s->keys.end())
    return false;
  kp = i->second;
  return true;
}

void
key_store::get_key_pair(rsa_keypair_id const & ident,
                        keypair & kp)
{
  bool found = maybe_get_key_pair(ident, kp);
  I(found);
}

bool
key_store::maybe_get_key_pair(hexenc<id> const & hash,
                              rsa_keypair_id & keyid,
                              keypair & kp)
{
  maybe_read_key_dir();
  map<hexenc<id>, rsa_keypair_id>::const_iterator hi = s->hashes.find(hash);
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
key_store::get_key_file(rsa_keypair_id const & ident,
                        system_path & file)
{
  // filename is the keypair id, except that some characters can't be put in
  // filenames (especially on windows).
  string leaf = ident();
  for (unsigned int i = 0; i < leaf.size(); ++i)
    if (leaf.at(i) == '+')
      leaf.at(i) = '_';

  file = s->key_dir / path_component(leaf);
}

void
key_store::write_key(rsa_keypair_id const & ident)
{
  keypair kp;
  get_key_pair(ident, kp);
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_key_pair(ident, kp);
  data dat(oss.str());
  system_path file;
  get_key_file(ident, file);

  // Make sure the private key is not readable by anyone other than the user.
  L(FL("writing key '%s' to file '%s' in dir '%s'") % ident % file % s->key_dir);
  write_data_userprivate(file, dat, s->key_dir);
}

bool
key_store::put_key_pair(rsa_keypair_id const & ident,
                        keypair const & kp)
{
  maybe_read_key_dir();
  bool newkey = s->put_key_pair_memory(ident, kp);
  if (newkey)
    write_key(ident);
  return newkey;
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
      hexenc<id> hash;
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
  maybe_read_key_dir();
  map<rsa_keypair_id, keypair>::iterator i = s->keys.find(ident);
  if (i != s->keys.end())
    {
      hexenc<id> hash;
      key_hash_code(ident, i->second.pub, hash);
      map<hexenc<id>, rsa_keypair_id>::iterator j = s->hashes.find(hash);
      I(j != s->hashes.end());
      s->hashes.erase(j);
      s->keys.erase(i);
    }
  system_path file;
  get_key_file(ident, file);
  delete_file(file);
}

//
// Crypto operations
//

void
key_store::create_key_pair(database & db,
                           rsa_keypair_id const & id,
                           utf8 const * maybe_passphrase,
                           hexenc<id> * maybe_pubhash,
                           hexenc<id> * maybe_privhash)
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
key_store::make_signature(database & db,
                          rsa_keypair_id const & id,
                          string const & tosign,
                          base64<rsa_sha1_signature> & signature)
{
  const string & opt_ssh_sign = s->app.opts.ssh_sign;

  keypair key;
  get_key_pair(id, key);

  // If the database doesn't have this public key, add it now.
  if (!db.public_key_exists(id))
    db.put_key(id, key.pub);

  string sig_string;
  ssh_agent & agent = s->app.agent;

  //sign with ssh-agent (if connected)
  N(agent.connected() || opt_ssh_sign != "only",
    F("You have chosen to sign only with ssh-agent but ssh-agent"
      " does not seem to be running."));
  if (opt_ssh_sign == "yes"
      || opt_ssh_sign == "check"
      || opt_ssh_sign == "only")
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

  N(ssh_sig.length() > 0 || opt_ssh_sign != "only",
    F("You don't seem to have your monotone key imported "));

  if (ssh_sig.length() <= 0
      || opt_ssh_sign == "check"
      || opt_ssh_sign == "no")
    {
      SecureVector<Botan::byte> sig;

      // we permit the user to relax security here, by caching a decrypted key
      // (if they permit it) through the life of a program run. this helps when
      // you're making a half-dozen certs during a commit or merge or
      // something.

      bool persist_phrase = !s->signers.empty() || hook_persist_phrase_ok();

      shared_ptr<PK_Signer> signer;
      shared_ptr<RSA_PrivateKey> priv_key;
      if (persist_phrase && s->signers.find(id) != s->signers.end())
        signer = s->signers[id].first;

      else
        {
          priv_key = get_private_key(*this, id, key.priv);
          if (agent.connected()
              && opt_ssh_sign != "only"
              && opt_ssh_sign != "no") {
            L(FL("make_signature: adding private key (%s) to ssh-agent") % id());
            agent.add_identity(*priv_key, id());
          }
          signer = shared_ptr<PK_Signer>(get_pk_signer(*priv_key, "EMSA3(SHA-1)"));

          /* XXX This is ugly. We need to keep the key around as long
           * as the signer is around, but the shared_ptr for the key will go
           * away after we leave this scope. Hence we store a pair of
           * <verifier,key> so they both exist. */
          if (persist_phrase)
            s->signers.insert(make_pair(id,make_pair(signer,priv_key)));
        }

      sig = signer->sign_message(reinterpret_cast<Botan::byte const *>(tosign.data()), tosign.size());
      sig_string = string(reinterpret_cast<char const*>(sig.begin()), sig.size());
    }

  if (opt_ssh_sign == "check" && ssh_sig.length() > 0)
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
// Migration from old databases
//

void
key_store::migrate_old_key_pair(rsa_keypair_id const & id,
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
  if (s->app.lua.hook_get_passphrase(id, lua_phrase))
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

//
// Hooks into the application configuration
//

bool
key_store::hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase)
{
  return s->app.lua.hook_get_passphrase(k, phrase);
}

bool
key_store::hook_persist_phrase_ok()
{
  return s->app.lua.hook_persist_phrase_ok();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
