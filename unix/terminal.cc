// copyright (C) 2005 derek scherger <derek@echologic.com>
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

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
      || !isatty(2))
    return false;
  else
    return true;
}

unsigned int terminal_width()
{
  struct winsize ws;
  int ret = ioctl(2, TIOCGWINSZ, &ws);
  if (ret < 0)
    {
      // FIXME: it would be nice to log something here
      // but we are called by the tick printing code, and trying to print
      // things while in the middle of printing a tick line is a great way to
      // break things.
      return 0;
    }
  return ws.ws_col;
}
