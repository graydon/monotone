// Copyright (C) 2006  Matthew Gregan <kinetik@.orcon.net.nz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
