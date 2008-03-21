// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file implements the xdelta algorithm, produces and consumes simple
// copy/insert binary patches with the following structure:
//
// patch  := (copy|insert)*
// copy   := 'C', ' ', pos=uint, ' ', len=uint, '\n'
// insert := 'I', ' ', len=uint, '\n', payload=(byte x len), '\n'
//
// this means you can generally read the patch if you print it on stdout,
// when it applies to text, but it can also apply to any binary, so the
// hunk payload itself might look awful. I made it semi-ascii only to make
// it slightly easier to debug; you really shouldn't read it normally. it's
// a strict format with minimal checking, so it must be transport-encoded
// to avoid whitespace munging.
//
// if you want to *read* a patch, you will like unidiff format much better.
// take a look in diff_patch.(cc|hh) for a nice interface to that.

#include "base.hh"
#include <algorithm>
#include "vector.hh"
#include <set>
#include <sstream>
#include <cstring>  // memcmp

#include <boost/shared_ptr.hpp>
#include <boost/version.hpp>

#include "adler32.hh"
#include "hash_map.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "xdelta.hh"

using std::make_pair;
using std::min;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::memcmp;
using std::lower_bound;

using boost::shared_ptr;

struct identity {size_t operator()(u32 const & v) const { return static_cast<size_t>(v);}};
typedef pair<string::size_type, string::size_type> extent;
typedef hashmap::hash_map<u32, extent> match_table;

struct
insn
{
  insn(char c) : code(insert), pos(0), len(0), payload("")  { payload += c; }
  insn(string s) : code(insert), pos(0), len(s.size()), payload(s)  {}
  insn(u32 p, u32 l) : code(copy), pos(p), len(l) {}
  enum { insert, copy } code;
  u32 pos, len;
  string payload;
};

ostream & operator<<(ostream & ost, insn const & i)
{
  if (i.code == insn::insert)
    {
      ost << "I " << i.payload.size() << '\n';
      ost.write(i.payload.data(), i.payload.size());
      ost << '\n';
    }
  else
    ost << "C " << i.pos << ' ' << i.len << '\n';
  return ost;
}


static inline void
init_match_table(string const & a,
                 const string::size_type blocksz,
                 match_table & tab)
{
  string::size_type sz = a.size();
  for (string::size_type i = 0; i < sz; i += blocksz)
    {
      string::size_type step = ((i + blocksz) >= sz) ? (sz - i) : blocksz;
      u32 sum = adler32(reinterpret_cast<u8 const *>(a.data() + i), step).sum();
      if (tab.find(sum) == tab.end())
        tab.insert(make_pair(sum, make_pair(i, step)));
    }
  return;
}


static inline bool
find_match(match_table const & matches,
           vector<insn> & delta,
           adler32 const & rolling,
           string const & a,
           string const & b,
           string::size_type bpos,
           string::size_type & apos,
           string::size_type & alen,
           string::size_type & badvance)
{
  u32 sum = rolling.sum();
  match_table::const_iterator e = matches.find(sum);

  // maybe we haven't seen it at all?
  if (e == matches.end())
      return false;

  string::size_type tpos = e->second.first;
  string::size_type tlen = e->second.second;

  I(tpos < a.size());
  I(tpos + tlen <= a.size());

  // maybe it's a false match?
  if (memcmp(a.data() + tpos, b.data() + bpos, tlen) != 0)
    return false;

  apos = tpos;

  // see if we can extend our match forwards
  string::const_iterator ai = a.begin() + apos + tlen;
  string::const_iterator ae = a.end();
  string::const_iterator bi = b.begin() + bpos + tlen;
  string::const_iterator be = b.end();

  while ((ai != ae)
         && (bi != be)
         && (*ai == *bi))
    {
      ++tlen;
      ++ai;
      ++bi;
    }

  alen = tlen;
  badvance = tlen;

  // see if we can extend backwards into a previous insert hunk
  if (! delta.empty() && delta.back().code == insn::insert)
    {
      while (apos > 0
             && bpos > 0
             && a[apos - 1] == b[bpos - 1]
             && !delta.back().payload.empty())
        {
          I(a[apos - 1] == *(delta.back().payload.rbegin()));
          I(delta.back().payload.size() > 0);
          delta.back().payload.resize(delta.back().payload.size() - 1);
          --apos;
          --bpos;
          ++alen;
          // the significant thing here is that we do not move
          // 'badvance' forward, just alen.
        }

      // if we've extended back to consume the *entire* insert,
      // let's do away with it altogether.
      if (delta.back().payload.empty())
        {
          delta.pop_back();
        }
    }

  I(memcmp(a.data() + apos, b.data() + bpos, alen) == 0);
  return true;
}

