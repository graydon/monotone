/* read_password.c: retrieve the password
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

#include <unistd.h>
#include <string.h>
#include <iostream>
#include <string>

#include "sanity.hh"

void 
read_password(std::string const & prompt, char * buf, size_t bufsz)
{
  I(buf != NULL);
  memset(buf, 0, bufsz);
  std::cout << prompt;
  std::cout.flush();
  std::cin.getline(buf, bufsz, '\n');
}

