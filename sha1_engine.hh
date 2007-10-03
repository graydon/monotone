#ifndef __SHA1_ENGINE_HH__
#define __SHA1_ENGINE_HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file defines the interface needed to implement a new SHA-1 engine,
// which we will hook into Botan.

#include <botan/base.h>

typedef Botan::HashFunction * sha1_maker();

// Declare one of these objects as a private global in your extension module.
// Note that all priorities must be distinct.  Higher priority means faster
// code.  Botan's built-in SHA-1 is always priority 0.
struct sha1_registerer
{
  sha1_registerer(int priority, std::string const & name, sha1_maker * maker);
};

#endif // header guard


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

