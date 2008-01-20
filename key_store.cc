#include "base.hh"
#include <sstream>

#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "keys.hh"
#include "globish.hh"
#include "app_state.hh"

using std::make_pair;
using std::istringstream;
using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

namespace
{
  struct keyreader : public packet_consumer
  {
    key_store & ks;

    keyreader(key_store & k): ks(k) {}
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

      E(ks.put_key_pair_memory(ident, kp),
        F("Key store has multiple keys with id '%s'.") % ident);

      L(FL("successfully read key pair '%s' from key store") % ident);
    }
  };
}

key_store::key_store(app_state & a): have_read(false), app(a)
{
}

void
key_store::set_key_dir(system_path const & kd)
{
  key_dir = kd;
}

system_path const &
key_store::get_key_dir()
{
  return key_dir;
}

void
key_store::read_key_dir()
{
  vector<path_component> key_files, dirs;
  if (directory_exists(key_dir))
    {
      L(FL("reading key dir '%s'") % key_dir);
      read_directory(key_dir, key_files, dirs);
    }
  else
    L(FL("key dir '%s' does not exist") % key_dir);
  keyreader kr(*this);
  for (vector<path_component>::const_iterator i = key_files.begin();
       i != key_files.end(); ++i)
    {
      L(FL("reading keys from file '%s'") % (*i));
      data dat;
      read_data(key_dir / *i, dat);
      istringstream is(dat());
      read_packets(is, kr, *this);
    }
}

void
key_store::maybe_read_key_dir()
{
  if (have_read)
    return;
  have_read = true;
  read_key_dir();
}

void
key_store::get_key_ids(globish const & pattern,
                       vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = keys.begin(); i != keys.end(); ++i)
    if (pattern.matches((i->first)()))
      priv.push_back(i->first);
}

void
key_store::get_key_ids(vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  for (map<rsa_keypair_id, keypair>::const_iterator
         i = keys.begin(); i != keys.end(); ++i)
    priv.push_back(i->first);
}

bool
key_store::key_pair_exists(rsa_keypair_id const & ident)
{
  maybe_read_key_dir();
  return keys.find(ident) != keys.end();
}

bool
key_store::maybe_get_key_pair(rsa_keypair_id const & ident,
                              keypair & kp)
{
  maybe_read_key_dir();
  map<rsa_keypair_id, keypair>::const_iterator i = keys.find(ident);
  if (i == keys.end())
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
  map<hexenc<id>, rsa_keypair_id>::const_iterator hi = hashes.find(hash);
  if (hi == hashes.end())
    return false;

  map<rsa_keypair_id, keypair>::const_iterator ki = keys.find(hi->second);
  if (ki == keys.end())
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

  file = key_dir / path_component(leaf);
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
  L(FL("writing key '%s' to file '%s' in dir '%s'") % ident % file % key_dir);
  write_data_userprivate(file, dat, key_dir);
}

bool
key_store::put_key_pair(rsa_keypair_id const & ident,
                        keypair const & kp)
{
  bool newkey = put_key_pair_memory(ident, kp);
  if (newkey)
    write_key(ident);
  return newkey;
}

bool
key_store::put_key_pair_memory(rsa_keypair_id const & ident,
                               keypair const & kp)
{
  maybe_read_key_dir();
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
  map<rsa_keypair_id, keypair>::iterator i = keys.find(ident);
  if (i != keys.end())
    {
      hexenc<id> hash;
      key_hash_code(ident, i->second.pub, hash);
      map<hexenc<id>, rsa_keypair_id>::iterator j = hashes.find(hash);
      I(j != hashes.end());
      hashes.erase(j);
      keys.erase(i);
    }
  system_path file;
  get_key_file(ident, file);
  delete_file(file);
}

bool
key_store::hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase)
{
  return app.lua.hook_get_passphrase(k, phrase);
}

bool
key_store::hook_persist_phrase_ok()
{
  return app.lua.hook_persist_phrase_ok();
}

bool
key_store::hook_get_current_branch_key(rsa_keypair_id & k)
{
  return app.lua.hook_get_branch_key(app.opts.branchname, k);
}

bool
key_store::has_opt_signing_key()
{
  return (app.opts.signing_key() != "");
}

rsa_keypair_id
key_store::get_opt_signing_key()
{
  return app.opts.signing_key;
}

const string &
key_store::get_opt_ssh_sign()
{
  return app.opts.ssh_sign;
}

ssh_agent &
key_store::get_agent()
{
  return app.agent;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
