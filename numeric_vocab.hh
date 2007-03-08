#ifndef __NUMERIC_VOCAB__
#define __NUMERIC_VOCAB__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstddef>
#include <limits>

#include "mt-stdint.h"
#include <boost/static_assert.hpp>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

BOOST_STATIC_ASSERT(sizeof(char) == 1);
BOOST_STATIC_ASSERT(CHAR_BIT == 8);
BOOST_STATIC_ASSERT(sizeof(u8) == 1);
BOOST_STATIC_ASSERT(sizeof(u16) == 2);
BOOST_STATIC_ASSERT(sizeof(u32) == 4);
BOOST_STATIC_ASSERT(sizeof(u64) == 8);

// This is similar to static_cast<T>(v).  The difference is that when T is
// unsigned, this cast does not sign-extend:
//   static_cast<u32>((signed char) -1) = 4294967295
//   widen<u32,signed char>(-1) == 255
template <typename T, typename V>
inline T
widen(V const & v)
{
  BOOST_STATIC_ASSERT(sizeof(T) >= sizeof(V));
  if (std::numeric_limits<T>::is_signed)
    return static_cast<T>(v);
  else if (!std::numeric_limits<V>::is_signed)
    return static_cast<T>(v);
  else
    {
      T mask = std::numeric_limits<T>::max();
      size_t shift = (sizeof(T) - sizeof(V)) * 8;
      mask >>= shift;
      return static_cast<T>(v) & mask;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
