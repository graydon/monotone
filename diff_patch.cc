// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iterator>
#include <vector>
#include <string>
#include <iostream>

#include "diff_patch.hh"
#include "interner.hh"
#include "lcs.hh"
#include "manifest.hh"
#include "packet.hh"
#include "patch_set.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"

using namespace std;

bool guess_binary(string const & s)
{
  // these do not occur in text files
  if (s.find_first_of("\x00\x01\x02\x03\x04\x05\x06\x0e\x0f"
		      "\x10\x11\x12\x13\x14\x15\x16\x17\x18"
		      "\x19\x1a\x1c\x1d\x1e\x1f") != string::npos)
    return true;
  return false;
}

//
// a 3-way merge works like this:
//
//            /---->   right
//    ancestor
//            \---->   left
//
// first you compute the edit list "EDITS(ancestor,left)".
// 
// then you make an offset table "leftpos" which describes positions in
// "ancestor" as they map to "left"; that is, for 0 < apos <
// ancestor.size(), we have
//
//  left[leftpos[apos]] == ancestor[apos]
//
// you do this by walking through the edit list and either jumping the
// current index ahead an extra position, on an insert, or remaining still,
// on a delete. on an insert *or* a delete, you push the current index back
// onto the leftpos array.
//
// next you compute the edit list "EDITS(ancestor,right)".
//
// you then go through this edit list applying the edits to left, rather
// than ancestor, and using the table leftpos to map the position of each
// edit to an appropriate spot in left. this means you walk a "curr_left" 
// index through the edits, and for each edit e:
//
// - if e is a delete (and e.pos is a position in ancestor)
//   - increment curr_left without copying anything to "merged"
//
// - if e is an insert (and e.pos is a position in right)
//   - copy right[e.pos] to "merged"
//   - leave curr_left alone
//
// - when advancing to apos (and apos is a position in ancestor)
//   - copy left[curr_left] to merged while curr_left < leftpos[apos]
//
//
// the practical upshot is that you apply the delta from ancestor->right
// to the adjusted contexts in left, producing something vaguely like
// the concatenation of delta(ancestor,left) :: delta(ancestor,right).
//
// NB: this is, as far as I can tell, what diff3 does. I don't think I'm
// infringing on anyone's fancy patents here.
//

struct hunk_consumer
{
  virtual void flush_hunk(size_t pos) = 0;
  virtual void advance_to(size_t newpos) = 0;
  virtual void insert_at(size_t b_pos) = 0;
  virtual void delete_at(size_t a_pos) = 0;
  virtual ~hunk_consumer() {}
};

void walk_hunk_consumer(vector<long> const & lcs,
			vector<long> const & lines1,
			vector<long> const & lines2,			
			hunk_consumer & cons)
{

  size_t a = 0, b = 0;
  if (lcs.begin() == lcs.end())
    {
      // degenerate case: files have nothing in common
      cons.advance_to(0);
      while (a < lines1.size())
	cons.delete_at(a++);
      while (b < lines2.size())
	cons.insert_at(b++);
      cons.flush_hunk(a);
    }
  else
    {
      // normal case: files have something in common
      for (vector<long>::const_iterator i = lcs.begin();
	   i != lcs.end(); ++i, ++a, ++b)
	{      	  
	  if (idx(lines1, a) == *i && idx(lines2, b) == *i)
	    continue;

	  cons.advance_to(a);
	  while (idx(lines1,a) != *i)
	      cons.delete_at(a++);
	  while (idx(lines2,b) != *i)
	    cons.insert_at(b++);
	}
      if (b < lines2.size())
	{
	  cons.advance_to(a);
	  while(b < lines2.size())
	    cons.insert_at(b++);
	}
      if (a < lines1.size())
	{
	  cons.advance_to(a);
	  while(a < lines1.size())
	    cons.delete_at(a++);
	}
      cons.flush_hunk(a);
    }
}


// helper class which calculates the offset table

struct hunk_offset_calculator : public hunk_consumer
{
  vector<size_t> & leftpos;
  set<size_t> & deletes;
  set<size_t> & inserts;
  size_t apos;
  size_t lpos;
  size_t final;
  hunk_offset_calculator(vector<size_t> & lp, size_t fin, 
			 set<size_t> & dels, set<size_t> & inss);
  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~hunk_offset_calculator();
};

hunk_offset_calculator::hunk_offset_calculator(vector<size_t> & off, size_t fin,
					       set<size_t> & dels, set<size_t> & inss)
  : leftpos(off), deletes(dels), inserts(inss), apos(0), lpos(0), final(fin)
{}

hunk_offset_calculator::~hunk_offset_calculator()
{
  while(leftpos.size() < final)
    leftpos.push_back(leftpos.back());
}

void hunk_offset_calculator::flush_hunk(size_t ap)
{
  this->advance_to(ap);
}

void hunk_offset_calculator::advance_to(size_t ap)
{
  while(apos < ap)
    {
      //       L(F("advance to %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
      //         ap % apos % lpos % apos+1 % lpos+1 % leftpos.size());
      apos++;
      leftpos.push_back(lpos++);
    }
}

void hunk_offset_calculator::insert_at(size_t lp)
{
  //   L(F("insert at %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
  //     lp % apos % lpos % apos % lpos+1 % leftpos.size());
  inserts.insert(apos);
  I(lpos == lp);
  lpos++;
}

void hunk_offset_calculator::delete_at(size_t ap)
{
  //   L(F("delete at %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
  //      ap % apos % lpos % apos+1 % lpos % leftpos.size());
  deletes.insert(apos);
  I(apos == ap);
  apos++;
  leftpos.push_back(lpos);
}

void calculate_hunk_offsets(vector<string> const & ancestor,
			    vector<string> const & left,
			    vector<size_t> & leftpos,
			    set<size_t> & deletes, 
			    set<size_t> & inserts)
{

  vector<long> anc_interned;  
  vector<long> left_interned;  
  vector<long> lcs;  

  interner<long> in;

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  lcs.reserve(std::min(left.size(),ancestor.size()));
  longest_common_subsequence(anc_interned.begin(), anc_interned.end(),
			     left_interned.begin(), left_interned.end(),
			     std::min(ancestor.size(), left.size()),
			     back_inserter(lcs));

  leftpos.clear();
  hunk_offset_calculator calc(leftpos, ancestor.size(), deletes, inserts);
  walk_hunk_consumer(lcs, anc_interned, left_interned, calc);
}


struct conflict {};

// utility class which performs the merge

