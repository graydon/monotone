#ifndef __ADLER32_HH__
#define __ADLER32_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this is a pseudo-adler32. it does not use a prime modulus. it is not
// entirely clear that this matters; it is what rsync and xdelta both do
// and it seems to work.

#include <string>
#include "numeric_vocab.hh"

struct
adler32
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __ADLER32_HH__
