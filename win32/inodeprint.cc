// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include <sys/stat.h>
#include <windows.h>

#include "platform.hh"
#include "sanity.hh"

inline double difftime(FILETIME now, FILETIME then)
{
  // 100 ns (1e-7 second) resolution
  double out = now.dwHighDateTime - then.dwHighDateTime;
  out *= (1<<16); // 1<<32 gives a compile warning about
  out *= (1<<16); // shifting by too many bits
  out += (now.dwLowDateTime - then.dwLowDateTime);
  return out * 1e-7;
}

inline bool is_nowish(FILETIME now, FILETIME then)
{
  double diff = difftime(now, then);
  return (diff >= -3 && diff <= 3);
}

inline bool is_future(FILETIME now, FILETIME then)
{
  double diff = difftime(now, then);
  return (diff < 0);
}


bool inodeprint_file(std::string const & file, inodeprint_calculator & calc)
{
  struct _stati64 st;
  if (_stati64(file.c_str(), &st) < 0)
    return false;

  FILETIME now;
  {
    SYSTEMTIME now_sys;
    GetSystemTime(&now_sys);
    SystemTimeToFileTime(&now_sys, &now);
  }

  calc.add_item(st.st_mode);
  calc.add_item(st.st_dev);
  calc.add_item(st.st_size);

  HANDLE filehandle = CreateFile(file.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (filehandle == INVALID_HANDLE_VALUE)
    return false;

  FILETIME create,write;
  if (GetFileTime(filehandle, &create, NULL, &write) == 0)
    {
      CloseHandle(filehandle);
      return false;
    }

  calc.note_nowish(is_nowish(now, create));
  calc.note_nowish(is_nowish(now, write));
  calc.note_future(is_future(now, create));
  calc.note_future(is_future(now, write));
  calc.add_item(create.dwLowDateTime);
  calc.add_item(create.dwHighDateTime);
  calc.add_item(write.dwLowDateTime);
  calc.add_item(write.dwHighDateTime);

  if (CloseHandle(filehandle) == 0)
    return false;

  return true;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
