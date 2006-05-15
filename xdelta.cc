// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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

#include <algorithm>
#include <vector>
#include <set>
#include <string>
#include <sstream>

#include <boost/shared_ptr.hpp>
#include <boost/version.hpp>

#include "adler32.hh"
#include "hash_map.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "xdelta.hh"

using namespace std;

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
                 string::size_type blocksz,
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
  
  while((ai != ae)
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
      while(apos > 0 
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
    delta.back().payload += c;
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
  string::size_type blocksz = 64;
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
       lo = 0, 
       hi = blocksz; 
       lo < sz; )
    {
      string::size_type apos = 0, alen = 1, badvance = 1;

      bool found_match = find_match(matches, delta, rolling, a, b, lo, apos, alen, badvance);

      if (found_match)
        {
          copy_insn(delta, apos, alen);
        }
      else
        { 
          I(apos + alen <= a.size());
          I(alen == 1);
          I(alen < blocksz);
          I(lo < b.size());
          insert_insn(delta, b[lo]);
        }

      string::size_type next = lo;
      for (; next < lo + badvance; ++next)
        {
          I(next < b.size());
          rolling.out(static_cast<u8>(b[next]));
          if (next + blocksz < b.size())
            rolling.in(static_cast<u8>(b[next + blocksz]));
        }
      lo = next;
      hi = lo + blocksz;
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

  if (a == b)
    {
      delta.clear();
      return;
    }

  if (a.size() == 0 && b.size() != 0)
    delta_insns.push_back(insn(b));
  else if (a.size() != 0 && b.size() == 0)
    delta_insns.push_back(insn(0, 0));
  else
    {
      I(a.size() > 0);
      I(b.size() > 0);
      
      L(FL("computing binary delta instructions\n"));
      compute_delta_insns(a, b, delta_insns);
      L(FL("computed binary delta instructions\n"));
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
apply_delta(boost::shared_ptr<delta_applicator> da,
            std::string const & delta)
{  
  std::string::const_iterator i = delta.begin(); 
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
  boost::shared_ptr<delta_applicator> da(new simple_applicator());
  da->begin(a);
  apply_delta(da, delta);
  da->next();
  da->finish(b);
}

struct 
size_accumulating_delta_applicator :
  public delta_applicator
{
  u64 & sz;
  size_accumulating_delta_applicator(u64 & s) : sz(s) {}
  virtual void begin(std::string const & base) {}
  virtual void next() {}
  virtual void finish(std::string & out) {}

  virtual void copy(std::string::size_type pos, 
                    std::string::size_type len) 
  { sz += len; }
  virtual void insert(std::string const & str) 
  { sz += str.size(); }
};


u64 
measure_delta_target_size(std::string const & delta)
{
  u64 sz = 0;
  boost::shared_ptr<delta_applicator> da(new size_accumulating_delta_applicator(sz));
  apply_delta(da, delta);
  return sz;
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
    for (version_spec::const_iterator i = in.begin();
         i != in.end(); ++i)
      {        
        append(out, i->piece, i->ppos, i->len);
      }
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
  boost::shared_ptr<version_spec> src;
  boost::shared_ptr<version_spec> dst;

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

boost::shared_ptr<delta_applicator> 
new_simple_applicator()
{
  return boost::shared_ptr<delta_applicator>(new simple_applicator());
}

boost::shared_ptr<delta_applicator> 
new_piecewise_applicator()
{
  return boost::shared_ptr<delta_applicator>(new piecewise_applicator());
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

  virtual void begin(std::string const & base) {}
  virtual void next() {}
  virtual void finish(std::string & out) 
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

  virtual void copy(std::string::size_type old_pos, 
                    std::string::size_type len) 
  { 
    I(old_pos < old.size());
    copied_extents.insert(copied_extent(old_pos, new_pos, len));
    new_pos += len;
  }

  virtual void insert(std::string const & str) 
  { 
    new_pos += str.size();
  }
};


void
invert_xdelta(string const & old_str,
	      string const & delta,
	      string & delta_inverse)
{
  boost::shared_ptr<delta_applicator> da(new inverse_delta_writing_applicator(old_str));
  apply_delta(da, delta);
  da->finish(delta_inverse);
}


#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"
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
  while(sz-- > 0)
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

void 
xdelta_random_simple_delta_test()
{
  for (int i = 0; i < 100; ++i)
    {
      string a, b, fdel, rdel, c, d;
      xdelta_random_string(a);
      b = a;
      xdelta_randomly_change(b);
      xdelta_randomly_insert(b);
      xdelta_randomly_delete(b);
      compute_delta(a, b, fdel);
      compute_delta(b, a, rdel);
      L(boost::format("src %d, dst %d, fdel %d, rdel %d\n")
        % a.size() % b.size()% fdel.size() % rdel.size()) ;
      if (fdel.size() == 0)
        {
          L(boost::format("confirming src == dst and rdel == 0\n"));
          BOOST_CHECK(a == b);
          BOOST_CHECK(rdel.size() == 0);
        }      
      else
        {
          apply_delta(a, fdel, c);
          apply_delta(b, rdel, d);
          L(boost::format("confirming dst1 %d, dst2 %d\n") % c.size() % d.size());
          BOOST_CHECK(b == c);
          BOOST_CHECK(a == d);
        }
    }
}

void 
add_xdelta_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&xdelta_random_simple_delta_test));
}

#endif // BUILD_UNIT_TESTS
