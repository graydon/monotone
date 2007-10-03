// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// Currently, this file just contains unit tests for widen<>.

#ifdef BUILD_UNIT_TESTS

#include "base.hh"
#include "unit_tests.hh"
#include "sanity.hh"
#include "numeric_vocab.hh"

UNIT_TEST(widen, widen)
{
  // These all have double-parens to stop the C preprocessor from becoming
  // confused by the commas in the template arguments.  The static_cast<u8>'s
  // are to shut up compiler warnings.

  // unsigned -> unsigned
  I((widen<u8,u8>(1) == 1));
  I((widen<u8,u8>(255) == 255));
  I((widen<u8,u8>(static_cast<u8>(-1)) == 255));
  I((widen<u32,u8>(1) == 1));
  I((widen<u32,u8>(255) == 255));
  I((widen<u32,u8>(static_cast<u8>(-1)) == 255));
  // unsigned -> signed
  I((widen<s32,u8>(1) == 1));
  I((widen<s32,u8>(255) == 255));
  I((widen<s32,u8>(static_cast<u8>(-1)) == 255));
  // signed -> signed
  I((widen<s32,s8>(1) == 1));
  I((widen<s32,s8>(255) == -1));
  I((widen<s32,s8>(-1) == -1));
  // signed -> unsigned ((critical case!))
  I((widen<u32,s8>(1) == 1));
  I((widen<u32,s8>(255) == 255));
  I((widen<u32,s8>(-1) == 255));
  // contrasts with:
  I((static_cast<u32>(s8(-1)) == u32(4294967295)));
}

#endif // BUILD_UNIT_TESTS


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

