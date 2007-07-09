// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>

#include "sanity.hh"
#include "rev_height.hh"

using std::ostream;
using std::string;
using std::ostringstream;

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


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
#include "randomizer.hh"

UNIT_TEST(rev_height, count_up)
{
  rev_height h = rev_height::root_height().child_height(1);

  I(h().size() / width == 3);
  I(read_at(h(), 0) == 0);
  I(read_at(h(), 1) == 0);
  I(read_at(h(), 2) == 0);
  UNIT_TEST_CHECK_THROW(read_at(h(), 3), std::out_of_range);

  for (u32 n = 1; n < 10000; n++)
    {
      h = h.child_height(0);
      I(read_at(h(), 0) == 0);
      I(read_at(h(), 1) == 0);
      I(read_at(h(), 2) == n);
    }    
}

UNIT_TEST(rev_height, children)
{
  rev_height h;
  I(!h.valid());
  h = rev_height::root_height();
  I(h.valid());
  MM(h);

  randomizer rng;
  for (u32 generations = 0; generations < 200; generations++)
    {
      L(FL("gen %d: %s") % generations % h);

      // generate between five and ten children each time
      u32 children = rng.uniform(5) + 5;
      u32 survivor_no;

      // take the first child 50% of the time, a randomly chosen second or
      // subsequent child the rest of the time.
      if (rng.flip())
        survivor_no = 0;
      else
        survivor_no = 1 + rng.uniform(children - 2);

      L(FL("gen %d: %d children, survivor %d")
        % generations % children % survivor_no);
      
      u32 parent_len = h().size() / width;
      rev_height survivor;
      MM(survivor);

      for (u32 c = 0; c < children; c++)
        {
          rev_height child = h.child_height(c);
          MM(child);
          I(child.valid());
          if (c == 0)
            {
              I(child().size() / width == parent_len);
              I(read_at(child(), parent_len - 1)
                == read_at(h(), parent_len - 1) + 1);
            }
          else
            {
              I(child().size() / width == parent_len + 2);
              I(read_at(child(), parent_len - 1)
                == read_at(h(), parent_len - 1));
              I(read_at(child(), parent_len) == c - 1);
              I(read_at(child(), parent_len + 1) == 0);
            }
          if (c == survivor_no)
            survivor = child;
        }
      I(survivor.valid());
      h = survivor;
    }
}

UNIT_TEST(rev_height, comparisons)
{
  rev_height root(rev_height::root_height());
  rev_height left(root.child_height(0));
  rev_height right(root.child_height(1));

  I(root < left);
  I(root < right);
  I(right < left);
  for (u32 i = 0; i < 1000; i++)
    {
      rev_height rchild(right.child_height(0));
      I(right < rchild);
      I(rchild < left);
      right = rchild;
    }
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
