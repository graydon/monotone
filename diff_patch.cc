// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iterator>
#include <vector>
#include <string>
#include <iostream>

#include "boost/sequence_algo/longest_common_subsequence.hpp"

#include "diff_patch.hh"
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

void walk_hunk_consumer(vector<string> lcs,
			vector<string> const & lines1,
			vector<string> const & lines2,			
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
      for (vector<string>::iterator i = lcs.begin();
	   i != lcs.end(); ++i, ++a, ++b)
	{      
	  if (lines1[a] == *i && lines2[b] == *i)
	    continue;
	  cons.advance_to(a);
	  while (lines1[a] != *i)
	    cons.delete_at(a++);
	  while (lines2[b] != *i)
	    cons.insert_at(b++);
	}
      if (b < lines2.size())
	{
	  cons.advance_to(a);
	  while(b < lines2.size())
	    cons.insert_at(b++);
	}
      cons.flush_hunk(a);
    }  
}


// helper class which calculates the offset table

struct hunk_offset_calculator : public hunk_consumer
{
  vector<size_t> & leftpos;
  size_t curr;
  size_t final;
  hunk_offset_calculator(vector<size_t> & lp, size_t fin);
  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~hunk_offset_calculator();
};

hunk_offset_calculator::hunk_offset_calculator(vector<size_t> & off, size_t fin) 
  : leftpos(off), curr(0), final(fin)
{}

hunk_offset_calculator::~hunk_offset_calculator()
{
  //   L("destructing.. filling out to past-the-end: %d\n", final);
  size_t end = leftpos.empty() ? 0 : leftpos.back();
  while(leftpos.size() < final)
    leftpos.push_back(++end);

  //    for (size_t i = 0; i < leftpos.size(); ++i)
  //      {
  //        L("leftpos[%d] == %d\n", i, leftpos[i]);
  //      }
}

void hunk_offset_calculator::flush_hunk(size_t pos)
{
  I(curr >= 0);
  this->advance_to(pos);
}

void hunk_offset_calculator::advance_to(size_t newpos)
{
  while(curr < newpos)
    {
      //       L("advance to %d: curr=%d -> curr=%d\n", newpos, curr, curr+1);
      I(curr >= 0);
      leftpos.push_back(curr++);
    }
}

void hunk_offset_calculator::insert_at(size_t b_pos)
{
  //   L("insert at %d: curr=%d -> curr=%d\n", b_pos, curr, curr+2);
  //
  //   REVISIT: I don't know why this invariant was here in the
  //            first place. I'm sketchy on this whole algorithm.
  //            I've had to disable it due to some test cases
  //            which clearly invalidate it, but it seems like, er,
  //            rather important if it's supposed to be true.
  //   I(curr == b_pos);
  I(curr >= 0);
  curr++;
  leftpos.push_back(curr++);
}

void hunk_offset_calculator::delete_at(size_t a_pos)
{
  // L("delete at %d: curr=%d -> curr=%d\n", a_pos, curr, curr+1);
  I(curr >= 0);
  leftpos.push_back(curr);
}

void calculate_hunk_offsets(vector<string> const & ancestor,
			    vector<string> const & left,
			    vector<size_t> & leftpos)
{
  vector<string> lcs;  
  boost::longest_common_subsequence<size_t>(ancestor.begin(), ancestor.end(),
					    left.begin(), left.end(),
					    back_inserter(lcs));
  leftpos.clear();
  hunk_offset_calculator calc(leftpos, ancestor.size());
  walk_hunk_consumer(lcs, ancestor, left, calc);
}


// utility class which performs the merge

struct hunk_merger : public hunk_consumer
{
  vector<string> const & left;
  vector<string> const & ancestor;
  vector<string> const & right;
  vector<size_t> const & leftpos;
  vector<string> & merged;
  size_t curr_apos;
  hunk_merger(vector<string> const & lft,
	      vector<string> const & anc,
	      vector<string> const & rght,
	      vector<size_t> const & lpos,
	      vector<string> & mrgd);	      
  virtual void flush_hunk(size_t apos);
  virtual void advance_to(size_t apos);
  virtual void insert_at(size_t rpos);
  virtual void delete_at(size_t apos);
  virtual ~hunk_merger() {}
};

struct conflict {};

