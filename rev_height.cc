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
 * formally a string, but in fact is an array of u64 integers stored
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

u64 rev_height::read_at(size_t pos) const
{
  u64 value = 0;
  size_t first = width * pos;

  for (size_t i = first; i < first + width;)
    {
      value <<= 8;
      value += d.at(i++) & 0xff;
    }

  return value;
}

void rev_height::write_at(size_t pos, u64 value)
{
  size_t first = width * pos;
  for (size_t i = first + width ; i > first;)
    {
      d.at(--i) = value & 0xff;
      value >>= 8;
    }
}

void rev_height::append(u64 value)
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

void rev_height::child_height(rev_height & child, u64 nr) const
{
  if (nr == 0)
    {
      size_t pos = size() - 1;
      child.from_string(d);
      child.write_at(pos, read_at(pos) + 1);
    }
  else
    {
      child.from_string(d);
      child.append(nr - 1);
      child.append(0);
    }
}

void rev_height::root_height(rev_height & root)
{
  root.clear();
  root.append(0);
}

// for debugging purposes

void rev_height::dump(ostream & os) const
{
  bool first(true);
  for (size_t i = 0; i < size(); ++i)
    {
      if (!first)
	os << '.';
      os << read_at(i);
      first = false;
    }
}

ostream & operator <<(ostream & os, rev_height const & h)
{
  h.dump(os);
  return os;
}

