// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/stat.h>
#include <windows.h>

#include "botan/botan.h"
#include "botan/sha160.h"

#include "platform.hh"
#include "transforms.hh"
#include "file_io.hh"

namespace
{
  template <typename T> void
  inline add_hash(Botan::SHA_160 & hash, T obj)
  {
      size_t size = sizeof(obj);
      hash.update(reinterpret_cast<Botan::byte const *>(&size),
                  sizeof(size));
      hash.update(reinterpret_cast<Botan::byte const *>(&obj),
                  sizeof(obj));
  }
};

bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  struct _stati64 st;
  if (_stati64(localized_as_string(file).c_str(), &st) < 0)
    return false;

  Botan::SHA_160 hash;

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

  add_hash(hash, create.dwLowDateTime);
  add_hash(hash, create.dwHighDateTime);
  add_hash(hash, write.dwLowDateTime);
  add_hash(hash, write.dwHighDateTime);

  if (CloseHandle(filehandle) == 0)
    return false;

  char digest[hash.OUTPUT_LENGTH];
  hash.final(reinterpret_cast<Botan::byte *>(digest));
  std::string out(digest, hash.OUTPUT_LENGTH);
  inodeprint ip_raw(out);
  encode_hexenc(ip_raw, ip);
  return true;
}