static inline void
insert_insn(vector<insn> & delta, char c)
{
  if (delta.empty() || delta.back().code == insn::copy)
    delta.push_back(insn(c));
  else
    {
      // design in gcc 3.3 and 4.0 STL expands the string one character
      // at a time when appending single characters ?!
      // see libstdc++5-3.3-dev 3.3.5-13: basic_string.h:471, calling with
      // size_type(1), then basic_string.tcc:717 reserving one more byte
      // if needed.
      // see libstdc++6-4.0-dev 4.0.3-3: basic_string.h:770 calling push_back
      // then basic_string.h: 849 again adding 1 to the length.
      if (delta.back().payload.capacity() == delta.back().payload.size())
        // standard amortized constant rule
        delta.back().payload.reserve(delta.back().payload.size() * 2);
      delta.back().payload += c;
    }
}


static inline void
copy_insn(vector<insn> & delta, string::size_type i, string::size_type matchlen)
{
  delta.push_back(insn(i, matchlen));
}


static void
compute_delta_insns(string const & a,
                    string const & b,
                    vector<insn> & delta)
{
  static const string::size_type blocksz = 64;
  match_table matches;
  init_match_table(a, blocksz, matches);

  if (b.size() < blocksz)
    {
      for (string::size_type i = 0; i < b.size(); ++i)
        insert_insn(delta, b[i]);
      return;
    }

  adler32 rolling(reinterpret_cast<u8 const *>(b.data()), blocksz);

  for (string::size_type
       sz = b.size(),
       lo = 0; 
       lo < sz; )
    {
      string::size_type apos = 0, alen = 1, badvance = 1;

      bool found_match = find_match(matches, delta, rolling, a, b, lo, apos, alen, badvance);

      // There are basically three cases:
      // 1) advance by 1 (common, no match found)
      // 2) advance by >blocksz (semi-common, usual case when a match is found)
      // 3) advance by <blocksz (rare, unusual case when a match is found)
      // in case (2), all of the rolling checksum data will be entirely replaced, so
      // we can do a fast skip forward.
      if (found_match)
        {
          copy_insn(delta, apos, alen);
          u32 save_lo = lo;
          if (badvance <= blocksz) 
            {
              string::size_type next = lo;
              I(next < b.size() && (lo + badvance - 1) < b.size());
              for (; next < lo + badvance; ++next)
                {
                  rolling.out(static_cast<u8>(b[next]));
                  if (next + blocksz < b.size())
                    rolling.in(static_cast<u8>(b[next + blocksz]));
                }
              lo = next;
            }

          // Skip advancement is always correct; however, for a small
          // increment it is more expensive than incremental advancement
          // Cost of doing a in() + out() is roughly the same as doing a
          // replace_with for 1 character, so if we are advancing more than
          // blocksz/2, it will be better to do a replacement than an
          // incremental advance.  The out could be more expensive because
          // it does a multiply, but for now, ignore this; it turns out that
          // advancements in the range of [2..blocksz-1] are actually really
          // rare.
          if (badvance >= blocksz/2) 
            {
              u32 new_lo = save_lo + badvance;
              u32 new_hi = new_lo + blocksz;
              if (new_hi > b.size()) 
                {
                  new_hi = b.size();
                }
              I(new_lo <= new_hi);
              rolling.replace_with(reinterpret_cast<u8 const *>(b.data() + new_lo), new_hi-new_lo);
              lo = new_lo;
            }
        }
      else
        {
          I(apos + alen <= a.size());
          I(alen == 1);
          I(alen < blocksz);
          I(lo < b.size());
          insert_insn(delta, b[lo]);
          rolling.out(static_cast<u8>(b[lo]));
          if (lo + blocksz < b.size()) 
            {
              rolling.in(static_cast<u8>(b[lo+blocksz]));
            }
          ++lo;
        }
    }
}