/*
struct hunk_merger : public hunk_consumer
{
  vector<string> const & left;
  vector<string> const & ancestor;
  vector<string> const & right;
  vector<size_t> const & ancestor_to_leftpos_map;
  set<size_t> const & deletes;
  set<size_t> const & inserts;  
  vector<string> & merged;
  size_t apos; 
  size_t lpos; 
  hunk_merger(vector<string> const & lft,
	      vector<string> const & anc,
	      vector<string> const & rght,
	      vector<size_t> const & lpos,
	      set<size_t> const & dels,
	      set<size_t> const & inss,  	      
	      vector<string> & mrgd);	      
  virtual void flush_hunk(size_t apos);
  virtual void advance_to(size_t apos);
  virtual void insert_at(size_t rpos);
  virtual void delete_at(size_t apos);
  virtual ~hunk_merger();
};

hunk_merger::hunk_merger(vector<string> const & lft,
			 vector<string> const & anc,
			 vector<string> const & rght,
			 vector<size_t> const & lpos,
			 set<size_t> const & dels,
			 set<size_t> const & inss,  	      
			 vector<string> & mrgd)
  : left(lft), ancestor(anc), right(rght), 
    ancestor_to_leftpos_map(lpos), deletes(dels),
    inserts(inss), merged(mrgd),
    apos(0), lpos(0)
{
  merged.clear();
}

hunk_merger::~hunk_merger()
{
  while (lpos < left.size())
    {
      // L(F("filling in final segment %d : '%s'\n") % lpos % left.at(lpos));
      merged.push_back(left.at(lpos++));
    }
}

void hunk_merger::flush_hunk(size_t ap)
{
  advance_to(ap);
}

// notes: 
//
// we are processing diff(ancestor,right), and applying the results
// to positions in left.
//
// advancing to 'ap' means that *nothing changed* in ancestor->right,
// between apos and ap, so you should copy all the lines between
// translated(apos) and translated(ap), from left to merged.

void hunk_merger::advance_to(size_t ap)
{

  I(ap <= ancestor_to_leftpos_map.size());

  size_t lend = 0;
  if (!ancestor_to_leftpos_map.empty())
    {
      if (ap == ancestor_to_leftpos_map.size())
	lend = left.size();
      else
	lend = idx(ancestor_to_leftpos_map, ap);
    }

//   L(F("advance to %d / %d (lpos = %d / %d -> %d / %d)\n") % 
//     ap % ancestor.size() % lpos % left.size() %
//     lend % left.size());

  I(lpos <= lend);
  I(lend <= left.size());

  while (lpos < lend)
    merged.push_back(idx(left,lpos++));

  apos = ap;
  if (apos > ancestor_to_leftpos_map.size())
    apos = ancestor_to_leftpos_map.size();
}

// notes: 
//
// we are processing diff(ancestor,right), and applying the results
// to positions in left.
//
// inserting at 'rp' means that the value at right[rp] was inserted here
// (where "here" means "at lpos"). our job is to figure out whether that
// insert is a conflict, or can be repeated.
//
// there are 4 cases:
//
// 1: a->l contained no change here
// 2: a->l contained a delete here
// 3: a->l contained an insert here
//  3.1: the insert was identical to ours
//  3.2: the insert was different from ours
//
// our interpretation of these cases is as follows:
//
//  1: insert our data, leave apos as is
//  2: conflict
//  3.1: ignore our (duplicate) insert
//  3.2: conflict

void hunk_merger::insert_at(size_t rp)
{
//   L(F("insert at %d (lpos = %d) '%8s'...\n") % rp % lpos %
//     idx(right,rp));
  I(ancestor.size() == ancestor_to_leftpos_map.size());
  I(rp < right.size());
  I(apos <= ancestor_to_leftpos_map.size());

  bool insert_here = inserts.find(apos) != inserts.end();
  bool delete_here = deletes.find(apos) != deletes.end();

  if (! (insert_here || delete_here))
    merged.push_back(idx(right,rp));
  
  else if (delete_here)
    {
      L(F("insert conflict type 1 -- delete "
	  "(apos = %d, lpos = %d, translated apos = %d)\n")
	% apos % lpos 
	% idx(ancestor_to_leftpos_map,apos));
      throw conflict();
    }
  
  else if (insert_here)
    {
      // simultaneous, identical insert?
      if (idx(left,lpos) == idx(right,rp))
	return;
      
      else
	{
	  L(F("insert conflict type 2 -- insert "
	      "(apos = %d, lpos = %d, translated apos = %d)\n")
	    % apos % lpos
	    % idx(ancestor_to_leftpos_map,apos));
	  L(F("trying to insert '%s', with '%s' conflicting\n")
	    % idx(right,rp) % idx(left,lpos));
	  throw conflict();
	}
    }
}

// notes: 
//
// we are processing diff(ancestor,right), and applying the results
// to positions in left.
//
// deleting at 'ap' means that the value at ancestor[ap] was removed on
// edge a->r, and is not present here in right (where "here" means "at
// lpos"). our job is to figure out whether that delete is a conflict, or
// can be repeated.
//
// there are 3 cases:
//
// 1: a->l contained no change here
// 2: a->l contained a delete here
//  2.1: there was no skew
//  2.2: there was skew
// 3: a->l contained an insert here
//
// our interpretation of these cases is as follows:
//
//  1: "delete" the line (increment apos) 
//  2.1: do nothing, the line's already "missing" in the translation of apos
//  2.2: conflict
//  3: conflict

void hunk_merger::delete_at(size_t ap)
{
//   L(F("delete at %d (apos = %d, lpos = %d, translated = %d) '%8s'...\n") % 
//     ap % apos % lpos % idx(ancestor_to_leftpos_map,ap) %
//     idx(ancestor,ap));
  I(ancestor.size() == ancestor_to_leftpos_map.size());
  I(ap < ancestor_to_leftpos_map.size());
  I(ap == apos);
  I(idx(ancestor_to_leftpos_map,ap) == lpos);

  bool insert_here = inserts.find(apos) != inserts.end();
  bool delete_here = deletes.find(apos) != deletes.end();

  if (delete_here || !insert_here)
    {
      apos++; 
      if (!delete_here)
	lpos++;
    }
  
  else
    {
      L(F("delete conflict -- insert "
	  "(apos = %d, lpos = %d, translated apos = %d)\n")
	% apos % lpos
	% idx(ancestor_to_leftpos_map,apos));
      L(F("trying to delete '%s', with '%s' conflicting\n")
	% idx(ancestor,ap) % idx(left,lpos));
      throw conflict();
    }
}

void merge_hunks_via_offsets(vector<string> const & left,
			     vector<string> const & ancestor,
			     vector<string> const & right,
			     vector<size_t> const & leftpos,
			     set<size_t> const & deletes,
			     set<size_t> const & inserts,
			     vector<string> & merged)
{
  vector<long> anc_interned;  
  vector<long> left_interned;  
  vector<long> right_interned;  
  vector<long> lcs;  

  interner<long> in;

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(right.size());
  for (vector<string>::const_iterator i = right.begin();
       i != right.end(); ++i)
    right_interned.push_back(in.intern(*i));

  lcs.reserve(std::min(right.size(),ancestor.size()));
  longest_common_subsequence(anc_interned.begin(), anc_interned.end(),
			     right_interned.begin(), right_interned.end(),
			     std::min(ancestor.size(), right.size()),
			     back_inserter(lcs));
  hunk_merger merger(left, ancestor, right, leftpos, deletes, inserts, merged);
  walk_hunk_consumer(lcs, anc_interned, right_interned, merger);
}
*/

typedef enum { preserved = 0, deleted = 1, changed = 2 } edit_t;
static char *etab[3] = 
  {
    "preserved",
    "deleted",
    "changed"
  };

struct extent
{
  extent(size_t p, size_t l, edit_t t) 
    : pos(p), len(l), type(t)
  {}
  size_t pos;
  size_t len;
  edit_t type;
};

