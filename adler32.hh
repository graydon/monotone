#ifndef __ADLER32_HH__
#define __ADLER32_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this is a pseudo-adler32. it does not use a prime modulus. it is not
// entirely clear that this matters; it is what rsync and xdelta both do
// and it seems to work.

#include <string>
#include <boost/cstdint.hpp>

typedef boost::uint32_t u32;

struct adler32
{
  u32 s1, s2, len;
  
  inline u32 sum() const
  {
    return (s2 << 16) | s1;
  }

  inline void in(char c)
  {
    s1 += static_cast<u32>(c);
    s1 &= 0xffff;
    s2 += s1;
    s2 &= 0xffff;
    ++len;
  }

  inline void out(char c)
  {
    s1 -= static_cast<u32>(c);
    s1 &= 0xffff;
    s2 -= (len * static_cast<u32>(c)) + 1;
    s2 &= 0xffff;
    --len;
  }

  adler32() : s1(1), s2(0), len(0) {}
  adler32(char const * ch, std::string::size_type count)
    : s1(1), s2(0), len(0)
  {
    while(count--)
      in(*(ch++));
  }
};

#endif // __ADLER32_HH__
