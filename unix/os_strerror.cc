// Copyright (C) 2006  Matthew Gregan <kinetik@.orcon.net.nz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string.h>

#include "sanity.hh"
#include "platform.hh"

std::string
os_strerror(os_err_t errnum)
{
  char* msg = strerror(errnum);
  if (msg == 0)
    return (F("unknown error code %d") % errnum).str();
  return std::string(msg);
}

