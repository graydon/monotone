// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <string>
#include <cstring>

#include "numeric_vocab.hh"
#include "rev_height.hh"

using std::ostream;
using std::string;

/*
 * Implementation note: hv, holding the raw revision height, is
 * formally a char[], but in fact is an array of u64 integers stored
 * in big endian byte order. The same format is used for storing
 * revision heights in the database. This has the advantage that we
 * can use memcmp() for comparing them, which will be the most common
 * operation for revision heights.
 *
 * One could also use vector<u64>. While this would be cleaner, it
 * would force us to convert back and forth to the database format
 * every now and then, and additionally inhibit the use of memcmp().
 * 
 */

rev_height::rev_height(rev_height const & other)
{
  length = other.length;
  hv_length = other.hv_length;
  hv = new char[hv_length];
  memcpy(hv, other.hv, hv_length);
}

rev_height::rev_height()
{
  hv = NULL;
  length = 0;
  hv_length = 0;
}

void rev_height::resize(size_t _length)
{
  delete [] hv;
  length = _length;
  hv_length = _length * width;
  hv = new char[hv_length];
}

void rev_height::from_string(string const & s)
{
  // I(s.size % width == 0)
  resize(s.size() / width);
  memcpy(hv, s.data(), hv_length);
}

bool rev_height::operator==(rev_height const & other) const
{
  if (hv_length != other.hv_length)
    return false;

  return (memcmp(hv, other.hv, hv_length) == 0);
}

bool rev_height::operator<(rev_height const & other) const
{
  if (hv_length == other.hv_length)
    return (memcmp(hv, other.hv, hv_length) < 0);

  if (hv_length < other.hv_length)
    return (memcmp(hv, other.hv, hv_length) <= 0);

  return (memcmp(hv, other.hv, other.hv_length) >= 0);
}

void rev_height::to_string(string & s) const
{
  s = string(hv, hv_length);
}

u64 rev_height::read_at(size_t pos) const
{
  u64 value = 0;
  size_t first = width * pos;

  for (size_t i = 0; i < width; ++i)
    {
      u64 tmp = hv[first + i] & 0xff;
      value += tmp << ((width - 1 - i) * 8);
    }

  return value;
}

void rev_height::write_at(size_t pos, u64 value)
{
  size_t first = width * pos;
  for (size_t i = first + width ; i > first;)
    {
      --i;
      hv[i] = value & 0xff;
      value >>= 8;
    }
}

void rev_height::child_height(rev_height & child, u64 nr) const
{
  if (nr == 0)
    {
      child.resize(length);
      child.write_at(child.length - 1, read_at(length-1) + 1);
    }
  else
    {
      child.resize(length + 2);
      child.write_at(child.length - 2, nr - 1);
      child.write_at(child.length - 1, 0);
    }
}

void rev_height::root_height(rev_height & root)
{
  root.resize(1);
  root.write_at(0, 0);
}

ostream & operator <<(ostream & os, rev_height const & h)
{
  h.dump(os);
  return os;
}

void rev_height::dump(ostream & os) const
{
  bool first(true);
  for (size_t i = 0; i < length; ++i)
    {
      if (!first)
	os << '.';
      os << read_at(i);
      first = false;
    }
}
