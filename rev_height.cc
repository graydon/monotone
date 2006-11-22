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
#include <sstream>

#include "numeric_vocab.hh"
#include "sanity.hh"
#include "rev_height.hh"

using std::ostream;
using std::string;
using std::ostringstream;
using std::memcmp;

/*
 * Implementation note: hv, holding the raw revision height, is
 * formally a string, but in fact is an array of u32 integers stored
 * in big endian byte order. The same format is used for storing
 * revision heights in the database. This has the advantage that we
 * can use memcmp() for comparing them, which will be the most common
 * operation for revision heights.
 *
 * One could also use vector<u32>. While this would be cleaner, it
 * would force us to convert back and forth to the database format
 * every now and then, and additionally inhibit the use of memcmp().
 * 
 */

rev_height::rev_height() {}

rev_height::rev_height(rev_height const & other)
{
  d = other.d;
}

void rev_height::from_string(string const & s)
{
  d = s;
}

bool rev_height::operator==(rev_height const & other) const
{
  if (d.size() != other.d.size())
    return false;

  // sizes are equal
  return (memcmp(d.data(), other.d.data(), d.size()) == 0);
}

bool rev_height::operator<(rev_height const & other) const
{
  if (d.size() == other.d.size())
    return (memcmp(d.data(), other.d.data(), d.size()) < 0);

  if (d.size() < other.d.size())
    return (memcmp(d.data(), other.d.data(), d.size()) <= 0);

  // d.size() > other.d.size()
  return (memcmp(d.data(), other.d.data(), other.d.size()) < 0);
}

string const & rev_height::operator()() const
{
  return d;
}

u32 rev_height::read_at(size_t pos) const
{
  u32 value = 0;
  size_t first = width * pos;

  for (size_t i = first; i < first + width;)
    {
      value <<= 8;
      value += d.at(i++) & 0xff;
    }

  return value;
}

void rev_height::write_at(size_t pos, u32 value)
{
  size_t first = width * pos;
  for (size_t i = first + width ; i > first;)
    {
      d.at(--i) = value & 0xff;
      value >>= 8;
    }
}

void rev_height::append(u32 value)
{
  d.resize(d.size() + width);   // make room
  write_at(size() - 1, value);
}

void rev_height::clear()
{
  d.clear();
}

size_t rev_height::size() const
{
  return d.size() / width;
}

void rev_height::child_height(rev_height & child, u32 nr) const
{
  child.from_string(d);

  if (nr == 0)
    {
      size_t pos = size() - 1;
      u32 tmp = read_at(pos);
      I(tmp < std::numeric_limits<u32>::max());
      child.write_at(pos, tmp + 1);
    }
  else
    {
      child.append(nr - 1);
      child.append(0);
    }
}

void rev_height::root_height(rev_height & root)
{
  root.clear();
  root.append(0);
}

ostream & operator <<(ostream & os, rev_height const & h)
{
  bool first(true);

  for (size_t i = 0; i < h.size(); ++i)
    {
      if (!first)
	os << '.';
      os << h.read_at(i);
      first = false;
    }
  return os;
}

void dump(rev_height const & h, string & out)
{
  ostringstream os;
  os << h;
  out = os.str();
}