void calculate_extents(vector<long> const & a_b_edits,
		       vector<long> const & b,
		       vector<long> & prefix,
		       vector<extent> & extents,
		       vector<long> & suffix,
		       size_t const a_len)
{
  extents.reserve(a_len * 2);

  size_t a_pos = 0, b_pos = 0;

  for (vector<long>::const_iterator i = a_b_edits.begin(); 
       i != a_b_edits.end(); ++i)
    {
      if (*i < 0)
	{
	  // negative elements code the negation of the one-based index into A
	  // of the element to be deleted
	  size_t a_deleted = (-1 - *i);

	  // fill positions out to the deletion point
	  while (a_pos < a_deleted)
	    {
	      a_pos++;
	      extents.push_back(extent(b_pos++, 1, preserved));
	    }

	  // skip the deleted line
	  a_pos++;
	  extents.push_back(extent(b_pos, 0, deleted));
	}
      else
	{
	  // positive elements code the one-based index into B of the element to
	  // be inserted
	  size_t b_inserted = (*i - 1);

	  // fill positions out to the insertion point
	  while (b_pos < b_inserted)
	    {
	      a_pos++;
	      extents.push_back(extent(b_pos++, 1, preserved));
	    }
	  
	  // record that there was an insertion, but a_pos did not move.
	  if ((b_pos == 0 && extents.empty())
	      || (b_pos == prefix.size()))
	    {
	      prefix.push_back(b.at(b_pos));
	    }
	  else if (a_len == a_pos)
	    {
	      suffix.push_back(b.at(b_pos));
	    }
	  else
	    {
	      // make the insertion
	      extents.back().type = changed;
	      extents.back().len++;
	    }
	  b_pos++;
	}
    }

  while (extents.size() < a_len)
    extents.push_back(extent(b_pos++, 1, preserved));
}


void merge_extents(vector<extent> const & a_b_map,
		   vector<extent> const & a_c_map,
		   vector<long> const & b,
		   vector<long> const & c,
		   interner<long> const & in,
		   vector<long> & merged)
{
  I(a_b_map.size() == a_c_map.size());

  vector<extent>::const_iterator i = a_b_map.begin();
  vector<extent>::const_iterator j = a_c_map.begin();
  merged.reserve(a_b_map.size() * 2);

  for (; i != a_b_map.end(); ++i, ++j)
    {

      
      //       L(F("trying to merge: [%s %d %d] vs. [%s %d %d] ")
      // 	% etab[i->type] % i->pos % i->len 
      // 	% etab[j->type] % j->pos % j->len);

      // mutual, identical preserves / inserts / changes
      if (((i->type == changed && j->type == changed)
	   || (i->type == preserved && j->type == preserved))
	  && i->len == j->len)
	{
	  for (size_t k = 0; k < i->len; ++k)
	    {
	      if (b.at(i->pos + k) != c.at(j->pos + k))
		{
		  L(F("conflicting edits: %s %d[%d] '%s' vs. %s %d[%d] '%s'\n")
		    % etab[i->type] % i->pos % k % in.lookup(b.at(i->pos + k)) 
		    % etab[j->type] % j->pos % k % in.lookup(c.at(j->pos + k)));
		  throw conflict();
		}
	      merged.push_back(b.at(i->pos + k));
	    }
	}

      // mutual or single-edge deletes
      else if ((i->type == deleted && j->len == deleted)
	       || (i->type == deleted && j->type == preserved)
	       || (i->type == preserved && j->type == deleted))
	{ 
	  // do nothing
	}

      // single-edge insert / changes 
      else if (i->type == changed && j->type == preserved)
	for (size_t k = 0; k < i->len; ++k)
	  merged.push_back(b.at(i->pos + k));
      
      else if (i->type == preserved && j->type == changed)
	for (size_t k = 0; k < j->len; ++k)
	  merged.push_back(c.at(j->pos + k));
      
      else
	{
	  L(F("conflicting edits: [%s %d %d] vs. [%s %d %d]\n")
	    % etab[i->type] % i->pos % i->len 
	    % etab[j->type] % j->pos % j->len);
	  throw conflict();	  
	}      

      //       if (merged.empty())
      // 	L(F(" --> EMPTY\n"));
      //       else
      // 	L(F(" --> [%d]: %s\n") % (merged.size() - 1) % in.lookup(merged.back()));
    }
}


void merge_via_edit_scripts(vector<string> const & ancestor,
			    vector<string> const & left,			    
			    vector<string> const & right,
			    vector<string> & merged)
{
  vector<long> anc_interned;  
  vector<long> left_interned, right_interned;  
  vector<long> left_edits, right_edits;  
  vector<long> left_prefix, right_prefix;  
  vector<long> left_suffix, right_suffix;  
  vector<extent> left_extents, right_extents;
  vector<long> merged_interned;
  vector<long> lcs;
  interner<long> in;

  //   for (int i = 0; i < std::min(std::min(left.size(), right.size()), ancestor.size()); ++i)
  //     {
  //       std::cerr << "[" << i << "] " << left[i] << " " << ancestor[i] <<  " " << right[i] << endl;
  //     }

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(right.size());
  for (vector<string>::const_iterator i = right.begin();
       i != right.end(); ++i)
    right_interned.push_back(in.intern(*i));

  L(F("calculating left edit script on %d -> %d lines\n")
    % anc_interned.size() % left_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
	      left_interned.begin(), left_interned.end(),
	      std::min(ancestor.size(), left.size()),
	      left_edits, back_inserter(lcs));
  
  L(F("calculating right edit script on %d -> %d lines\n")
    % anc_interned.size() % right_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
	      right_interned.begin(), right_interned.end(),
	      std::min(ancestor.size(), right.size()),
	      right_edits, back_inserter(lcs));

  L(F("calculating left extents on %d edits\n") % left_edits.size());
  calculate_extents(left_edits, left_interned, 
		    left_prefix, left_extents, left_suffix, 
		    anc_interned.size());

  L(F("calculating right extents on %d edits\n") % right_edits.size());
  calculate_extents(right_edits, right_interned, 
		    right_prefix, right_extents, right_suffix, 
		    anc_interned.size());

  if ((!right_prefix.empty()) && (!left_prefix.empty()))
    {
      L(F("conflicting prefixes\n"));
      throw conflict();
    }

  if ((!right_suffix.empty()) && (!left_suffix.empty()))
    {
      L(F("conflicting suffixes\n"));
      throw conflict();
    }

  L(F("merging %d left, %d right extents\n") 
    % left_extents.size() % right_extents.size());

  copy(left_prefix.begin(), left_prefix.end(), back_inserter(merged_interned));
  copy(right_prefix.begin(), right_prefix.end(), back_inserter(merged_interned));

  merge_extents(left_extents, right_extents,
		left_interned, right_interned, 
		in, merged_interned);

  copy(left_suffix.begin(), left_suffix.end(), back_inserter(merged_interned));
  copy(right_suffix.begin(), right_suffix.end(), back_inserter(merged_interned));

  merged.reserve(merged_interned.size());
  for (vector<long>::const_iterator i = merged_interned.begin();
       i != merged_interned.end(); ++i)
    merged.push_back(in.lookup(*i));
}


bool merge3(vector<string> const & ancestor,
	    vector<string> const & left,
	    vector<string> const & right,
	    vector<string> & merged)
{
  try 
   { 
      merge_via_edit_scripts(ancestor, left, right, merged);
    }
  catch(conflict & c)
    {
      L(F("conflict detected. no merge.\n"));
      return false;
    }
  return true;
}