void
write_delta_insns(vector<insn> const & delta_insns,
                  string & delta)
{
  delta.clear();
  ostringstream oss;
  for (vector<insn>::const_iterator i = delta_insns.begin();
       i != delta_insns.end(); ++i)
    {
      oss << *i;
    }
  delta = oss.str();
}

void
compute_delta(string const & a,
              string const & b,
              string & delta)
{
  vector<insn> delta_insns;

  // FIXME: in theory you can do empty files and empty deltas; write some
  // tests to be sure you're doing it right, and in any case implement the
  // necessary logic directly in this function, don't bother doing an
  // xdelta. several places of the xdelta code prefer assertions which are
  // only true with non-empty chunks anyways.

  if (a.size() == 0 && b.size() != 0)
    delta_insns.push_back(insn(b));
  else if (a.size() != 0 && b.size() == 0)
    delta_insns.push_back(insn(0, 0));
  else if (a == b)
    delta_insns.push_back(insn(0, a.size()));
  else
    {
      I(a.size() > 0);
      I(b.size() > 0);

      L(FL("computing binary delta instructions"));
      compute_delta_insns(a, b, delta_insns);
      L(FL("computed binary delta instructions"));
    }

  ostringstream oss;
  for (vector<insn>::const_iterator i = delta_insns.begin();
       i != delta_insns.end(); ++i)
    {
      oss << *i;
    }
  delta = oss.str();
}

struct
simple_applicator
  : public delta_applicator
{
  string src;
  string dst;
  virtual ~simple_applicator () {}
  virtual void begin(string const & base)
  {
    src = base;
    dst.clear();
  }
  virtual void next()
  {
    swap(src,dst);
    dst.clear();
  }
  virtual void finish(string & out)
  {
    out = src;
  }

  virtual void copy(string::size_type pos, string::size_type len)
  {
    dst.append(src, pos, len);
  }
  virtual void insert(string const & str)
  {
    dst.append(str);
  }
};

inline string::size_type
read_num(string::const_iterator &i,
         string::const_iterator e)
{
  string::size_type n = 0;

  while (i != e && *i == ' ')
    ++i;

  while (i != e && *i >= '0' && *i <= '9')
    {
      n *= 10;
      n += static_cast<size_t>(*i - '0');
      ++i;
    }
  return n;
}

void
apply_delta(shared_ptr<delta_applicator> da,
            string const & delta)
{
  string::const_iterator i = delta.begin();
  while (i != delta.end() && (*i == 'I' || *i == 'C'))
    {
      if (*i == 'I')
        {
          ++i;
          I(i != delta.end());
          string::size_type len = read_num(i, delta.end());
          I(i != delta.end());
          I(*i == '\n');
          ++i;
          I(i != delta.end());
          I((i - delta.begin()) + len <= delta.size());
          if (len > 0)
            {
              string tmp(i, i+len);
              da->insert(tmp);
            }
          i += len;
        }
      else
        {
          I(*i == 'C');
          ++i;
          I(i != delta.end());
          string::size_type pos = read_num(i, delta.end());
          I(i != delta.end());
          string::size_type len = read_num(i, delta.end());
          if (len != 0)
            da->copy(pos, len);
        }
      I(i != delta.end());
      I(*i == '\n');
      ++i;
    }
  I(i == delta.end());
}

void
apply_delta(string const & a,
            string const & delta,
            string & b)
{
  shared_ptr<delta_applicator> da(new simple_applicator());
  da->begin(a);
  apply_delta(da, delta);
  da->next();
  da->finish(b);
}


// diffing and patching

