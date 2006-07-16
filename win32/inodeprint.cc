// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/stat.h>
#include <windows.h>

#include "platform.hh"
#include "sanity.hh"

inline double difftime(FILETIME now, FILETIME then)
{
  ULONG_INTEGER here, there, res;
  // Yes, MSDN says to use memcpy for this.
  memcpy(&here, &now, sizeof(here));
  memcpy(&there, &then, sizeof(there));
  res = here - there;
  double out = res.HighPart * (1<<32) + res.LowPart;
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
