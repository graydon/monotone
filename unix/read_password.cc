/* read_password.c: retrieve the password
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

#include "base.hh"
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <iostream>

#include "sanity.hh"

static void 
echo_on(struct termios & save_term)
{
   tcsetattr(0, TCSANOW, &save_term);
}

static void 
echo_off(struct termios & save_term) 
{
  struct termios temp;
  tcgetattr(0,&save_term);
  temp=save_term;
  temp.c_lflag &= ~(ECHO | ECHOE | ECHOK);
  tcsetattr(0, TCSANOW, &temp);
}

void 
read_password(std::string const & prompt, char * buf, size_t bufsz)
{
  struct termios save_term;  
  I(buf != NULL);
  memset(buf, 0, bufsz);
  std::cout << prompt;
  std::cout.flush();
  echo_off(save_term);
  std::cin.getline(buf, bufsz, '\n');
  std::cout << std::endl;
  echo_on(save_term);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
