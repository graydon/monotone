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

#include <string>
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
