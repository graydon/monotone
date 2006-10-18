#ifndef __SHA1_HH__
#define __SHA1_HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file defines an interface to hook our own (hopefully optimized)
// implementations of SHA-1 into Botan.

void hook_botan_sha1();

#endif // header guard
