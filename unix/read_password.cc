/* read_password.c: retrieve the password
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <iostream>
#include <string>

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
  echo_on(save_term);
}

