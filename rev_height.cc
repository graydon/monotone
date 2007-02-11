// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstring>
#include <sstream>

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

// Comparisons
bool rev_height::operator==(rev_height const & other) const
{
  if (d.size() != other.d.size())
    return false;

  // sizes are equal
  return (memcmp(d.data(), other.d.data(), d.size()) == 0);
}

bool rev_height::operator<(rev_height const & other) const
{
  int o = memcmp(d.data(), other.d.data(),
                 std::min(d.size(), other.d.size()));
  if (o != 0)
    return o < 0;
  else
    return d.size() < other.d.size();
}

// Internal manipulations
size_t const width = sizeof(u32);

static u32 read_at(string const & d, size_t pos)
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

static void write_at(string & d, size_t pos, u32 value)
{
  size_t first = width * pos;
  for (size_t i = first + width ; i > first;)
    {
      d.at(--i) = value & 0xff;
      value >>= 8;
    }
}

static void append(string & d, u32 value)
{
  d.resize(d.size() + width);   // make room
  write_at(d, d.size() / width - 1, value);
}

// Creating derived heights
rev_height rev_height::child_height(u32 nr) const
{
  string child = d;

  if (nr == 0)
    {
      size_t pos = child.size() / width - 1;
      u32 tmp = read_at(child, pos);
      I(tmp < std::numeric_limits<u32>::max());
      write_at(child, pos, tmp + 1);
    }
  else
    {
      append(child, nr - 1);
      append(child, 0);
    }
  return rev_height(child);
}

rev_height rev_height::root_height()
{
  string root;
  append(root, 0);
  return rev_height(root);
}

// Human-readable output
ostream & operator <<(ostream & os, rev_height const & h)
{
  bool first(true);
  string const & d(h());

  for (size_t i = 0; i < d.size() / width; ++i)
    {
      if (!first)
	os << '.';
      os << read_at(d, i);
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


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
