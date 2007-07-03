/* read_password.c: retrieve the password
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

#include "base.hh"
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <windows.h>

#include "sanity.hh"

void 
read_password(std::string const & prompt, char * buf, size_t bufsz)
{
  HANDLE mt_stdin;
  DWORD origmode, pwmode = 0;

  I(buf != NULL);

  mt_stdin = GetStdHandle(STD_INPUT_HANDLE);
  I(mt_stdin != INVALID_HANDLE_VALUE);
  I(mt_stdin != NULL); // NULL is non-interactive.  Can't get a passphrase if we're non-interactive
  if (GetConsoleMode(mt_stdin, &origmode) == 0)
  {
    /* This looks like we're not a real windows console.  
       Possibly MSYS or Cygwin.  We'll do the best we can
       to make the password invisible in the absence of tcsetattr,
       namely emitting vt100 codes to change the foreground and
       background colour to the same thing.  If someone knows a
       better way, be my guest. */
    mt_stdin = NULL;
  }
  else
    pwmode = origmode & (~ENABLE_ECHO_INPUT);

  memset(buf, 0, bufsz);
  std::cout << prompt;
  std::cout.flush();

  if (mt_stdin != NULL)
  {
    I(SetConsoleMode(mt_stdin, pwmode) != 0);
    std::cin.getline(buf, bufsz, '\n');
    std::cout << std::endl;
    I(SetConsoleMode(mt_stdin, origmode) != 0);
  }
  else
  {
    std::cout << "\x1B\x37\x1B[30;40m";
    std::cout.flush();
    fgets(buf, bufsz, stdin); /* Sorry, but cin.getline just doesn't work under MinGW's rxvt */
    std::cout << "\x1B[0m\x1B\x38\n";
    std::cout.flush();

    /* ...and fgets gives us an LF we don't want */
    size_t bufend = strlen(buf)-1;
    if (buf[bufend]=='\x0A')
      buf[bufend] = '\0';
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
