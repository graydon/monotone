#ifndef __NUMERIC_VOCAB__
#define __NUMERIC_VOCAB__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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
