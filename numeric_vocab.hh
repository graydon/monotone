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

#include <limits>

#include "mt-stdint.h"
#include <boost/static_assert.hpp>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

BOOST_STATIC_ASSERT(sizeof(char) == 1);
BOOST_STATIC_ASSERT(CHAR_BIT == 8);

template <typename T, typename V>
inline T
widen(V const & v)
{
  BOOST_STATIC_ASSERT(sizeof(T) >= sizeof(V));
  if (std::numeric_limits<T>::is_signed)
    return static_cast<T>(v);
  else
    {
      T mask = std::numeric_limits<T>::max();
      size_t shift = (sizeof(T) - sizeof(V)) * 8;
      mask >>= shift;
      return static_cast<T>(v) & mask;
    }
}

#endif