hunk_merger::hunk_merger(vector<string> const & lft,
			 vector<string> const & anc,
			 vector<string> const & rght,
			 vector<size_t> const & lpos,
			 vector<string> & mrgd)
  : left(lft), ancestor(anc), right(rght), 
    leftpos(lpos), merged(mrgd),
    curr_apos(0)
{
  merged.clear();
}

void hunk_merger::flush_hunk(size_t apos)
{
  advance_to(apos);
}

void hunk_merger::advance_to(size_t apos)
{
  //   L("advancing merger to ancestor pos %d (curr_apos = %d)\n", apos, curr_apos);
  I(apos >= 0);
  I(curr_apos >= 0);
  if (apos < leftpos.size())
    {
      // advancing to something which had a coord in the ancestor
      I(curr_apos <= apos);
      size_t curr_left = leftpos[curr_apos];
      while(curr_left < leftpos[apos])
	{
	  // 	  L("copying from curr_left == %d\n", curr_left);
	  merged.push_back(left[curr_left++]);
	}
      curr_apos = apos;
    }
  else
    {
      // advancing to something which didn't (i.e. the end of the descendent)
      size_t curr_left = leftpos.empty() ? 0 : leftpos.back();
      while(curr_left < left.size())
	{
	  // 	  L("copying tail from curr_left == %d\n", curr_left);
	  merged.push_back(left[curr_left++]);
	}
    }
}

void hunk_merger::insert_at(size_t rpos)
{
  //   L("insertion at right pos %d / %d (curr_apos == %d)\n", rpos, right.size(), curr_apos);
  I(rpos < right.size());
  I(curr_apos < leftpos.size());
  if (curr_apos > 0 && leftpos[curr_apos] > 0)
    {
      // nonzero apos means we might be on a line which was, itself, an insertion. we must
      // check to see if it was, and if so whether it was the same as what we're inserting
      // presently from the right side. this is the first kind of conflict.
      if (ancestor[curr_apos] != left[leftpos[curr_apos-1]+1])
	{
	  // aha! by backing up 1 apos, mapping to left, and advancing, we do *not*
	  // stay still. therefore there was an insert here too. check to see if it is
	  // identical.

	  // 	  if (left[leftpos[curr_apos-1]+1] != right[rpos])
	  // 	    L("insert conflict: ancestor[%d] != left[%d] and also != right[%d]\n", 
	  // 	      curr_apos, leftpos[curr_apos-1]+1, rpos);
	  throw conflict();
	}
    }
  merged.push_back(right[rpos]);
}

void hunk_merger::delete_at(size_t apos)
{
  
  //   L("deletion at ancestor pos %d\n", apos);
  I(apos < leftpos.size());
  //   L("  == left pos %d / %d, leftpos[curr_apos] == %d\n", leftpos[apos], left.size(), leftpos[curr_apos]);
  I(leftpos[curr_apos] < left.size());
  I(curr_apos == apos);
  if (ancestor[apos] != left[leftpos[curr_apos]])
    {
      // the second kind of conflict is when you are processing a delete from the right hand
      // edge, and you find that the context on the left hand edge wasn't constant from ancestor 
      // to left. this means you're not safe to delete it.
      //       L("delete conflict: ancestor[%d] != left[%d]\n", apos, leftpos[apos]);
      throw conflict();
    }
  curr_apos++;
}


void merge_hunks_via_offsets(vector<string> const & left,
			     vector<string> const & ancestor,
			     vector<string> const & right,
			     vector<size_t> const & leftpos,
			     vector<string> & merged)
{
  vector<string> lcs;  
  boost::longest_common_subsequence<size_t>(ancestor.begin(), ancestor.end(),
					    right.begin(), right.end(),
					    back_inserter(lcs));
  hunk_merger merger(left, ancestor, right, leftpos, merged);
  walk_hunk_consumer(lcs, ancestor, right, merger);
}