void
diff(data const & olddata,
     data const & newdata,
     delta & del)
{
  string unpacked;
  compute_delta(olddata(), newdata(), unpacked);
  del = delta(unpacked);
}

void
patch(data const & olddata,
      delta const & del,
      data & newdata)
{
  string result;
  apply_delta(olddata(), del(), result);
  newdata = data(result);
}


// piecewise-applicator stuff follows (warning: ugly)

typedef string::size_type version_pos;
typedef string::size_type piece_pos;
typedef string::size_type length;
typedef unsigned long piece_id;

struct chunk
{
  length len;        // how many chars in this chunk
  piece_id piece;    // which piece to take chars from
  version_pos vpos;  // position in the current version
  piece_pos ppos;    // position in piece to take chars from

  chunk (length ln, piece_id p, version_pos vp, piece_pos pp) :
    len(ln), piece(p), vpos(vp), ppos(pp)
  {}

  chunk subchunk(version_pos vp,
                 length ln,
                 length offset) const
  {
    I(ppos + offset >= ppos);
    I(ppos + offset + ln <= ppos + len);

    chunk c = *this;
    c.len = ln;
    c.vpos = vp;
    c.ppos += offset;
    return c;
  }
};

typedef vector<chunk> version_spec;

struct
piece_table
{
  vector<string> pieces;

  void clear()
  {
    pieces.clear();
  }

  piece_id insert(string const & p)
  {
    pieces.push_back(p);
    return pieces.size() - 1;
  }

  void append(string & targ, piece_id p, piece_pos pp, length ln)
  {
    I(p < pieces.size());
    targ.append(pieces[p], pp, ln);
  }

  void build(version_spec const & in, string & out)
  {
    out.clear();
    unsigned out_len = 0;
    for (version_spec::const_iterator i = in.begin(); i != in.end(); ++i)
      out_len += i->len;
    out.reserve(out_len);
    for (version_spec::const_iterator i = in.begin(); i != in.end(); ++i)
      append(out, i->piece, i->ppos, i->len);
  }
};

static void
apply_insert(piece_table & p, version_spec & out, string const & str)
{
  piece_id piece = p.insert(str);
  version_pos vpos = 0;
  if (!out.empty())
    vpos = out.back().vpos + out.back().len;
  out.push_back(chunk(str.size(), piece, vpos, 0));
}

struct
chunk_less_than
{
  bool operator()(chunk const & ch1, chunk const & ch2) const
  {
    // nb: ch.vpos + ch.len is the 0-based index of the first element *not*
    // included in ch; thus we measure against ch.len - 1.
//    I(ch1.len > 0);
    return (ch1.vpos + ch1.len - 1) < ch2.vpos;
  }
};

