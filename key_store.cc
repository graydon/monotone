#include <sstream>

#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "keys.hh"
#include "globish.hh"

struct keyreader : public packet_consumer
{
  key_store * ks;

  keyreader(key_store * k): ks(k) {}
  virtual void consume_file_data(file_id const & ident, 
                                 file_data const & dat)
  {E(false, F("Extraneous data in key store."));}
  virtual void consume_file_delta(file_id const & id_old, 
                                  file_id const & id_new,
                                  file_delta const & del)
  {E(false, F("Extraneous data in key store."));}
  virtual void consume_file_reverse_delta(file_id const & id_new,
                                          file_id const & id_old,
                                          file_delta const & del)
  {E(false, F("Extraneous data in key store."));}
  

  virtual void consume_manifest_data(manifest_id const & ident, 
                                     manifest_data const & dat)
  {E(false, F("Extraneous data in key store."));}
  virtual void consume_manifest_delta(manifest_id const & id_old, 
                                      manifest_id const & id_new,
                                      manifest_delta const & del)
  {E(false, F("Extraneous data in key store."));}
  virtual void consume_manifest_reverse_delta(manifest_id const & id_new,
                                              manifest_id const & id_old,
                                              manifest_delta const & del)
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
    E(!ks->key_pair_exists(ident),
      F("Key store has multiple keys with id '%s'.") % ident);
    ks->keys.insert(std::make_pair(ident, kp));
    hexenc<id> hash;
    key_hash_code(ident, kp.pub, hash);
    ks->hashes.insert(std::make_pair(hash, ident));
    L(F("Read key pair '%s' from key store.") % ident);
  } 
};

key_store::key_store(app_state * a): have_read(false), app(a)
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
  std::vector<utf8> key_files, dirs;
  if (directory_exists(key_dir))
    read_directory(key_dir, key_files, dirs);
  keyreader kr(this);
  for (std::vector<utf8>::const_iterator i = key_files.begin();
       i != key_files.end(); ++i)
    {
      data dat;
      read_data(key_dir / (*i)(), dat);
      std::istringstream is(dat());
      read_packets(is, kr, *app);
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
key_store::ensure_in_database(rsa_keypair_id const & ident)
{
  maybe_read_key_dir();
  if (app->db.public_key_exists(ident))
    return;
  std::map<rsa_keypair_id, keypair>::iterator i = keys.find(ident);
  I(i != keys.end());
  app->db.put_key(ident, i->second.pub);
}

bool
key_store::try_ensure_in_db(hexenc<id> const & hash)
{
  std::map<hexenc<id>, rsa_keypair_id>::const_iterator i = hashes.find(hash);
  if (i == hashes.end())
    return false;
  ensure_in_database(i->second);
  return true;
}

void
key_store::get_key_ids(std::string const & pattern,
                   std::vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  utf8 inc(pattern);
  if (pattern.empty())
    inc = utf8("*");
  globish_matcher gm(inc, utf8(""));
  for (std::map<rsa_keypair_id, keypair>::const_iterator
         i = keys.begin(); i != keys.end(); ++i)
    {
      if (gm((i->first)()))
        priv.push_back(i->first);
    }
}

void
key_store::get_keys(std::vector<rsa_keypair_id> & priv)
{
  maybe_read_key_dir();
  priv.clear();
  for (std::map<rsa_keypair_id, keypair>::const_iterator
         i = keys.begin(); i != keys.end(); ++i)
    {
      priv.push_back(i->first);
    }
}

bool
key_store::key_pair_exists(rsa_keypair_id const & ident)
{
  maybe_read_key_dir();
  return keys.find(ident) != keys.end();
}

void
key_store::get_key_pair(rsa_keypair_id const & ident,
                        keypair & kp)
{
  maybe_read_key_dir();
  std::map<rsa_keypair_id, keypair>::const_iterator i = keys.find(ident);
  I(i != keys.end());
  kp = i->second;
}

namespace
{
  // filename is the keypair id, except that some characters can't be put in
  // filenames (especially on windows).
  void
  get_filename(rsa_keypair_id const & ident, std::string & filename)
  {
    filename = ident();
    for (unsigned int i = 0; i < filename.size(); ++i)
      if (std::string("+").find(filename[i]) != std::string::npos)
        filename.at(i) = '_';
  }
}

void
key_store::get_key_file(rsa_keypair_id const & ident,
                 system_path & file)
{
  std::string leaf;
  get_filename(ident, leaf);
  file = key_dir /  leaf;
}

void
key_store::write_key(rsa_keypair_id const & ident)
{
  keypair kp;
  get_key_pair(ident, kp);
  std::ostringstream oss;
  packet_writer pw(oss);
  pw.consume_key_pair(ident, kp);
  data dat(oss.str());
  system_path file;
  get_key_file(ident, file);
  write_data(file, dat, key_dir);
}

void
key_store::put_key_pair(rsa_keypair_id const & ident, 
                        keypair const & kp)
{
  maybe_read_key_dir();
  L(F("putting key pair '%s'") % ident);
  std::pair<std::map<rsa_keypair_id, keypair>::iterator, bool> res;
  res = keys.insert(std::make_pair(ident, kp));
  if (res.second)
    {
      hexenc<id> hash;
      key_hash_code(ident, kp.pub, hash);
      I(hashes.insert(std::make_pair(hash, ident)).second);
      write_key(ident);
    }
  else
    {
      E(/*keys_match(ident, res.first->second.priv, ident, kp.priv)
        && */keys_match(ident, res.first->second.pub, ident, kp.pub),
        F("Cannot store key '%s'; a different key by that name exists.")
          % ident);
    }
}

void
key_store::delete_key(rsa_keypair_id const & ident)
{
  maybe_read_key_dir();
  std::map<rsa_keypair_id, keypair>::iterator i = keys.find(ident);
  if (i != keys.end())
    {
      hexenc<id> hash;
      key_hash_code(ident, i->second.pub, hash);
      std::map<hexenc<id>, rsa_keypair_id>::iterator j = hashes.find(hash);
      I(j != hashes.end());
      hashes.erase(j);
      keys.erase(i);
    }
  system_path file;
  get_key_file(ident, file);
  delete_file(file);
}
