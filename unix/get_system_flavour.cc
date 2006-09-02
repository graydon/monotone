// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/utsname.h>
#include "sanity.hh"

void get_system_flavour(std::string & ident)
{
  struct utsname n;
  /* Solaris has >= 0 as success, while
     Linux only knows 0 - as >0 is not an
     error condition there, relax a bit */ 
  I(uname(&n) >= 0);
  ident = (FL("%s %s %s %s")
           % n.sysname
           % n.release
           % n.version
           % n.machine).str();
}
