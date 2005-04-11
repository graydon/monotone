// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/stat.h>

#include "cryptopp/sha.h"

#include "platform.hh"
#include "transforms.hh"

namespace
{
  template <typename T> void
  add_hash(CryptoPP::SHA & hash, T obj)
  {
      // FIXME: this is not endian safe, which will cause problems
      // if the inodeprint listing is shared between machines of
      // different types (over NFS etc).
      size_t size = sizeof(obj);
      hash.Update(reinterpret_cast<byte const *>(&size),
                  sizeof(size));
      hash.Update(reinterpret_cast<byte const *>(&obj),
                  sizeof(obj));
  }
};

bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  struct stat st;
  if (stat(file().c_str(), &st) < 0)
    return false;

  CryptoPP::SHA hash;

  add_hash(hash, st.st_ctime);

  // aah, portability.
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
  add_hash(hash, st.st_ctim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC)
  add_hash(hash, st.st_ctimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
  add_hash(hash, st.st_ctimensec);
#else
  add_hash(hash, (long)0);
#endif

  add_hash(hash, st.st_mtime);

#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
  add_hash(hash, st.st_mtim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
  add_hash(hash, st.st_mtimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
  add_hash(hash, st.st_mtimensec);
#else
  add_hash(hash, (long)0);
#endif

  add_hash(hash, st.st_mode);
  add_hash(hash, st.st_ino);
  add_hash(hash, st.st_dev);
  add_hash(hash, st.st_uid);
  add_hash(hash, st.st_gid);
  add_hash(hash, st.st_size);

  char digest[CryptoPP::SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  std::string out(digest, CryptoPP::SHA::DIGESTSIZE);
  inodeprint ip_raw(out);
  encode_hexenc(ip_raw, ip);
  return true;
}