/*

bool merge3_lcs(vector<string> const & ancestor,
		vector<string> const & left,
		vector<string> const & right,
		vector<string> & merged)
{
  try 
    {
      vector<size_t> leftpos;
      set<size_t> deletes;
      set<size_t> inserts;
      
      L(F("calculating offsets from ancestor:[%d..%d) to left:[%d..%d)\n")
	% 0 % ancestor.size() % 0 % left.size());
      calculate_hunk_offsets(ancestor, left, leftpos, deletes, inserts);

      L(F("sanity-checking offset table (sz=%d, ancestor=%d)\n")
	% leftpos.size() % ancestor.size());
      I(leftpos.size() == ancestor.size());
      for(size_t i = 0; i < ancestor.size(); ++i)
	{
	  if (idx(leftpos,i) > left.size())
	    L(F("weird offset table: leftpos[%d] = %d (left.size() = %d)\n") 
	      % i % idx(leftpos,i) % left.size());
	  I(idx(leftpos,i) <= left.size());
	}
      
      L(F("merging differences from ancestor:[%d..%d) to right:[%d..%d)\n")
	% 0 % ancestor.size() % 0 % right.size());
      merge_hunks_via_offsets(left, ancestor, right, leftpos, 
			      deletes, inserts, merged);
    }
  catch(conflict & c)
    {
      L(F("conflict detected. no merge.\n"));
      return false;
    }
  return true;
}

*/

// this does a merge2 on manifests. it's not as likely to succeed as the
// merge3 on manifests, but you do what you can..

bool merge2(manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged)
{
  for (manifest_map::const_iterator i = left.begin();
       i != left.end(); ++i)
    {
      // we go left-to-right. we could go right-to-left too, but who
      // knows. merge2 is really weak. FIXME: replace this please.
      path_id_pair l_pip(i);
      manifest_map::const_iterator j = right.find(l_pip.path());
      if (right.find(l_pip.path()) != right.end())
	{
	  // right has this file, we can try to merge2 the file.
	  path_id_pair r_pip(j);
	  path_id_pair m_pip;
	  L(F("merging existant versions of %s in both manifests\n")
	    % l_pip.path());
	  if (file_merger.try_to_merge_files(l_pip, r_pip, m_pip))
	    {
	      L(F("arrived at merged version %s\n") % m_pip.ident());
	      merged.insert(m_pip.get_entry()); 
	    }
	  else
	    {
	      merged.clear();
	      return false;
	    }
	}
      else
	{
	  // right hasn't seen this file at all.
	  merged.insert(l_pip.get_entry());
	}
    }
  return true;
}


void check_no_intersect(set<file_path> const & a,
			set<file_path> const & b,
			string const & a_name,
			string const & b_name)
{
  set<file_path> tmp;
  set_intersection(a.begin(), a.end(), b.begin(), b.end(),
		   inserter(tmp, tmp.begin()));
  if (tmp.empty())
    {
      L(F("file sets '%s' and '%s' do not intersect\n")
	% a_name % b_name);      
    }
  else
    {
      L(F("conflict: file sets '%s' and '%s' intersect\n")
	% a_name % b_name);
      throw conflict();
    }
}

template <typename K, typename V>
void check_map_inclusion(map<K,V> const & a,
			 map<K,V> const & b,
			 string const & a_name,
			 string const & b_name)
{
  for (typename map<K, V>::const_iterator self = a.begin();
       self != a.end(); ++self)
    {
      typename map<K, V>::const_iterator other =
	b.find(self->first);

      if (other == b.end())
	{
	  L(F("map '%s' has unique entry for %s -> %s\n")
	    % a_name % self->first % self->second);
	}
      
      else if (other->second == self->second)
	{
	  L(F("maps '%s' and '%s' agree on %s -> %s\n")
	    % a_name % b_name % self->first % self->second);
	}

      else
	{
	  L(F("conflict: maps %s and %s disagree: %s -> %s and %s\n") 
	    % a_name % b_name
	    % self->first % self->second % other->second);
	  throw conflict();
	}
    }  
}


void merge_deltas (map<file_path, patch_delta> const & self_map,
		   map<file_path, patch_delta> & other_map,
		   file_merge_provider & file_merger,
		   patch_set & merged_edge)
{
  for (map<file_path, patch_delta>::const_iterator delta = self_map.begin();
       delta != self_map.end(); ++delta)
    {
      
      map<file_path, patch_delta>::const_iterator other = 
	other_map.find(delta->first);

      if (other == other_map.end())
	{
	  merged_edge.f_deltas.insert(patch_delta(delta->second.id_old,
						  delta->second.id_new,
						  delta->first));
	}
      else
	{
	  // concurrent deltas! into the file merger we go..
	  I(other->second.id_old == delta->second.id_old);
	  I(other->second.path == delta->second.path);
	  L(F("attempting to merge deltas on %s : %s -> %s and %s\n")
	    % delta->first 
	    % delta->second.id_old 
	    % delta->second.id_new
	    % other->second.id_new);
	  
	  path_id_pair a_pip = path_id_pair(make_pair(delta->second.path, 
						      delta->second.id_old));
	  path_id_pair l_pip = path_id_pair(make_pair(delta->first,
						      delta->second.id_new));
	  path_id_pair r_pip = path_id_pair(make_pair(delta->first,
						      other->second.id_new));
	  path_id_pair m_pip;
	  if (file_merger.try_to_merge_files(a_pip, l_pip, r_pip, m_pip))
	    {
	      L(F("concurrent deltas on %s merged to %s OK\n")
		% delta->first % m_pip.ident());
	      I(m_pip.path() == delta->first);
	      other_map.erase(delta->first);
	      merged_edge.f_deltas.insert(patch_delta(delta->second.id_old,
						      m_pip.ident(),
						      m_pip.path()));
	    }
	  else
	    {
	      L(F("conflicting deltas on %s, merge failed\n")
		% delta->first);
	      throw conflict();
	    }	  
	}
    }  
}


static void apply_directory_moves(map<file_path, file_path> const & dir_moves,
				  file_path & fp)
{
  fs::path stem = fs::path(fp());
  fs::path rest;
  while (stem.has_branch_path())
    {
      rest = stem.leaf() / rest;
      stem = stem.branch_path();
      map<file_path, file_path>::const_iterator j = 
	dir_moves.find(file_path(stem.string()));
      if (j != dir_moves.end())
	{
	  file_path old = fp;
	  fp = file_path((fs::path(j->second()) / rest).string());
	  L(F("applying dir rename: %s -> %s\n") % old % fp);
	  return;
	}      
    }
}

static void rebuild_under_directory_moves(map<file_path, file_path> const & dir_moves,
					  manifest_map const & in,
					  manifest_map & out)
{
  for (manifest_map::const_iterator mm = in.begin();
       mm != in.end(); ++mm)
    {
      file_path fp = mm->first;
      apply_directory_moves(dir_moves, fp);
      I(out.find(fp) == out.end());
      out.insert(make_pair(fp, mm->second));
    }
}
					  

