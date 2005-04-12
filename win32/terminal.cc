// copyright (C) 2005 derek scherger <derek@echologic.com>
// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "platform.hh"

bool have_smart_terminal()
{
  return true;
}

unsigned int terminal_width()
{
  // apparently there is no such thing as a non-80-character win32 terminal.
  return 80;
}