bool merge3(vector<string> const & ancestor,
	    vector<string> const & left,
	    vector<string> const & right,
	    vector<string> & merged)
{
  try 
    {
      vector<size_t> leftpos;
      L("calculating offsets from ancestor:[%d..%d) to left:[%d..%d)\n",
	0, ancestor.size(), 0, left.size());
      calculate_hunk_offsets(ancestor, left, leftpos);
      L("merging differences from ancestor:[%d..%d) to right:[%d..%d)\n",
	0, ancestor.size(), 0, right.size());
      merge_hunks_via_offsets(left, ancestor, right, leftpos, merged);
    }
  catch(conflict & c)
    {
      L("conflict detected. no merge.\n");
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
	  L("merging existant versions of %s in both manifests\n",
	    l_pip.path()().c_str());
	  if (file_merger.try_to_merge_files(l_pip, r_pip, m_pip))
	    {
	      L("arrived at merged version %s\n",
		m_pip.ident().inner()().c_str());
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
      L("merging delete %s...", (*del)().c_str());
      merged.erase(*del);
      L("OK\n");
    }

  if (ps.f_dels.size() > 0)
    L("merged %d deletes\n", ps.f_dels.size());

  /////////////////////////
  // STAGE 2: process moves
  /////////////////////////

  for (set<patch_move>::const_iterator mov = ps.f_moves.begin();
       mov != ps.f_moves.end(); ++mov)
    {      
      if (merged.find(mov->path_old) != merged.end())
	{
	  L("merging move %s -> %s...",
	    mov->path_old().c_str(), mov->path_new().c_str());
	  file_id ident = merged[mov->path_old];
	  merged.erase(mov->path_old);
	  merged.insert(make_pair(mov->path_new, ident));
	  L("OK\n");
	}
      else
	{
	  // file to move is not where it should be. maybe they moved it
	  // the same way, too?
	  if (merged.find(mov->path_new) != merged.end())
	    {
	      if (merged.find(mov->path_new)->second == left.find(mov->path_new)->second)
		{
		  // they moved it to the same destination, no problem.
		  L("skipping duplicate move %s -> %s\n",
		    mov->path_old().c_str(), mov->path_new().c_str());
		}
	      else
		{
		  // they moved it to a different file at the same destination. try to merge.
 		  L("attempting to merge conflicting moves of %s -> %s\n",
		    mov->path_old().c_str(), mov->path_new().c_str());

		  path_id_pair a_pip = path_id_pair(ancestor.find(mov->path_old()));
		  path_id_pair l_pip = path_id_pair(left.find(mov->path_new()));
		  path_id_pair r_pip = path_id_pair(merged.find(mov->path_new()));
		  path_id_pair m_pip;
		  if (file_merger.try_to_merge_files(a_pip, l_pip, r_pip, m_pip))
		    {
		      L("conflicting moves of %s -> %s merged OK\n",
			mov->path_old().c_str(), mov->path_new().c_str());
		      merged[mov->path_new()] = m_pip.ident();
		    }
		  else
		    {
		      L("conflicting moves of %s -> %s, merge failed\n",
			mov->path_old().c_str(), mov->path_new().c_str());
		      merged.clear();
		      return false;
		    }
		}
	    }
	  else
	    {
	      // no, they moved it somewhere else, or deleted it. either
	      // way, this is a conflict.
	      L("conflicting move %s -> %s: no source file present\n",
		mov->path_old().c_str(), mov->path_new().c_str());
	      merged.clear();
	      return false;
	    }
	}
    }

  if (ps.f_moves.size() > 0)
    L("merged %d moves\n", ps.f_moves.size());


  ////////////////////////
  // STAGE 3: process adds
  ////////////////////////

  for(set<patch_addition>::const_iterator add = ps.f_adds.begin();
      add != ps.f_adds.end(); ++add)
    {
      if (merged.find(add->path()) == merged.end())
	{
	  L("merging addition %s...", add->path().c_str());
	  merged.insert(make_pair(add->path(), add->ident.inner()()));
	  L("OK\n");
	}
      else
	{
	  // there's already a file there.
	  if (merged[add->path()] == add->ident)
	    {
	      // it's ok, they added the same file
	      L("skipping duplicate add of %s\n", add->path().c_str());
	    }
	  else
	    {
	      // it's not the same file. try to merge (nb: no ancestor)
	      L("attempting to merge conflicting adds of %s\n", 
		add->path().c_str());

	      path_id_pair l_pip = path_id_pair(left.find(add->path));
	      path_id_pair r_pip = path_id_pair(merged.find(add->path));
	      path_id_pair m_pip;
	      if (file_merger.try_to_merge_files(l_pip, r_pip, m_pip))
		{
		  L("conflicting adds of %s merged OK\n",
		    add->path().c_str());
		  merged[add->path()] = m_pip.ident();
		}
	      else
		{
		  L("conflicting adds of %s, merge failed\n",
		    add->path().c_str());
		  merged.clear();
		  return false;
		}
	    }
	}
    }

  if (ps.f_adds.size() > 0)
    L("merged %d adds\n", ps.f_adds.size());

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
	      L("merging delta on %s %s -> %s...", 
		delta->path().c_str(), 
		delta->id_old.inner()().c_str(),
		delta->id_new.inner()().c_str());
	      merged[delta->path] = delta->id_new;
	      L("OK\n");
	    }
	  else
	    {
	      // damn, they modified it too
	      L("attempting to merge conflicting deltas on %s\n", 
		delta->path().c_str());

	      path_id_pair a_pip = path_id_pair(ancestor.find(delta->path()));
	      path_id_pair l_pip = path_id_pair(left.find(delta->path()));
	      path_id_pair r_pip = path_id_pair(merged.find(delta->path()));
	      path_id_pair m_pip;
	      if (file_merger.try_to_merge_files(a_pip, l_pip, r_pip, m_pip))
		{
		  L("conflicting deltas on %s merged OK\n",
		    delta->path().c_str());
		  merged[delta->path()] = m_pip.ident();
		}
	      else
		{
		  L("conflicting deltas on %s, merge failed\n",
		    delta->path().c_str());
		  merged.clear();
		  return false;
		}

	    }
	}
    }

  if (ps.f_deltas.size() > 0)
    L("merged %d deltas\n", ps.f_deltas.size());

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
  L("recording successful merge of %s <-> %s into %s\n",
    left_ident.inner()().c_str(),
    right_ident.inner()().c_str(),
    merged_ident.inner()().c_str());

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
  
  L("trying to merge %s <-> %s (ancestor: %s)\n",
    left.ident().inner()().c_str(),
    right.ident().inner()().c_str(),
    ancestor.ident().inner()().c_str());
  
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
      
    L("existing merges: %s <-> %s, %d found\n",
      left.ident().inner()().c_str(),
      right.ident().inner()().c_str(),
      common_children.size());
      
    if (common_children.size() == 1)
      {
	cert_value unpacked;
	decode_base64(*common_children.begin(), unpacked);
	file_id ident = file_id(unpacked());
	merged.ident(ident);
	merged.path(left.path());
	L("reusing existing merge\n");
	return true;
      }
    L("no reusable merge\n");
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
      
      L("internal 3-way merged ok\n");
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

      L("lua merge3 hook merged ok\n");
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

  app.db.get_file_version(left.ident(), left_data);
  // virtual call; overridden later in the "update" command
  this->get_right_version(right, right_data);
    
  unpack(left_data.inner(), left_unpacked);
  unpack(right_data.inner(), right_unpacked);

  if (app.lua.hook_merge2(left_unpacked, right_unpacked, merged_unpacked))
    {
      hexenc<id> merged_id;
      base64< gzip<data> > packed_merge;
      
      L("lua merge2 hook merged ok\n");
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
  L("temporarily recording merge of %s <-> %s into %s\n",
    left_ident.inner()().c_str(),
    right_ident.inner()().c_str(),
    merged_ident.inner()().c_str());  
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
  vector<string> lcs;  
  boost::longest_common_subsequence<size_t>(lines1.begin(), lines1.end(),
					    lines2.begin(), lines2.end(),
					    back_inserter(lcs));
  unidiff_hunk_writer hunks(lines1, lines2, 3, ost);
  walk_hunk_consumer(lcs, lines1, lines2, hunks);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"

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

static void merge_additions_test()
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI don't like spam\nI like orange juice\nI like toast");
  string confl("I like oatmeal\nI don't like tuna\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like orange juice\nI don't like tuna\nI like toast");
  string good_merge("I like oatmeal\nI don't like spam\nI like orange juice\nI don't like tuna\nI like toast");

  vector<string> anc, d1, cf, d2, m, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(confl, cf);
  split_into_lines(desc2, d2);
  split_into_lines(good_merge, gm);
  
  BOOST_CHECK(merge3(anc, d1, d2, m));
  BOOST_CHECK(gm == m);
  BOOST_CHECK(!merge3(anc, d1, cf, m));
}

static void merge_deletions_test()
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like toast");

  vector<string> anc, d1, d2, m;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(desc2, d2);
  
  BOOST_CHECK(merge3(anc, d1, d2, m));
  BOOST_CHECK(d2 == m);
}


void add_diff_patch_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&unidiff_append_test));
  suite->add(BOOST_TEST_CASE(&merge_additions_test));
  suite->add(BOOST_TEST_CASE(&merge_deletions_test));
}


#endif // BUILD_UNIT_TESTS