static void infer_directory_moves(manifest_map const & ancestor,
				  manifest_map const & child,
				  map<file_path, file_path> const & moves,
				  map<file_path, file_path> & dir_portion)
{
  for (map<file_path, file_path>::const_iterator mov = moves.begin();
       mov != moves.end(); ++mov)
    {
      fs::path src = fs::path(mov->first());
      fs::path dst = fs::path(mov->second());

      // we will call this a "directory move" if the branch path changed,
      // and the new branch path didn't exist in the ancestor, and there
      // old branch path doesn't exist in the child
      
      if (src.empty() 
	  || dst.empty() 
	  || !src.has_branch_path() 
	  || !dst.has_branch_path())
	continue;
      
      if (src.branch_path().string() != dst.branch_path().string())
	{
	  fs::path dp = dst.branch_path();
	  fs::path sp = src.branch_path();

	  if (dir_portion.find(file_path(sp.string())) != dir_portion.end())
	    continue;

	  bool clean_move = true;

	  for (manifest_map::const_iterator mm = ancestor.begin();
	       mm != ancestor.end(); ++mm)
	    {
	      fs::path mp = fs::path(mm->first());
	      if (mp.branch_path().string() == dp.string())
		{
		  clean_move = false;
		  break;
		}
	    }
	  
	  if (clean_move)
	    for (manifest_map::const_iterator mm = child.begin();
		 mm != child.end(); ++mm)
	      {
		fs::path mp = fs::path(mm->first());
		if (mp.branch_path().string() == sp.string())
		  {
		    clean_move = false;
		    break;
		  }
	      }
	  
	  
	  if (clean_move)
	    {
	      L(F("inferred pure directory rename %s -> %s\n")
		% sp.string() % dst.branch_path().string());
	      dir_portion.insert
		(make_pair(file_path(sp.string()),
			   file_path(dst.branch_path().string())));
	    }
	  else
	    {
	      L(F("skipping uncertain directory rename %s -> %s\n")
		% sp.string() % dst.branch_path().string());
	    }
	  
	}
    }
}

// this is a 3-way merge algorithm on manifests.

