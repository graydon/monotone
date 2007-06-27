
// this is a somewhat more portable mkstemp which uses the system prng to
// seed. from what I've seen on other system mkstemps, they are usually
// *worse* than this (or non-existant).
//
// the source is partially cribbed from gfileutils.c in glib, which is
// copyright (c) 2000 Red Hat. It was released as LGPL, so I have copied
// some of its text into this file and am relicensing my derivative work
// (this file) copyright (C) 2004 graydon hoare, as LGPL also.


#include "base.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "botan/botan.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

using std::string;

bool
monotone_mkstemp(string &tmpl)
{
  unsigned int len = 0;
  int i = 0;
  int count = 0, fd = -1;
  string tmp;

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
        tmp.append(1, letters[Botan::Global_RNG::random() % NLETTERS]);
#ifdef _MSC_VER
      fd = _open(tmp.c_str(), _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, 0600);
#else
	  fd = open(tmp.c_str(), O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
#endif
	  if (fd >= 0)
      {
        tmpl = tmp;
#ifdef _MSC_VER
        _close(fd);
#else
	      close(fd);
#endif
        return true;
      }
      else if (errno != EEXIST)
        break;
    }
  return false;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
