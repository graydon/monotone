// copyright (C) 2005 derek scherger <derek@echologic.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdlib.h>
#include <unistd.h>

#include <string>

bool have_smart_terminal()
{
  std::string term;
  if (char* term_cstr = getenv("TERM"))
    term = term_cstr;
  else
    term = "";

  if (term == "" || term == "emacs" || term == "dumb"
      || !isatty(2))
    return false;
  else
    return true;
}