bool merge3(manifest_map const & ancestor,
	    manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged,
	    rename_set & left_renames,
	    rename_set & right_renames)
{
  // in phase #1 we make a bunch of indexes of the changes we saw on each
  // edge.

  patch_set left_edge, right_edge, merged_edge;
  manifests_to_patch_set(ancestor, left, app, left_edge);
  manifests_to_patch_set(ancestor, right, app, right_edge);

  // this is an unusual map of deltas, in that the "path" field of the
  // patch_delta is used to store the ancestral path of the delta, and the
  // key of this map is used to store the merged path of the delta (in
  // cases where the delta happened along with a merge, these differ)
  map<file_path, patch_delta> 
    left_delta_map, right_delta_map;

  map<file_path, file_path> 
    left_move_map, right_move_map,
    left_dir_move_map, right_dir_move_map;

  map<file_path, file_id> 
    left_add_map, right_add_map;

  set<file_path> 
    left_adds, right_adds,
    left_move_srcs, right_move_srcs,
    left_move_dsts, right_move_dsts,
    left_deltas, right_deltas;


  for(set<patch_addition>::const_iterator a = left_edge.f_adds.begin(); 
      a != left_edge.f_adds.end(); ++a)
    left_adds.insert(a->path);
  
  for(set<patch_addition>::const_iterator a = right_edge.f_adds.begin(); 
      a != right_edge.f_adds.end(); ++a)
    right_adds.insert(a->path);


  for(set<patch_move>::const_iterator m = left_edge.f_moves.begin(); 
      m != left_edge.f_moves.end(); ++m)
    {
      left_move_srcs.insert(m->path_old);
      left_move_dsts.insert(m->path_new);
      left_move_map.insert(make_pair(m->path_old, m->path_new));
    }

  for(set<patch_move>::const_iterator m = right_edge.f_moves.begin(); 
      m != right_edge.f_moves.end(); ++m)
    {
      right_move_srcs.insert(m->path_old);
      right_move_dsts.insert(m->path_new);
      right_move_map.insert(make_pair(m->path_old, m->path_new));
    }

  for(set<patch_delta>::const_iterator d = left_edge.f_deltas.begin();
      d != left_edge.f_deltas.end(); ++d)
    {
      left_deltas.insert(d->path);
      left_delta_map.insert(make_pair(d->path, *d));
    }

  for(set<patch_delta>::const_iterator d = right_edge.f_deltas.begin();
      d != right_edge.f_deltas.end(); ++d)
    {
      right_deltas.insert(d->path);
      right_delta_map.insert(make_pair(d->path, *d));
    }


  // phase #2 is a sort of "pre-processing" phase, in which we handle
  // "interesting" move cases:
  // 
  //   - locating any moves which moved *directories* and modifying any adds
  //     in the opposing edge which have the source directory as input, to go
  //     to the target directory (not yet implemented)
  //
  //   - locating any moves between non-equal ids. these are move+edit
  //     events, so we degrade them to a move + an edit (on the move target).
  //     note that in the rest of this function, moves *must* therefore be
  //     applied before edits. just in this function (specifically phase #6)
  //     

  // find any left-edge directory renames 
  infer_directory_moves(ancestor, left, left_move_map, left_dir_move_map);
  infer_directory_moves(ancestor, right, right_move_map, right_dir_move_map);
  {
    manifest_map left_new, right_new;
    rebuild_under_directory_moves(left_dir_move_map, right, right_new);
    rebuild_under_directory_moves(right_dir_move_map, left, left_new);
    if (left != left_new || right != right_new)
      {
	L(F("restarting merge under propagated directory renames\n"));
	return merge3(ancestor, left_new, right_new, app, file_merger, merged,
		      left_renames, right_renames);
      }
  }

  // split edit part off of left move+edits 
  for (map<file_path, file_path>::const_iterator mov = left_move_map.begin();
       mov != left_move_map.end(); ++mov)
    {
      manifest_map::const_iterator 
	anc = ancestor.find(mov->first),
	lf = left.find(mov->second);

      if (anc != ancestor.end() && lf != left.end()
	  && ! (anc->second == lf->second))
	{
	  // it's possible this one has already been split by patch_set.cc
	  map<file_path, patch_delta>::const_iterator left_patch;
	  left_patch = left_delta_map.find(lf->first);
	  if (left_patch != left_delta_map.end())
	    {
	      I(left_patch->second.id_new == lf->second);
	    }
	  else
	    {
	      left_delta_map.insert
		(make_pair
		 (lf->first, patch_delta(anc->second, lf->second, anc->first)));
	    }	    
	}
    }

  // split edit part off of right move+edits 
  for (map<file_path, file_path>::const_iterator mov = right_move_map.begin();
       mov != right_move_map.end(); ++mov)
    {
      manifest_map::const_iterator 
	anc = ancestor.find(mov->first),
	rt = right.find(mov->second);

      if (anc != ancestor.end() && rt != right.end()
	  && ! (anc->second == rt->second))
	{
	  // it's possible this one has already been split by patch_set.cc
	  map<file_path, patch_delta>::const_iterator right_patch;
	  right_patch = right_delta_map.find(rt->first);
	  if (right_patch != right_delta_map.end())
	    {
	      I(right_patch->second.id_new == rt->second);
	    }
	  else
	    {
	      right_delta_map.insert
		(make_pair
		 (rt->first, patch_delta(anc->second, rt->second, anc->first)));
	    }
	}
    }

  // phase #3 detects conflicts. any conflicts here -- including those
  // which were a result of the actions taken in phase #2 -- result in an
  // early return.

  try 
    {

      // no adding and deleting the same file
      check_no_intersect (left_adds, right_edge.f_dels, 
			  "left adds", "right dels");
      check_no_intersect (left_edge.f_dels, right_adds, 
			  "left dels", "right adds");


      // no fiddling with the source of a move
      check_no_intersect (left_move_srcs, right_adds, 
			  "left move sources", "right adds");
      check_no_intersect (left_adds, right_move_srcs, 
			  "left adds", "right move sources");

      check_no_intersect (left_move_srcs, right_edge.f_dels, 
			  "left move sources", "right dels");
      check_no_intersect (left_edge.f_dels, right_move_srcs, 
			  "left dels", "right move sources");

      check_no_intersect (left_move_srcs, right_deltas, 
			  "left move sources", "right deltas");
      check_no_intersect (left_deltas, right_move_srcs, 
			  "left deltas", "right move sources");


      // no fiddling with the destinations of a move
      check_no_intersect (left_move_dsts, right_adds, 
			  "left move destinations", "right adds");
      check_no_intersect (left_adds, right_move_dsts, 
			  "left adds", "right move destinations");

      check_no_intersect (left_move_dsts, right_edge.f_dels, 
			  "left move destinations", "right dels");
      check_no_intersect (left_edge.f_dels, right_move_dsts, 
			  "left dels", "right move destinations");

      check_no_intersect (left_move_dsts, right_deltas, 
			  "left move destinations", "right deltas");
      check_no_intersect (left_deltas, right_move_dsts, 
			  "left deltas", "right move destinations");


      // we're not going to do anything clever like chaining moves together
      check_no_intersect (left_move_srcs, right_move_dsts, 
			  "left move sources", "right move destinations");
      check_no_intersect (left_move_dsts, right_move_srcs, 
			  "left move destinations", "right move sources");

      // check specific add-map conflicts
      check_map_inclusion (left_add_map, right_add_map,
			   "left add map", "right add map");
      check_map_inclusion (right_add_map, left_add_map,
			   "right add map", "left add map");

      // check specific move-map conflicts
      check_map_inclusion (left_move_map, right_move_map,
			   "left move map", "right move map");
      check_map_inclusion (right_move_map, left_move_map,
			   "right move map", "left move map");


    }
  catch (conflict & c)
    {
      merged.clear();
      return false;
    }

  // in phase #4 we union all the (now non-conflicting) deletes, adds, and
  // moves into a "merged" edge

  set_union(left_edge.f_adds.begin(), left_edge.f_adds.end(),
	    right_edge.f_adds.begin(), right_edge.f_adds.end(), 
	    inserter(merged_edge.f_adds, merged_edge.f_adds.begin()));

  set_union(left_edge.f_dels.begin(), left_edge.f_dels.end(),
	    right_edge.f_dels.begin(), right_edge.f_dels.end(), 
	    inserter(merged_edge.f_dels, merged_edge.f_dels.begin()));

  set_union(left_edge.f_moves.begin(), left_edge.f_moves.end(),
	    right_edge.f_moves.begin(), right_edge.f_moves.end(), 
	    inserter(merged_edge.f_moves, merged_edge.f_moves.begin()));

  // (phase 4.5, copy the renames into the disjoint rename sets for independent
  // certification in our caller)

  left_renames.clear();
  for (set<patch_move>::const_iterator mv = left_edge.f_moves.begin();
       mv != left_edge.f_moves.end(); ++mv)
    left_renames.insert(make_pair(mv->path_old, mv->path_new));

  right_renames.clear();
  for (set<patch_move>::const_iterator mv = right_edge.f_moves.begin();
       mv != right_edge.f_moves.end(); ++mv)
    right_renames.insert(make_pair(mv->path_old, mv->path_new));

  // in phase #5 we run 3-way file merges on all the files which have 
  // a delta on both edges, and union the results of the 3-way merges with
  // all the deltas which only happen on one edge, and dump all this into
  // the merged edge too.

  try 
    {      
      merge_deltas (left_delta_map, right_delta_map, 
		    file_merger, merged_edge);

      merge_deltas (right_delta_map, left_delta_map, 
		    file_merger, merged_edge);
    }
  catch (conflict & c)
    {
      merged.clear();
      return false;
    }

  // in phase #6 (finally) we copy ancestor into our result, and apply the
  // merged edge to it, mutating it into the merged manifest.

  copy(ancestor.begin(), ancestor.end(), inserter(merged, merged.begin()));

  for (set<file_path>::const_iterator del = merged_edge.f_dels.begin();
       del != merged_edge.f_dels.end(); ++del)
    {
      L(F("applying merged delete of file %s\n") % *del);
      I(merged.find(*del) != merged.end());
      merged.erase(*del);
    }
  
  for (set<patch_move>::const_iterator mov = merged_edge.f_moves.begin();
       mov != merged_edge.f_moves.end(); ++mov)
    {
      L(F("applying merged move of file %s -> %s\n")
	% mov->path_old % mov->path_new);
      I(merged.find(mov->path_new) == merged.end());
      file_id fid = merged[mov->path_old];
      merged.erase(mov->path_old);
      merged.insert(make_pair(mov->path_new, fid));
    }

  for (set<patch_addition>::const_iterator add = merged_edge.f_adds.begin();
       add != merged_edge.f_adds.end(); ++add)
    {
      L(F("applying merged addition of file %s: %s\n")
	% add->path % add->ident);
      I(merged.find(add->path) == merged.end());
      merged.insert(make_pair(add->path, add->ident));
    }

  for (set<patch_delta>::const_iterator delta = merged_edge.f_deltas.begin();
       delta != merged_edge.f_deltas.end(); ++delta)
    {
      if (merged_edge.f_dels.find(delta->path) != merged_edge.f_dels.end())
	{
	  L(F("skipping merged delta on deleted file %s\n") % delta->path);
	  continue;
	}
      L(F("applying merged delta to file %s: %s -> %s\n")
	% delta->path % delta->id_old % delta->id_old);
      I(merged.find(delta->path) != merged.end());
      I(merged[delta->path] == delta->id_old);
      merged[delta->path] = delta->id_new;
    }

  return true;
}


simple_merge_provider::simple_merge_provider(app_state & app) 
  : app(app) {}

void simple_merge_provider::record_merge(file_id const & left_ident, 
					 file_id const & right_ident, 
					 file_id const & merged_ident,
					 file_data const & left_data, 
					 file_data const & merged_data)
{  
  L(F("recording successful merge of %s <-> %s into %s\n")
    % left_ident % right_ident % merged_ident);

  base64< gzip<delta> > merge_delta;
  transaction_guard guard(app.db);

  diff(left_data.inner(), merged_data.inner(), merge_delta);  
  packet_db_writer dbw(app);
  dbw.consume_file_delta (left_ident, merged_ident, file_delta(merge_delta));
  cert_file_ancestor(left_ident, merged_ident, app, dbw);
  cert_file_ancestor(right_ident, merged_ident, app, dbw);
  guard.commit();
}

void simple_merge_provider::get_version(path_id_pair const & pip, file_data & dat)
{
  app.db.get_file_version(pip.ident(), dat);
}

