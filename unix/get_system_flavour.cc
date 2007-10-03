// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include <sys/utsname.h>
#include "sanity.hh"
#include <ostream> // for operator<<

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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