static void
apply_copy(version_spec const & in, version_spec & out,
           version_pos src_vpos, length src_len)
{
  //
  // this is a little tricky because there's *4* different extents
  // we're talking about at any time:
  //
  //
  // - the 'src' extent, which is 1 or more chunks in the previous version.
  //   its address in the previous version is given in terms of a version_pos
  //   + length value.
  //
  // - the 'dst' extent, which is 1 chunk in the new version. its address
  //   in the new version is given in terms of a version_pos + length value.
  //
  // - the portion of a piece referenced by the src extent, which we're
  //   selecting a subset of. this is given in terms of a piece_pos + length
  //   value, against a particular piece.
  //
  // - the portion of a piece going into the dst extent, which is the
  //   selected subset. this is given in terms of a piece_pos + length value,
  //   against a particular piece.
  //

  version_pos src_final = src_vpos + src_len;
  version_pos dst_vpos = 0;
  if (!out.empty())
    dst_vpos = out.back().vpos + out.back().len;
  version_pos dst_final = dst_vpos + src_len;
  chunk src_bounding_chunk(0,0,src_vpos,0);
  version_spec::const_iterator lo = lower_bound(in.begin(),
                                                in.end(),
                                                src_bounding_chunk,
                                                chunk_less_than());
  for ( ; src_len > 0; ++lo)
    {
      I(lo != in.end());

      // now we are iterating over src extents which cover the current dst
      // extent. we found these src extents by calling lower_bound,
      // above. note, this entire function is called once per dst extent.
      //
      // there's two possible arrangements of spanning src extents:
      //
      // [ src extent 1 ][ src extent 2 ]
      //     [ ... dst extent .. ]
      //
      // or
      //
      // [  ...    src extent   ...  ]
      //     [ ... dst extent .. ]
      //
      // the following arithmetic should bite off the lowest chunk of
      // either of these two scenarios, append it to the dst version
      // vector, and advance the 2 pos' and 1 len value appropriately.

      version_pos src_end = min ((src_vpos + src_len), (lo->vpos + lo->len));
      version_pos offset = src_vpos - lo->vpos;
      length seglen = src_end - src_vpos;

      I(seglen > 0);
      I(src_vpos >= lo->vpos);
      I(src_vpos + seglen <= lo->vpos + lo->len);

      out.push_back(lo->subchunk(dst_vpos, seglen, offset));
      src_vpos += seglen;
      dst_vpos += seglen;
      I(src_len >= seglen);
      src_len -= seglen;
      I(out.back().vpos + out.back().len == dst_vpos);
    }

  I(src_vpos == src_final);
  I(dst_vpos == dst_final);
  I(src_len == 0);
}


struct
piecewise_applicator
  : public delta_applicator
{
  piece_table pt;
  shared_ptr<version_spec> src;
  shared_ptr<version_spec> dst;

  piecewise_applicator() :
    src(new version_spec()),
    dst(new version_spec())
  {}

  virtual ~piecewise_applicator () {}

  virtual void begin(string const & base)
  {
    pt.clear();
    piece_id piece = pt.insert(base);
    src->clear();
    src->push_back(chunk(base.size(), piece, 0, 0));
    dst->clear();
  }

  virtual void next()
  {
    swap(src,dst);
    dst->clear();
  }

  virtual void finish(string & out)
  {
    out.clear();
    pt.build(*src, out);
  }

  virtual void copy(string::size_type pos, string::size_type len)
  {
    apply_copy(*src, *dst, pos, len);
  }

  virtual void insert(string const & str)
  {
    apply_insert(pt, *dst, str);
  }
};


// these just hide our implementation types from outside

shared_ptr<delta_applicator>
new_simple_applicator()
{
  return shared_ptr<delta_applicator>(new simple_applicator());
}

shared_ptr<delta_applicator>
new_piecewise_applicator()
{
  return shared_ptr<delta_applicator>(new piecewise_applicator());
}


// inversion

struct copied_extent
{
  copied_extent(string::size_type op,
                string::size_type np,
                string::size_type len)
    : old_pos(op),
      new_pos(np),
      len(len)
  {}
  string::size_type old_pos;
  string::size_type new_pos;
  string::size_type len;
  bool operator<(copied_extent const & other) const
  {
    return (old_pos < other.old_pos) ||
      (old_pos == other.old_pos && len > other.len);
  }
};

