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


// utility class which performs the merge

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

struct conflict {};

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

  if (!(insert_here || delete_here))
    {
      apos++; lpos++;
    }
  
  else if (delete_here)
    {
      if (idx(ancestor,ap) == idx(left,lpos))
	{apos++; lpos++;}
      
      else
	{
	  L(F("delete conflict type 1 -- delete "
	      "(apos = %d, lpos = %d, translated apos = %d)\n")
	    % apos % lpos
	    % idx(ancestor_to_leftpos_map,apos));
	  L(F("trying to delete '%s', with '%s' conflicting\n") 
	    % idx(ancestor,ap) % idx(left,lpos));
	  throw conflict();
	}
    }
  
  else
    {
      L(F("delete conflict type 1 -- insert "
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


bool merge3(vector<string> const & ancestor,
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


// this version does a merge3 on manifests. it's somewhat easier than the
// line-oriented version because manifests are set-theoretic. though, since
// it's "easier" we put it all in one function, and the funtion is thereby
// somewhat long-winded. at least it's not spread across 4 classes :)
//
// nb: I totally made this algorithm up. there's no reason to believe
// it's the best way of merging manifests. you can do more clever things
// wrt. detecting moves of files 

bool merge3(manifest_map const & ancestor,
	    manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged)
{

  copy(right.begin(), right.end(), inserter(merged, merged.begin()));

  patch_set ps;
  manifests_to_patch_set(ancestor, left, app, ps);

  ///////////////////////////
  // STAGE 1: process deletes
  ///////////////////////////

  for (set<file_path>::const_iterator del = ps.f_dels.begin();
       del != ps.f_dels.end(); ++del)
    {
      L(F("merging delete %s...") % (*del));
      merged.erase(*del);
      L(F("OK\n"));
    }

  if (ps.f_dels.size() > 0)
    L(F("merged %d deletes\n") % ps.f_dels.size());

  /////////////////////////
  // STAGE 2: process moves
  /////////////////////////

  for (set<patch_move>::const_iterator mov = ps.f_moves.begin();
       mov != ps.f_moves.end(); ++mov)
    {      
      if (merged.find(mov->path_old) != merged.end())
	{
	  L(F("merging move %s -> %s...") 
	    % mov->path_old % mov->path_new);
	  file_id ident = merged[mov->path_old];
	  merged.erase(mov->path_old);
	  merged.insert(make_pair(mov->path_new, ident));
	  L(F("OK\n"));
	}
      else
	{
	  // file to move is not where it should be. maybe they moved it
	  // the same way, too?
	  if (merged.find(mov->path_new) != merged.end())
	    {
	      if (merged.find(mov->path_new)->second 
		  == left.find(mov->path_new)->second)
		{
		  // they moved it to the same destination, no problem.
		  L(F("skipping duplicate move %s -> %s\n")
		    % mov->path_old % mov->path_new);
		}
	      else
		{
		  // they moved it to a different file at the same destination. try to merge.
 		  L(F("attempting to merge conflicting moves of %s -> %s\n")
		    % mov->path_old % mov->path_new);

		  path_id_pair a_pip = path_id_pair(ancestor.find(mov->path_old()));
		  path_id_pair l_pip = path_id_pair(left.find(mov->path_new()));
		  path_id_pair r_pip = path_id_pair(merged.find(mov->path_new()));
		  path_id_pair m_pip;
		  if (file_merger.try_to_merge_files(a_pip, l_pip, r_pip, m_pip))
		    {
		      L(F("conflicting moves of %s -> %s merged OK\n")
			% mov->path_old % mov->path_new);
		      merged[mov->path_new()] = m_pip.ident();
		    }
		  else
		    {
		      L(F("conflicting moves of %s -> %s, merge failed\n")
			% mov->path_old % mov->path_new);
		      merged.clear();
		      return false;
		    }
		}
	    }
	  else
	    {
	      // no, they moved it somewhere else, or deleted it. either
	      // way, this is a conflict.
	      L(F("conflicting move %s -> %s: no source file present\n")
		% mov->path_old %  mov->path_new);
	      merged.clear();
	      return false;
	    }
	}
    }

  if (ps.f_moves.size() > 0)
    L(F("merged %d moves\n") % ps.f_moves.size());


  ////////////////////////
  // STAGE 3: process adds
  ////////////////////////

  for(set<patch_addition>::const_iterator add = ps.f_adds.begin();
      add != ps.f_adds.end(); ++add)
    {
      if (merged.find(add->path()) == merged.end())
	{
	  L(F("merging addition %s...") % add->path);
	  merged.insert(make_pair(add->path(), add->ident.inner()()));
	  L("OK\n");
	}
      else
	{
	  // there's already a file there.
	  if (merged[add->path()] == add->ident)
	    {
	      // it's ok, they added the same file
	      L(F("skipping duplicate add of %s\n") % add->path);
	    }
	  else
	    {
	      // it's not the same file. try to merge (nb: no ancestor)
	      L(F("attempting to merge conflicting adds of %s\n") % 
		add->path);

	      path_id_pair l_pip = path_id_pair(left.find(add->path));
	      path_id_pair r_pip = path_id_pair(merged.find(add->path));
	      path_id_pair m_pip;
	      if (file_merger.try_to_merge_files(l_pip, r_pip, m_pip))
		{
		  L(F("conflicting adds of %s merged OK\n") % add->path);
		  merged[add->path()] = m_pip.ident();
		}
	      else
		{
		  L(F("conflicting adds of %s, merge failed\n") % add->path);
		  merged.clear();
		  return false;
		}
	    }
	}
    }

  if (ps.f_adds.size() > 0)
    L(F("merged %d adds\n") % ps.f_adds.size());

  //////////////////////////
  // STAGE 4: process deltas
  //////////////////////////

  for (set<patch_delta>::const_iterator delta = ps.f_deltas.begin();
       delta != ps.f_deltas.end(); ++delta)
    {
      if (merged.find(delta->path) != merged.end())
	{
	  if (merged[delta->path] == delta->id_old)
	    {
	      // they did not edit this file, and we did. no problem.
	      L(F("merging delta on %s %s -> %s...")
		% delta->path % delta->id_old % delta->id_new);
	      merged[delta->path] = delta->id_new;
	      L("OK\n");
	    }
	  else if (merged[delta->path] == delta->id_new)
	    {
	      // they edited this file and miraculously made the same
	      // change we did (or we're processing fused edges). no
	      // problem.
	      L(F("ignoring duplicate delta on %s %s -> %s.")
		% delta->path % delta->id_old % delta->id_new);
	    }
	  else
	    {
	      // damn, they modified it too, differently..
	      L(F("attempting to merge conflicting deltas on %s\n") % 
		delta->path);

	      path_id_pair a_pip = path_id_pair(ancestor.find(delta->path()));
	      path_id_pair l_pip = path_id_pair(left.find(delta->path()));
	      path_id_pair r_pip = path_id_pair(merged.find(delta->path()));
	      path_id_pair m_pip;
	      if (file_merger.try_to_merge_files(a_pip, l_pip, r_pip, m_pip))
		{
		  L(F("conflicting deltas on %s merged OK\n")
		    % delta->path);
		  merged[delta->path()] = m_pip.ident();
		}
	      else
		{
		  L(F("conflicting deltas on %s, merge failed\n")
		    % delta->path);
		  merged.clear();
		  return false;
		}

	    }
	}
    }

  if (ps.f_deltas.size() > 0)
    L(F("merged %d deltas\n") % ps.f_deltas.size());

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

void simple_merge_provider::get_right_version(path_id_pair const & pip, file_data & dat)
{
  app.db.get_file_version(pip.ident(),dat);
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

  app.db.get_file_version(left.ident(), left_data);
  app.db.get_file_version(ancestor.ident(), ancestor_data);
  // virtual call; overridden later in the "update" command
  this->get_right_version(right, right_data);
    
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

  app.db.get_file_version(left.ident(), left_data);
  // virtual call; overridden later in the "update" command
  this->get_right_version(right, right_data);
    
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

void update_merge_provider::get_right_version(path_id_pair const & pip, file_data & dat)
{
  base64< gzip<data> > tmp;
  read_data(pip.path(), tmp);
  dat = tmp;
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
