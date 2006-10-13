// copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <time.h>

double
cpu_now()
{
  return static_cast<double>(clock()) / CLOCKS_PER_SEC;
}
