// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <io.h>
#include <errno.h>

#include "sanity.hh"
#include "platform.hh"

std::string get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % strerror(errno));
  return std::string(buffer);
}
  
void change_current_working_dir(std::string const & to)
{
  E(!chdir(to.c_str()),
    F("cannot change to directory %s: %s") % to % strerror(errno));
}
