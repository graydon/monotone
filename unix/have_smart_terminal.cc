// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdlib.h>
#include <unistd.h>

#include <string>

bool have_smart_terminal()
{
  std::string term = getenv("TERM");

  if (term == "emacs" || term == "dumb" || !isatty(2)) return false;
  return true;
}