struct
inverse_delta_writing_applicator :
  public delta_applicator
{
  string const & old;
  set<copied_extent> copied_extents;
  string::size_type new_pos;

  inverse_delta_writing_applicator(string const & old)
    : old(old),
      new_pos(0)
  {}

  virtual void begin(string const & base) {}
  virtual void next() {}
  virtual void finish(string & out)
  {
    // We are trying to write a delta instruction stream which
    // produces 'old' from 'new'. We don't care what was in 'new',
    // because we're only going to copy some parts forwards, and we
    // already know which parts: those in the table. Our table lists
    // extents which were copied in order that they appear in 'old'.
    //
    // When we run into a section of 'old' which isn't in the table,
    // we have to emit an insert instruction for the gap.

    string::size_type old_pos = 0;
    out.clear();
    vector<insn> delta_insns;

    for (set<copied_extent>::iterator i = copied_extents.begin();
         i != copied_extents.end(); ++i)
      { 
        // It is possible that this extent left a gap after the
        // previously copied extent; in this case we wish to pad
        // the intermediate space with an insert.
        while (old_pos < i->old_pos)
          {
            I(old_pos < old.size());
            // Don't worry, adjacent inserts are merged.
            insert_insn(delta_insns, old.at(old_pos++));
          }

        // It is also possible that this extent *overlapped* the
        // previously copied extent; in this case we wish to subtract
        // the overlap from the inverse copy.

        string::size_type overlap = 0;
        if (i->old_pos < old_pos)
          overlap = old_pos - i->old_pos;

        if (i->len <= overlap)
          continue;

        I(i->len > overlap);
        copy_insn(delta_insns, i->new_pos + overlap, i->len - overlap);
        old_pos += (i->len - overlap);
      }
    while (old_pos < old.size())
      insert_insn(delta_insns, old.at(old_pos++));

    write_delta_insns(delta_insns, out);
  }

  virtual void copy(string::size_type old_pos,
                    string::size_type len)
  {
    I(old_pos < old.size());
    copied_extents.insert(copied_extent(old_pos, new_pos, len));
    new_pos += len;
  }

  virtual void insert(string const & str)
  {
    new_pos += str.size();
  }
};


void
invert_xdelta(string const & old_str,
              string const & delta,
              string & delta_inverse)
{
  shared_ptr<delta_applicator> da(new inverse_delta_writing_applicator(old_str));
  apply_delta(da, delta);
  da->finish(delta_inverse);
}


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"

string
apply_via_normal(string const & base, string const & delta)
{
  string tmp;
  apply_delta(base, delta, tmp);
  return tmp;
}

string
apply_via_piecewise(string const & base, string const & delta)
{
  shared_ptr<delta_applicator> appl = new_piecewise_applicator();
  appl->begin(base);
  apply_delta(appl, delta);
  appl->next();
  string tmp;
  appl->finish(tmp);
  return tmp;
}

void
spin(string a, string b)
{
  string ab, ba;
  compute_delta(a, b, ab);
  compute_delta(b, a, ba);
  UNIT_TEST_CHECK(a == apply_via_normal(b, ba));
  UNIT_TEST_CHECK(a == apply_via_piecewise(b, ba));
  UNIT_TEST_CHECK(b == apply_via_normal(a, ab));
  UNIT_TEST_CHECK(b == apply_via_piecewise(a, ab));
  string ab_inverted, ba_inverted;
  invert_xdelta(a, ab, ab_inverted);
  invert_xdelta(b, ba, ba_inverted);
  UNIT_TEST_CHECK(a == apply_via_normal(b, ab_inverted));
  UNIT_TEST_CHECK(a == apply_via_piecewise(b, ab_inverted));
  UNIT_TEST_CHECK(b == apply_via_normal(a, ba_inverted));
  UNIT_TEST_CHECK(b == apply_via_piecewise(a, ba_inverted));
}

UNIT_TEST(xdelta, simple_cases)
{
  L(FL("empty/empty"));
  spin("", "");
  L(FL("empty/short"));
  spin("", "a");
  L(FL("empty/longer"));
  spin("", "asdfasdf");
  L(FL("two identical strings"));
  spin("same string", "same string");
}

#ifdef WIN32
#define BOOST_NO_STDC_NAMESPACE
#endif
#include <boost/random.hpp>

boost::mt19937 xdelta_prng;

#if BOOST_VERSION >= 103100
boost::uniform_smallint<char> xdelta_chargen('a', 'z');
boost::uniform_smallint<size_t> xdelta_sizegen(1024, 65536);
boost::uniform_smallint<size_t> xdelta_editgen(3, 10);
boost::uniform_smallint<size_t> xdelta_lengen(1, 256);
#define PRNG xdelta_prng
#else
boost::uniform_smallint<boost::mt19937, char> xdelta_chargen(xdelta_prng, 'a', 'z');
boost::uniform_smallint<boost::mt19937, size_t> xdelta_sizegen(xdelta_prng, 1024, 65536);
boost::uniform_smallint<boost::mt19937, size_t> xdelta_editgen(xdelta_prng, 3, 10);
boost::uniform_smallint<boost::mt19937, size_t> xdelta_lengen(xdelta_prng, 1, 256);
#define PRNG
#endif

