
// this is a somewhat more portable mkstemp which uses the system prng to
// seed. from what I've seen on other system mkstemps, they are usually
// *worse* than this (or non-existant).
//
// the source is partially cribbed from gfileutils.c in glib, which is
// copyright (c) 2000 Red Hat. It was released as LGPL, so I have copied
// some of its text into this file and am relicensing my derivative work
// (this file) copyright (C) 2004 graydon hoare, as LGPL also.

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <boost/filesystem/path.hpp>

#include "file_io.hh"
#include "botan/botan.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

int
monotone_mkstemp(std::string &tmpl)
{
  unsigned int len = 0;
  int i = 0;
  int count = 0, fd = -1;
  std::string tmp;
  fs::path path;

  static const char letters[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static const int NLETTERS = sizeof (letters) - 1;

  len = tmpl.length();
  if (len < 6 || tmpl.rfind("XXXXXX") != len-6)
    return -1;

  for (count = 0; count < 100; ++count)
    {
      tmp = tmpl.substr(0, len-6);

      for (i = 0; i < 6; ++i)
        tmp.append(1, letters[Botan::Global_RNG::random(Botan::Nonce) % NLETTERS]);
      fd = open(tmp.c_str(), O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);      
      if (fd >= 0)
      {
        fs::path path;
        path = mkpath(tmp);
        tmpl = path.native_directory_string();
        return fd;
      }
      else if (errno != EEXIST)
        break;
    }  
  return -1;
}

