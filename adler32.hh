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
#include "numeric_vocab.hh"

struct adler32
{
  u32 s1, s2, len;
  u32 const mask;

  inline u32 sum() const
  {
    return (s2 << 16) | s1;
  }

  inline void in(u8 c)
  {
    s1 += widen<u32,u8>(c);
    s1 &= mask;
    s2 += s1;
    s2 &= mask;
    ++len;
  }

  inline void out(u8 c)
  {
    s1 -= widen<u32,u8>(c);
    s1 &= mask;
    s2 -= (len * widen<u32,u8>(c)) + 1;
    s2 &= mask;
    --len;
  }

  adler32() 
    : s1(1), s2(0), len(0), mask(widen<u32,u16>(0xffff)) 
  {}

  adler32(u8 const * ch, std::string::size_type count)
    : s1(1), s2(0), len(0), mask(widen<u32,u16>(0xffff))
  {
    while(count--)
      in(*(ch++));
  }
};

#endif // __ADLER32_HH__