void
xdelta_random_string(string & str)
{
  size_t sz = xdelta_sizegen(PRNG);
  str.clear();
  str.reserve(sz);
  while (sz-- > 0)
    {
      str += xdelta_chargen(PRNG);
    }
}

void
xdelta_randomly_insert(string & str)
{
  size_t nedits = xdelta_editgen(PRNG);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(PRNG) % str.size();
      size_t len = xdelta_lengen(PRNG);
      if (pos+len >= str.size())
        continue;
      string tmp;
      tmp.reserve(len);
      for (size_t i = 0; i < len; ++i)
        tmp += xdelta_chargen(PRNG);
        str.insert(pos, tmp);
      nedits--;
    }
}

void
xdelta_randomly_change(string & str)
{
  size_t nedits = xdelta_editgen(PRNG);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(PRNG) % str.size();
      size_t len = xdelta_lengen(PRNG);
      if (pos+len >= str.size())
        continue;
      for (size_t i = 0; i < len; ++i)
        str[pos+i] = xdelta_chargen(PRNG);
      nedits--;
    }
}

void
xdelta_randomly_delete(string & str)
{
  size_t nedits = xdelta_editgen(PRNG);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(PRNG) % str.size();
      size_t len = xdelta_lengen(PRNG);
      if (pos+len >= str.size())
        continue;
      str.erase(pos, len);
      --nedits;
    }
}

UNIT_TEST(xdelta, random_simple_delta)
{
  for (int i = 0; i < 100; ++i)
    {
      string a, b;
      xdelta_random_string(a);
      b = a;
      xdelta_randomly_change(b);
      xdelta_randomly_insert(b);
      xdelta_randomly_delete(b);
      spin(a, b);
    }
}

UNIT_TEST(xdelta, random_piecewise_delta)
{
  for (int i = 0; i < 50; ++i)
    {
      string prev, next, got;
      xdelta_random_string(prev);
      shared_ptr<delta_applicator> appl = new_piecewise_applicator();
      appl->begin(prev);
      for (int j = 0; j < 5; ++j)
        {
          appl->finish(got);
          UNIT_TEST_CHECK(got == prev);
          next = prev;
          xdelta_randomly_change(next);
          xdelta_randomly_insert(next);
          xdelta_randomly_delete(next);
          string delta;
          compute_delta(prev, next, delta);
          apply_delta(appl, delta);
          appl->next();
          prev = next;
        }
      appl->finish(got);
      UNIT_TEST_CHECK(got == prev);
  }
}

UNIT_TEST(xdelta, rolling_sanity_check)
{
  const unsigned testbufsize = 512;
  static const string::size_type blocksz = 64;
  char testbuf[testbufsize];

  for(unsigned i = 0; i < testbufsize; ++i) 
    {
      testbuf[i] = xdelta_chargen(PRNG);
    }
  for(unsigned advanceby = 0; advanceby < testbufsize; ++advanceby) 
    {
      adler32 incremental(reinterpret_cast<u8 const *>(testbuf), blocksz);
      for(unsigned i = 0; i < advanceby; ++i) 
        {
          incremental.out(static_cast<u8>(testbuf[i]));
          if ((i + blocksz) < testbufsize) 
            {
              incremental.in(static_cast<u8>(testbuf[i+blocksz]));
            }
        }
      adler32 skip(reinterpret_cast<u8 const *>(testbuf), blocksz);
      u32 new_lo = advanceby;
      u32 new_hi = new_lo + blocksz;
      if (new_hi > testbufsize) 
        {
          new_hi = testbufsize;
        }
      skip.replace_with(reinterpret_cast<u8 const *>(testbuf + new_lo), new_hi - new_lo);

      UNIT_TEST_CHECK(skip.sum() == incremental.sum());
    }
  L(FL("rolling sanity check passed"));
}                   

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
