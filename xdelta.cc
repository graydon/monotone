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

#include <ext/hash_map>
#include <vector>
#include <string>
#include <sstream>
#include <boost/shared_ptr.hpp>

#include "adler32.hh"
#include "xdelta.hh"
#include "sanity.hh"

using namespace std;
using namespace __gnu_cxx;

struct identity {size_t operator()(u32 const & v) const { return static_cast<size_t>(v);}};
typedef pair<string::size_type, string::size_type> extent;
typedef hash_map<u32, extent, identity> match_table;

struct insn
{
  insn(char c) : code(insert), pos(0), len(0), payload("")  { payload += c; }
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
      u32 sum = adler32(a.data() + i, step).sum();
      if (tab.find(sum) == tab.end())
 	tab.insert(make_pair(sum, make_pair(i, step)));
    }  
  return;
}


static inline void 
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
      return;

  string::size_type tpos = e->second.first;
  string::size_type tlen = e->second.second;
  
  I(tpos < a.size());
  I(tpos + tlen <= a.size());

  // maybe it's a false match?
  if (memcmp(a.data() + tpos, b.data() + bpos, tlen) != 0)
    return;
  
  apos = tpos;
  alen = tlen;
  badvance = tlen;

  // see if we can extend our match forwards
  while((apos + alen >= 0)
	&& (bpos + badvance >= 0)
	&& (apos + alen < a.size())
 	&& (bpos + badvance < b.size())
 	&& (a[apos + alen] == b[bpos + badvance]))
    {
      ++alen;
      ++badvance;
    }

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
  return;
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
  match_table matches ((a.size() / blocksz) * 2);
  init_match_table(a, blocksz, matches);

  if (b.size() < blocksz)
    {
      for (string::size_type i = 0; i < b.size(); ++i)
	insert_insn(delta, b[i]);
      return;
    }
  
  adler32 rolling(b.data(), blocksz);  

  for (string::size_type 
	 sz = b.size(),
	 lo = 0, 
	 hi = blocksz; 
       lo < sz; )
    {
      string::size_type apos = 0, alen = 1, badvance = 1;

      find_match(matches, delta, rolling, a, b, lo, apos, alen, badvance);

      if (alen < blocksz && 
	  (apos + alen < a.size()))
	{
	  I(alen == 1);
	  I(lo >= 0);
	  I(lo < b.size());
	  insert_insn(delta, b[lo]);
	}
      else
	  copy_insn(delta, apos, alen);

      string::size_type next = lo;
      for (; next < lo + badvance; ++next)
	{
	  I(next >= 0);
	  I(next < b.size());
	  rolling.out(b[next]);
	  if (next + blocksz < b.size())
	    rolling.in(b[next + blocksz]);
	}
      lo = next;
      hi = lo + blocksz;
    }
}


void 
compute_delta(string const & a,
	      string const & b,
	      string & delta)
{
  vector<insn> delta_insns;
  L("computing binary delta instructions\n");
  compute_delta_insns(a, b, delta_insns);
  L("computed binary delta instructions\n");
  ostringstream oss;  
  for (vector<insn>::const_iterator i = delta_insns.begin(); 
       i != delta_insns.end(); ++i)
    {
      oss << *i;
    }
  delta = oss.str();
}

struct simple_applicator : public delta_applicator
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

void apply_delta(boost::shared_ptr<delta_applicator> da,
		 std::string const & delta)
{
  istringstream del(delta);
  for (char c = del.get(); c == 'I' || c == 'C'; c = del.get())
    {
      I(del.good());
      if (c == 'I')
	{ 
	  string::size_type len = string::npos;
	  del >> len;
	  I(del.good());
	  I(len != string::npos);
	  string tmp;
	  tmp.reserve(len);
	  I(del.get(c).good());
	  I(c == '\n');
	  while(len--)
	    {
	      I(del.get(c).good());
	      tmp += c;
	    }
	  I(del.get(c).good());
	  I(c == '\n');
	  da->insert(tmp);
	}
      else
	{
	  string::size_type pos = string::npos, len = string::npos;
	  del >> pos >> len;	  
	  I(del.good());
	  I(len != string::npos);
	  I(del.get(c).good());
	  I(c == '\n');
	  da->copy(pos, len);
	}
    }    
  I(del.eof());
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

typedef vector<chunk> version;

struct piece_table
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
    I(p >= 0);
    I(p < pieces.size());
    targ.append(pieces[p], pp, ln);
  }

  void build(version const & in, string & out)
  {
    out.clear();
    for (version::const_iterator i = in.begin();
	 i != in.end(); ++i)
      {	
	append(out, i->piece, i->ppos, i->len);
      }
  }
};

static void 
apply_insert(piece_table & p, version & out, string const & str)
{
  piece_id piece = p.insert(str);
  version_pos vpos = 0;
  if (!out.empty()) 
    vpos = out.back().vpos + out.back().len;
  out.push_back(chunk(str.size(), piece, vpos, 0));
}

struct chunk_less_than
{
  bool operator()(chunk const & ch, version_pos vp) const
  {
    // nb: ch.vpos + ch.len is the 0-based index of the first element *not*
    // included in ch; thus we measure against ch.len - 1.
    I(ch.len > 0);
    return (ch.vpos + ch.len - 1) < vp;
  }
};

static void 
apply_copy(version const & in, version & out, 
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
  version::const_iterator lo = lower_bound(in.begin(), 
					   in.end(), 
					   src_vpos, 
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
      src_len -= seglen;
      I(src_len >= 0);
      I(out.back().vpos + out.back().len == dst_vpos);
    }

  I(src_vpos == src_final);
  I(dst_vpos == dst_final);
  I(src_len == 0);
}


struct piecewise_applicator : public delta_applicator
{
  piece_table pt;
  boost::shared_ptr<version> src;
  boost::shared_ptr<version> dst;

  piecewise_applicator() :
    src(new version()),
    dst(new version())
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

boost::shared_ptr<delta_applicator> new_simple_applicator()
{
  return boost::shared_ptr<delta_applicator>(new simple_applicator());
}

boost::shared_ptr<delta_applicator> new_piecewise_applicator()
{
  return boost::shared_ptr<delta_applicator>(new piecewise_applicator());
}
