// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string.h>
#include <errno.h>

#include "unix/unix.hh"
#include "transforms.hh"

utf8
last_error()
{
  external msg = std::string(strerror(errno));
  utf8 out;
  system_to_utf8(msg, out);
  return out;
}