bool simple_merge_provider::try_to_merge_files(path_id_pair const & ancestor,
					       path_id_pair const & left,
					       path_id_pair const & right,
					       path_id_pair & merged)
{
  
  L(F("trying to merge %s <-> %s (ancestor: %s)\n")
    % left.ident() % right.ident() % ancestor.ident());

  if (left.ident() == right.ident() &&
      left.path() == right.path())
    {
      L(F("files are identical\n"));
      merged = left;
      return true;      
    }  

  // check for an existing merge, use it if available
  {
    vector< file<cert> > left_edges, right_edges;
    set< base64<cert_value> > left_children, right_children, common_children;
    app.db.get_file_certs(left.ident(), cert_name(ancestor_cert_name), left_edges);
    app.db.get_file_certs(right.ident(), cert_name(ancestor_cert_name), left_edges);
    for (vector< file<cert> >::const_iterator i = left_edges.begin();
	 i != left_edges.end(); ++i)
      left_children.insert(i->inner().value);

    for (vector< file<cert> >::const_iterator i = right_edges.begin();
	 i != right_edges.end(); ++i)
      right_children.insert(i->inner().value);

    set_intersection(left_children.begin(), left_children.end(),
		     right_children.begin(), right_children.end(),
		     inserter(common_children, common_children.begin()));
      
    L(F("existing merges: %s <-> %s, %d found\n")
      % left.ident() % right.ident() % common_children.size());
    
    if (common_children.size() == 1)
      {
	cert_value unpacked;
	decode_base64(*common_children.begin(), unpacked);
	file_id ident = file_id(unpacked());
	merged.ident(ident);
	merged.path(left.path());
	L(F("reusing existing merge\n"));
	return true;
      }
    L(F("no reusable merge\n"));
  }

  // no existing merges, we'll have to do it ourselves.
  file_data left_data, right_data, ancestor_data;
  data left_unpacked, ancestor_unpacked, right_unpacked, merged_unpacked;
  vector<string> left_lines, ancestor_lines, right_lines, merged_lines;

  this->get_version(left, left_data);
  this->get_version(ancestor, ancestor_data);
  this->get_version(right, right_data);
    
  unpack(left_data.inner(), left_unpacked);
  unpack(ancestor_data.inner(), ancestor_unpacked);
  unpack(right_data.inner(), right_unpacked);

  split_into_lines(left_unpacked(), left_lines);
  split_into_lines(ancestor_unpacked(), ancestor_lines);
  split_into_lines(right_unpacked(), right_lines);
    
  if (merge3(ancestor_lines, 
	     left_lines, 
	     right_lines, 
	     merged_lines))
    {
      hexenc<id> merged_id;
      base64< gzip<data> > packed_merge;
      string tmp;
      
      L(F("internal 3-way merged ok\n"));
      join_lines(merged_lines, tmp);
      calculate_ident(data(tmp), merged_id);
      file_id merged_fid(merged_id);
      pack(data(tmp), packed_merge);

      merged.path(left.path());
      merged.ident(merged_fid);
      record_merge(left.ident(), right.ident(), merged_fid, 
		   left_data, packed_merge);

      return true;
    }
  else if (app.lua.hook_merge3(ancestor_unpacked, left_unpacked, 
			       right_unpacked, merged_unpacked))
    {
      hexenc<id> merged_id;
      base64< gzip<data> > packed_merge;

      L(F("lua merge3 hook merged ok\n"));
      calculate_ident(merged_unpacked, merged_id);
      file_id merged_fid(merged_id);
      pack(merged_unpacked, packed_merge);

      merged.path(left.path());
      merged.ident(merged_fid);
      record_merge(left.ident(), right.ident(), merged_fid, 
		   left_data, packed_merge);
      return true;
    }

  return false;
}

bool simple_merge_provider::try_to_merge_files(path_id_pair const & left,
					       path_id_pair const & right,
					       path_id_pair & merged)
{
  file_data left_data, right_data;
  data left_unpacked, right_unpacked, merged_unpacked;

  L(F("trying to merge %s <-> %s\n")
    % left.ident() % right.ident());

  if (left.ident() == right.ident() &&
      left.path() == right.path())
    {
      L(F("files are identical\n"));
      merged = left;
      return true;      
    }  

  this->get_version(left, left_data);
  this->get_version(right, right_data);
    
  unpack(left_data.inner(), left_unpacked);
  unpack(right_data.inner(), right_unpacked);

  if (app.lua.hook_merge2(left_unpacked, right_unpacked, merged_unpacked))
    {
      hexenc<id> merged_id;
      base64< gzip<data> > packed_merge;
      
      L(F("lua merge2 hook merged ok\n"));
      calculate_ident(merged_unpacked, merged_id);
      file_id merged_fid(merged_id);
      pack(merged_unpacked, packed_merge);
      
      merged.path(left.path());
      merged.ident(merged_fid);
      record_merge(left.ident(), right.ident(), merged_fid, 
		   left_data, packed_merge);
      return true;
    }
  
  return false;
}


// during the "update" command, the only real differences from merging
// are that we take our right versions from the filesystem, not the db,
// and we only record the merges in a transient, in-memory table.

update_merge_provider::update_merge_provider(app_state & app) 
  : simple_merge_provider(app) {}

void update_merge_provider::record_merge(file_id const & left_ident, 
					 file_id const & right_ident,
					 file_id const & merged_ident,
					 file_data const & left_data, 
					 file_data const & merged_data)
{  
  L(F("temporarily recording merge of %s <-> %s into %s\n")
    % left_ident % right_ident % merged_ident);
  I(temporary_store.find(merged_ident) == temporary_store.end());
  temporary_store.insert(make_pair(merged_ident, merged_data));
}

void update_merge_provider::get_version(path_id_pair const & pip, file_data & dat)
{
  if (app.db.file_version_exists(pip.ident()))
      app.db.get_file_version(pip.ident(), dat);
  else
    {
      base64< gzip<data> > tmp;
      file_id fid;
      N(file_exists (pip.path()),
	F("file %s does not exist in working copy") % pip.path());
      read_data(pip.path(), tmp);
      calculate_ident(tmp, fid);
      N(fid == pip.ident(),
	F("file %s in working copy has id %s, wanted %s")
	% pip.path() % fid % pip.ident());
      dat = tmp;
    }
}



// the remaining part of this file just handles printing out unidiffs for
// the case where someone wants to *read* a diff rather than apply it.

struct unidiff_hunk_writer : public hunk_consumer
{
  vector<string> const & a;
  vector<string> const & b;
  size_t ctx;
  ostream & ost;
  size_t a_begin, b_begin, a_len, b_len;
  vector<string> hunk;
  unidiff_hunk_writer(vector<string> const & a,
		      vector<string> const & b,
		      size_t ctx,
		      ostream & ost);
  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~unidiff_hunk_writer() {}
};

unidiff_hunk_writer::unidiff_hunk_writer(vector<string> const & a,
					 vector<string> const & b,
					 size_t ctx,
					 ostream & ost)
: a(a), b(b), ctx(ctx), ost(ost),
  a_begin(0), b_begin(0), 
  a_len(0), b_len(0)
{}

void unidiff_hunk_writer::insert_at(size_t b_pos)
{
  b_len++;
  hunk.push_back(string("+") + b[b_pos]);
}

void unidiff_hunk_writer::delete_at(size_t a_pos)
{
  a_len++;
  hunk.push_back(string("-") + a[a_pos]);  
}

