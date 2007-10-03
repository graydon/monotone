// copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "numeric_vocab.hh"

u64 to_ticks(FILETIME const & ft)
{
  u64 ticks = ft.dwHighDateTime;
  ticks <<= 32;
  ticks += ft.dwLowDateTime;
  return ticks;
}

double
cpu_now()
{
  FILETIME creation_time, exit_time, kernel_time, user_time;
  if (GetProcessTimes(GetCurrentProcess(),
                      &creation_time, &exit_time,
                      &kernel_time, &user_time) == 0)
    return -1;

  u64 total_ticks = 0;
  total_ticks += to_ticks(kernel_time);
  total_ticks += to_ticks(user_time);
  // 1 tick is 100 ns = 1e-7 seconds
  return total_ticks * 1e-7;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
