// Copyright (C) 2006  Matthew Gregan <kinetik@.orcon.net.nz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#define WIN32_LEAN_AND_MEAN
#include "base.hh"
#include <windows.h>

#include "sanity.hh"
#include "platform.hh"

std::string
os_strerror(os_err_t errnum)
{
  LPTSTR tstr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM;
  DWORD len = FormatMessage(flags, 0, errnum,
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            reinterpret_cast<LPTSTR>(&tstr), 0,
                            static_cast<va_list *>(0));
  if (len == 0)
    return (F("unknown error code %d") % errnum).str();
  std::string errstr = tstr;
  LocalFree(tstr);

  // clobber trailing newlines.
  std::string::size_type end = errstr.find_last_not_of("\r\n");
  if (end != std::string::npos)
    errstr.erase(end + 1);
  return errstr;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
