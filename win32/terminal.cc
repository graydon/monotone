// copyright (C) 2005 derek scherger <derek@echologic.com>
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <windows.h>
#include <io.h>

#include <string>

#include "platform.hh"

bool have_smart_terminal()
{
  std::string term;
  if (const char* term_cstr = getenv("TERM"))
    term = term_cstr;
  else
    term = "";

  if (term == "" || term == "emacs" || term == "dumb"
      || !_isatty(_fileno(stderr)))
    return false;
  else
    return true;
}

unsigned int terminal_width()
{
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE)
    {
      CONSOLE_SCREEN_BUFFER_INFO ci;
      if (GetConsoleScreenBufferInfo(h, &ci) != 0)
        {
          return static_cast<unsigned int>(ci.dwSize.X);
        }
    }
  
  // default to 80 columns if the width query failed.
  return 80;
}
