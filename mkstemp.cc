
// this is a somewhat more portable mkstemp which uses the system prng to
// seed. from what I've seen on other system mkstemps, they are usually
// *worse* than this (or non-existant).
//
// the source is partially cribbed from gfileutils.c in glib, which is
// copyright (c) 2000 Red Hat. It was released as LGPL, so I have copied
// some of its text into this file and am relicensing my derivative work
// (this file) copyright (C) 2004 graydon hoare, as LGPL also.

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cryptopp/osrng.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

int
monotone_mkstemp(char *tmpl)
{
  int len = 0, i = 0;
  char *XXXXXX = NULL;
  int count = 0, fd = -1;

  static const char letters[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static const int NLETTERS = sizeof (letters) - 1;
  static CryptoPP::AutoSeededRandomPool mkstemp_urandom;

  len = strlen (tmpl);
  if (len < 6 || strcmp (&tmpl[len - 6], "XXXXXX"))
    return -1;

  XXXXXX = &tmpl[len - 6];

  for (count = 0; count < 100; ++count)
    {
      for (i = 0; i < 6; ++i)
	XXXXXX[i] = letters[mkstemp_urandom.GenerateByte() % NLETTERS];
      fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);      
      if (fd >= 0)
	return fd;
      else if (errno != EEXIST)
	break;
    }  
  return -1;
}

