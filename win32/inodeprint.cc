// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/stat.h>
#include <windows.h>

#include "cryptopp/sha.h"

#include "platform.hh"
#include "transforms.hh"
#include "file_io.hh"

namespace
{
  template <typename T> void
  inline add_hash(CryptoPP::SHA & hash, T obj)
  {
      size_t size = sizeof(obj);
      hash.Update(reinterpret_cast<byte const *>(&size),
                  sizeof(size));
      hash.Update(reinterpret_cast<byte const *>(&obj),
                  sizeof(obj));
  }
};

bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  struct _stati64 st;
  if (_stati64(localized(file).native_file_string().c_str(), &st) < 0)
    return false;

  CryptoPP::SHA hash;

  add_hash(hash, st.st_mode);
  add_hash(hash, st.st_dev);
  add_hash(hash, st.st_size);

  HANDLE filehandle = CreateFile(file().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (filehandle == INVALID_HANDLE_VALUE)
    return false;

  FILETIME create,write;
  if (GetFileTime(filehandle, &create, NULL, &write) == 0)
    {
      CloseHandle(filehandle);
      return false;
    }

  add_hash(hash, create);
  add_hash(hash, write);

  if (CloseHandle(filehandle) == 0)
    return false;

  char digest[CryptoPP::SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  std::string out(digest, CryptoPP::SHA::DIGESTSIZE);
  inodeprint ip_raw(out);
  encode_hexenc(ip_raw, ip);
  return true;
}