void unidiff_hunk_writer::flush_hunk(size_t pos)
{
  if (hunk.size() > ctx)
    {
      // insert trailing context
      size_t a_pos = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (a_pos + i < a.size()); ++i)
	{	  
	  hunk.push_back(string(" ") + a[a_pos + i]);
	  a_len++;
	  b_len++;
	}
    }

  if (hunk.size() > 0)
    {
      
      // write hunk to stream
      ost << "@@ -" << a_begin+1;
      if (a_len > 1)
	ost << "," << a_len;
      
      ost << " +" << b_begin+1;
      if (b_len > 1)
    ost << "," << b_len;
      ost << " @@" << endl;
      
      copy(hunk.begin(), hunk.end(), ostream_iterator<string>(ost, "\n"));
    }
  
  // reset hunk
  hunk.clear();
  int skew = b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void unidiff_hunk_writer::advance_to(size_t newpos)
{
  if (a_begin + a_len + (2 * ctx) < newpos)
    {
      flush_hunk(newpos);

      // insert new leading context
      if (newpos - ctx < a.size())
	{
	  for (int i = ctx; i > 0; --i)
	    {
	      if (newpos - i < 0)
		continue;
	      hunk.push_back(string(" ") + a[newpos - i]);
	      a_begin--; a_len++;
	      b_begin--; b_len++;
	    }
	}
    }
  else
    {
      // pad intermediate context
      while(a_begin + a_len < newpos)
	{
	  hunk.push_back(string(" ") + a[a_begin + a_len]);
	  a_len++;
	  b_len++;	  
	}
    }
}


void unidiff(string const & filename1,
	     string const & filename2,
	     vector<string> const & lines1,
	     vector<string> const & lines2,
	     ostream & ost)
{
  ost << "--- " << filename1 << endl;
  ost << "+++ " << filename2 << endl;  

  vector<long> left_interned;  
  vector<long> right_interned;  
  vector<long> lcs;  

  interner<long> in;

  left_interned.reserve(lines1.size());
  for (vector<string>::const_iterator i = lines1.begin();
       i != lines1.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(lines2.size());
  for (vector<string>::const_iterator i = lines2.begin();
       i != lines2.end(); ++i)
    right_interned.push_back(in.intern(*i));

  lcs.reserve(std::min(lines1.size(),lines2.size()));
  longest_common_subsequence(left_interned.begin(), left_interned.end(),
			     right_interned.begin(), right_interned.end(),
			     std::min(lines1.size(), lines2.size()),
			     back_inserter(lcs));

  unidiff_hunk_writer hunks(lines1, lines2, 3, ost);
  walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"
#include <boost/lexical_cast.hpp>
#include "randomfile.hh"

using boost::lexical_cast;

static void dump_incorrect_merge(vector<string> const & expected,
				 vector<string> const & got,
				 string const & prefix)
{
  size_t mx = expected.size();
  if (mx < got.size())
    mx = got.size();
  for (size_t i = 0; i < mx; ++i)
    {
      cerr << "bad merge: " << i << " [" << prefix << "]\t";
      
      if (i < expected.size())
	cerr << "[" << expected[i] << "]\t";
      else
	cerr << "[--nil--]\t";
      
      if (i < got.size())
	cerr << "[" << got[i] << "]\t";
      else
	cerr << "[--nil--]\t";
      
      cerr << endl;
    }
}

// regression blockers go here
static void unidiff_append_test()
{
  string src(string("#include \"hello.h\"\n")
	     + "\n"
	     + "void say_hello()\n"
	     + "{\n"
	     + "        printf(\"hello, world\\n\");\n"
	     + "}\n"
	     + "\n"
	     + "int main()\n"
	     + "{\n"
	     + "        say_hello();\n"
	     + "}\n");
  
  string dst(string("#include \"hello.h\"\n")
	     + "\n"
	     + "void say_hello()\n"
	     + "{\n"
	     + "        printf(\"hello, world\\n\");\n"
	     + "}\n"
	     + "\n"
	     + "int main()\n"
	     + "{\n"
	     + "        say_hello();\n"
	     + "}\n"
	     + "\n"
	     + "void say_goodbye()\n"
	     + "{\n"
	     + "        printf(\"goodbye\\n\");\n"
	     + "}\n"
	     + "\n");
  
  string ud(string("--- hello.c\n")
	    + "+++ hello.c\n"
	    + "@@ -9,3 +9,9 @@\n"
	    + " {\n"
	    + "         say_hello();\n"
	    + " }\n"
	    + "+\n"
	    + "+void say_goodbye()\n"
	    + "+{\n"
	    + "+        printf(\"goodbye\\n\");\n"
	    + "+}\n"
	    + "+\n");

  vector<string> src_lines, dst_lines;
  split_into_lines(src, src_lines);
  split_into_lines(dst, dst_lines);
  stringstream sst;
  unidiff("hello.c", "hello.c", src_lines, dst_lines, sst);
  BOOST_CHECK(sst.str() == ud);  
}


// high tech randomizing test

static void randomizing_merge_test()
{
  for (int i = 0; i < 30; ++i)
    {
      vector<string> anc, d1, d2, m1, m2, gm;

      file_randomizer::build_random_fork(anc, d1, d2, gm,
					 i * 1023, (10 + 2 * i));      

      BOOST_CHECK(merge3(anc, d1, d2, m1));
      if (gm != m1)
	dump_incorrect_merge (gm, m1, "random_merge 1");
      BOOST_CHECK(gm == m1);

      BOOST_CHECK(merge3(anc, d2, d1, m2));
      if (gm != m2)
	dump_incorrect_merge (gm, m2, "random_merge 2");
      BOOST_CHECK(gm == m2);
    }
}


// old boring tests

static void merge_prepend_test()
{
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  for (int i = 0; i < 10; ++i)
    {
      anc.push_back(lexical_cast<string>(i));
      d1.push_back(lexical_cast<string>(i));
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_prepend 1");
  BOOST_CHECK(gm == m1);


  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_prepend 2");
  BOOST_CHECK(gm == m2);
}


static void merge_append_test()
{
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 0; i < 10; ++i)
      anc.push_back(lexical_cast<string>(i));

  d1 = anc;
  d2 = anc;
  gm = anc;

  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_append 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_append 2");
  BOOST_CHECK(gm == m2);


}

static void merge_additions_test()
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI don't like spam\nI like orange juice\nI like toast");
  string confl("I like oatmeal\nI don't like tuna\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like orange juice\nI don't like tuna\nI like toast");
  string good_merge("I like oatmeal\nI don't like spam\nI like orange juice\nI don't like tuna\nI like toast");
  vector<string> anc, d1, cf, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(confl, cf);
  split_into_lines(desc2, d2);
  split_into_lines(good_merge, gm);
  
  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_addition 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_addition 2");
  BOOST_CHECK(gm == m2);

  BOOST_CHECK(!merge3(anc, d1, cf, m1));
}


static void merge_deletions_test()
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like toast");

  vector<string> anc, d1, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc2, d2);
  d1 = anc;
  gm = d2;

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_deletion 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_deletion 2");
  BOOST_CHECK(gm == m2);
}


void add_diff_patch_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&unidiff_append_test));
  suite->add(BOOST_TEST_CASE(&merge_prepend_test));
  suite->add(BOOST_TEST_CASE(&merge_append_test));
  suite->add(BOOST_TEST_CASE(&merge_additions_test));
  suite->add(BOOST_TEST_CASE(&merge_deletions_test));
  suite->add(BOOST_TEST_CASE(&randomizing_merge_test));
}


#endif // BUILD_UNIT_TESTS
