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

#include "numeric_vocab.hh"
#include "sanity.hh"

struct
adler32
{
  u32 s1, s2, len;
  static const u32 mask = 0xffff;

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

  // monotone only uses the adler32 in order to do a rolling window over
  // the data for the purpose of finding matches in xdelta.cc
  // Optimize for this case avoiding a lot of unneeded masking.
  inline void replace_with(u8 const * ch, std::string::size_type count) 
  {
    I(count < 255);
    s1 = 1;
    s2 = 0;
    len = count;
    // Can't overflow in this case as (for s1) 255*255 < 0xffff, 
    // and (for s2) (maxs1 = 255*255)*255 < 0xffff_ffff
    while (count--) 
      {
        u32 c = widen<u32,u8>(*(ch++));
        s1 += c;
        s2 += s1;
      }
    s1 &= mask;
    s2 &= mask;
  }

  adler32()
    : s1(1), s2(0), len(0)
  {}

  adler32(u8 const * ch, std::string::size_type count)
  {
    replace_with(ch, count);
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
