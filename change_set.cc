// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iterator>
#include <iostream>
#include <list>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "interner.hh"
#include "sanity.hh"

/*

path_states:
------------

a tid is a "transient identity", an integer generated from a
"tid source", which is just a counter.

tid 0 is the root tid, the parent of entries in the root directory

a path_item is a triple (parent_tid,type,name), where name is a 1-element
file_path (a single path component). the empty name is also called the
"null name".

a bijection between tids and path_items is called a 
path_state, when these conditions are met:
  - the parent_tid links form a proper tree rooted at "tid 0"
  - no path_items actually *have* tid 0; it is a fantasy
  - any tid referenced in a parent_tid link is a directory
  - all basenames with a particular parent_tid are unique

a pair of path_states (p0,p1) is called a path_rearrangement, 
when these conditions are met:
  - every tid which exists in p0 exists in p1, and vice versa
  - every tid in p0 has the same type in p1, and vice versa


path_edit_lists:
----------------

a path_edit_list is an ordered sequence of add, delete, and rename
commands. 

a path_rearrangement is isomorphic to a subset of all path_edit_lists,
considering the path_edit_list as "executed imperatively" against a
particular "tid source", such as the ancestral state of a 3-way merge.
this subset is called the "normalized path_edit_lists". here is the
mapping:

  an (add:path) command maps to ((tid, *null*), (tid, path)),
  for a fresh tid. 

  a (del:path) command maps to ((tid, path), (tid, *null*))
  for an existing tid.
 
  a (rename:path1,path2) command maps to:
    ((tid, path1), (tid, path2)) when there is no existing entry for path1
    ((tid, path0), (tid, path2)) when there is an existing entry
    of the form ((tid, path0), (tid, path1))

adds can apply to files alone (creating the directory along the way). 

renames and deletes can apply to directories or files. 

the prerequisite of a rename is that the source directory (one parent above
the renamed tid) exists, along with the renamed tid. the destination
directory need not exist; it will be created along the way.

a path_edit_list subsumes the role a work_set and rename_set played in
previous monotones.

the "normalized" subset is such that, for example, renaming A->B and B->C
collapses to a single rename from A->C. also normalization requires that no
tids be mentionned which are not involved in an actual *change*, this is
achieved by playing back the normalized frorm through its path_edit_list
representation and skipping any path edits which are identities.


change_sets:
------------

a change_set is a path_edit_list and a map from pathname -> delta, 
where each delta is an (id1, id2) pair.

adds are modeled as an add command followed by a delta from the empty
string to the file's full contents.


concatenation:
--------------

two path_rearrangements A and B can be concatenated by renumbering the tids in
B.prestate to match the numbers in A.poststate.

two sets of deltas can be concatenated by replacing (A,B) and (B,C) with (A,C)
on a given tid.

if deltas (A,B) and (C,D) exist on a given tid, they cannot be concatenated.


merger:
-------

two path_rearrangements A and B can be merged iff their prestates and
poststates agree with one another. agreement between states X and Y means:

  - foreach (tid,path) in X: if Y[tid] exists then Y[tid] == path
  - foreach (tid,path) in Y: if X[tid] exists then X[tid] == path

the result of merging two path_rearrangements is the result of unioning the
pre and post states. since they agree on their various intersecting images
this should be harmless.

merging deltas is accomplished through 3-way line-level merging.

*/

static change_set::tid root_tid = 0;
static file_path null_path;

static bool null_name(file_path const & p)
{
  return p().empty();
}

static void
sanity_check_path_item(change_set::path_item const & pi)
{
  fs::path tmp = mkpath(pi.name());
  I(null_name(pi.name) || ++(tmp.begin()) == tmp.end());
}

static void
confirm_proper_tree(change_set::path_state const & ps)
{
  std::set<change_set::tid> confirmed;
  I(ps.find(root_tid) == ps.end());
  for (change_set::path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      change_set::tid curr = i->first;
      change_set::path_item item = i->second;
      std::set<change_set::tid> ancs; 

      while (confirmed.find(curr) == confirmed.end())
	{	      
	  sanity_check_path_item(item);
	  I(ancs.find(curr) == ancs.end());
	  ancs.insert(curr);
	  if (path_item_parent(item) == root_tid)
	    break;
	  else
	    {
	      curr = path_item_parent(item);
	      change_set::path_state::const_iterator j = ps.find(curr);
	      I(j != ps.end());
	      item = path_state_item(j);
	      I(path_item_type(item) == change_set::ptype_directory);
	    }
	}
      std::copy(ancs.begin(), ancs.end(), 
		inserter(confirmed, confirmed.begin()));      
    }
  I(confirmed.find(root_tid) == confirmed.end());
}

static void
confirm_unique_entries_in_directories(change_set::path_state const & ps)
{  
  std::set< std::pair<change_set::tid,file_path> > entries;
  for (change_set::path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<change_set::tid,file_path> p = std::make_pair(i->first, path_item_name(i->second));
      I(entries.find(p) == entries.end());
      entries.insert(p);
    }
}

static void
sanity_check_path_state(change_set::path_state const & ps)
{
  confirm_proper_tree(ps);
  confirm_unique_entries_in_directories(ps);
}

change_set::path_item::path_item(change_set::tid p, change_set::ptype t, file_path const & n) 
  : parent(p), ty(t), name(n) 
{
  sanity_check_path_item(*this);
}

change_set::path_item::path_item(path_item const & other) 
  : parent(other.parent), ty(other.ty), name(other.name) 
{
  sanity_check_path_item(*this);
}

change_set::path_item const & change_set::path_item::operator=(path_item const & other)
{
  parent = other.parent;
  ty = other.ty;
  name = other.name;
  sanity_check_path_item(*this);
  return *this;
}

bool change_set::path_item::operator==(path_item const & other) const
{
  return this->parent == other.parent &&
    this->ty == other.ty &&
    this->name == other.name;
}


static void
check_states_agree(change_set::path_state const & p1,
		   change_set::path_state const & p2)
{
  for (change_set::path_state::const_iterator i = p1.begin(); i != p1.end(); ++i)
    {
      change_set::path_state::const_iterator j = p2.find(i->first);
      I(j != p2.end());
      I(path_item_type(i->second) == path_item_type(j->second));
      //       I(! (null_name(path_item_name(i->second))
      // 	   &&
      // 	   null_name(path_item_name(j->second))));
    }
}

static void
sanity_check_path_rearrangement(change_set::path_rearrangement const & pr)
{
  sanity_check_path_state(pr.first);
  sanity_check_path_state(pr.second);
  check_states_agree(pr.first, pr.second);
  check_states_agree(pr.second, pr.first);
}

static void 
compose_path(std::vector<file_path> const & names,
	     file_path & path)
{
  try
    {      
      std::vector<file_path>::const_iterator i = names.begin();
      I(i != names.end());
      fs::path p = mkpath((*i)());
      ++i;
      for ( ; i != names.end(); ++i)
	p /= mkpath((*i)());
      path = file_path(p.string());
    }
  catch (std::runtime_error &e)
    {
      throw informative_failure(e.what());
    }
}

static void
get_full_path(change_set::path_state const & state,
	      change_set::tid t,
	      std::vector<file_path> & pth)
{
  std::vector<file_path> tmp;
  while(t != root_tid)
    {
      change_set::path_state::const_iterator i = state.find(t);
      I(i != state.end());
      tmp.push_back(path_item_name(i->second));
      t = path_item_parent(i->second);
    }
  pth.clear();
  std::copy(tmp.rbegin(), tmp.rend(), inserter(pth, pth.begin()));
}

static void
get_full_path(change_set::path_state const & state,
	      change_set::tid t,
	      file_path & pth)
{
  std::vector<file_path> tmp;
  get_full_path(state, t, tmp);
  L(F("got %d-entry path for tid %d\n") % tmp.size() % t);
  compose_path(tmp, pth);
}


struct
tid_source
{
  change_set::tid ctr;
  tid_source() : ctr(root_tid + 1) {}
  tid_source(change_set::path_rearrangement pr) : ctr(root_tid) 
  {
    for (change_set::path_state::const_iterator i = pr.first.begin();
	 i != pr.first.end(); ++i)
      ctr = std::max(ctr, path_state_tid(i));
    for (change_set::path_state::const_iterator i = pr.second.begin();
	 i != pr.second.end(); ++i)
      ctr = std::max(ctr, path_state_tid(i));
    ctr++;
  }
  change_set::tid next() { return ctr++; }
};

static void 
analyze_rearrangement(change_set::path_rearrangement const & pr,
		      std::set<file_path> & deleted_files,
		      std::set<file_path> & deleted_dirs,
		      std::map<file_path, file_path> & renamed_files,
		      std::map<file_path, file_path> & renamed_dirs,
		      std::set<file_path> & added_files)
{
  deleted_files.clear();
  deleted_dirs.clear();
  renamed_files.clear();
  renamed_dirs.clear();
  added_files.clear();

  for (change_set::path_state::const_iterator i = pr.first.begin();
       i != pr.first.end(); ++i)
    {      
      change_set::tid curr(path_state_tid(i));
      std::vector<file_path> old_name, new_name;
      file_path old_path, new_path;
     
      change_set::path_state::const_iterator j = pr.second.find(curr);
      I(j != pr.second.end());
      change_set::path_item old_item(path_state_item(i));
      change_set::path_item new_item(path_state_item(j));

      // compose names
      if (!null_name(path_item_name(old_item)))
	{
	  get_full_path(pr.first, curr, old_name);
	  compose_path(old_name, old_path);
	}

      if (!null_name(path_item_name(new_item)))      
	{
	  get_full_path(pr.second, curr, new_name);
	  compose_path(new_name, new_path);
	}

      if (old_path == new_path)
	{
	  L(F("skipping preserved %s %d : '%s'\n")
	    % (path_item_type(old_item) == change_set::ptype_directory ? "directory" : "file")
	    % curr % old_path);
	  continue;
	}
      
      L(F("analyzing %s %d : '%s' -> '%s'\n")
	% (path_item_type(old_item) == change_set::ptype_directory ? "directory" : "file")
	% curr % old_path % new_path);
      
      if (null_name(path_item_name(old_item)))
	{
	  // an addition (which must be a file, not a directory)
	  I(! null_name(path_item_name(new_item)));
	  I(path_item_type(new_item) != change_set::ptype_directory);
	  added_files.insert(new_path);
	}
      else if (null_name(path_item_name(new_item)))
	{
	  // a deletion
	  I(! null_name(path_item_name(old_item)));
	  switch (path_item_type(new_item))
	    {
	    case change_set::ptype_directory:
	      deleted_dirs.insert(old_path);
	      break;
	    case change_set::ptype_file:
	      deleted_files.insert(old_path);
	      break;
	    }	  
	}
      else
	{
	  // a generic rename
	  switch (path_item_type(new_item))
	    {
	    case change_set::ptype_directory:
	      renamed_dirs.insert(std::make_pair(old_path, new_path));
	      break;
	    case change_set::ptype_file:
	      renamed_files.insert(std::make_pair(old_path, new_path));
	      break;
	    }
	}
    }
}

static void 
play_back_analysis(std::set<file_path> const & deleted_files,
		   std::set<file_path> const & deleted_dirs,
		   std::map<file_path, file_path> const & renamed_files,
		   std::map<file_path, file_path> const & renamed_dirs,
		   std::set<file_path> const & added_files,
		   path_edit_consumer & pc)
{
  for (std::set<file_path>::const_iterator i = deleted_files.begin();
       i != deleted_files.end(); ++i)
    pc.delete_file(*i);

  for (std::set<file_path>::const_iterator i = deleted_dirs.begin();
       i != deleted_dirs.end(); ++i)
    pc.delete_dir(*i);

  for (std::map<file_path,file_path>::const_iterator i = renamed_files.begin();
       i != renamed_files.end(); ++i)
    pc.rename_file(i->first, i->second);

  for (std::map<file_path,file_path>::const_iterator i = renamed_dirs.begin();
       i != renamed_dirs.end(); ++i)
    pc.rename_dir(i->first, i->second);

  for (std::set<file_path>::const_iterator i = added_files.begin();
       i != added_files.end(); ++i)
    pc.add_file(*i);
}
			   


void
play_back_rearrangement(change_set::path_rearrangement const & pr,
			path_edit_consumer & pc)
{
  sanity_check_path_rearrangement(pr);

  L(F("playing back path_rearrangement with %d entries\n") 
    % pr.first.size());

  // we copy the replayed state into temporary sets
  // before outputting them. this ensures that the
  // output is canonicalized (each type of change is
  // output in a fixed order, and each change within
  // each type is alphabetical)

  std::set<file_path> deleted_files;
  std::set<file_path> deleted_dirs;
  std::map<file_path, file_path> renamed_files;
  std::map<file_path, file_path> renamed_dirs;
  std::set<file_path> added_files;

  analyze_rearrangement(pr, 
			deleted_files, 
			deleted_dirs,
			renamed_files, 
			renamed_dirs,
			added_files);

  play_back_analysis(deleted_files, 
		     deleted_dirs,
		     renamed_files, 
		     renamed_dirs,
		     added_files, 
		     pc);
}


struct change_set_playback_rearrangement_consumer
  : public path_edit_consumer
{
  change_set::delta_map const & deltas;
  change_set_consumer & csc;
  change_set_playback_rearrangement_consumer(change_set::delta_map const & d,
					     change_set_consumer & csc) 
    : deltas(d), csc(csc) {}
  virtual void add_file(file_path const & a)
  {
    change_set::delta_map::const_iterator i = deltas.find(a);
    I(i != deltas.end());
    csc.add_file(a, delta_entry_dst(i));
  }
  virtual void delete_file(file_path const & d) { csc.delete_file(d); }
  virtual void delete_dir(file_path const & d) { csc.delete_dir(d); }
  virtual void rename_file(file_path const & a, file_path const & b) { csc.rename_file(a, b); }
  virtual void rename_dir(file_path const & a, file_path const & b) { csc.rename_dir(a, b); }
  virtual ~change_set_playback_rearrangement_consumer() {}
};


void
play_back_change_set(change_set const & cs,
		     change_set_consumer & csc)
{
  change_set_playback_rearrangement_consumer csprc(cs.deltas, csc);
  play_back_rearrangement(cs.rearrangement, csprc);
  L(F("playing back %d deltas\n") % cs.deltas.size());
  for (change_set::delta_map::const_iterator i = cs.deltas.begin();
       i != cs.deltas.end(); ++i)
    {
      if (delta_entry_src(i).inner()().empty() 
	  || delta_entry_dst(i).inner()().empty())
	{
	  L(F("skipping delta %s : '%s' -> '%s' ")
	    % delta_entry_path(i)
	    % delta_entry_src(i)
	    % delta_entry_dst(i));
	  continue;
	}
      csc.apply_delta(delta_entry_path(i),
		      delta_entry_src(i),
		      delta_entry_dst(i));
    }
}



// a few of the auxiliary functions here operate on a more "normal"
// representation of a directory tree:
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]
//
// this structure is called a directory_map, and it is local to this file: we
// occasionally build it, do some analysis of its contents, then throw it away
// and revert to the path_state form.

typedef std::map<file_path, std::pair<change_set::ptype,change_set::tid> > directory_node;
typedef std::map<change_set::tid, boost::shared_ptr<directory_node> > directory_map;

static change_set::ptype
directory_entry_type(directory_node::const_iterator const & i)
{
  return i->second.first;
}

static change_set::tid
directory_entry_tid(directory_node::const_iterator const & i)
{
  return i->second.second;
}

static boost::shared_ptr<directory_node>
new_dnode()
{
  return boost::shared_ptr<directory_node>(new directory_node());
}

static boost::shared_ptr<directory_node>
dnode(directory_map & dir, change_set::tid t)
{
  boost::shared_ptr<directory_node> node;
  directory_map::const_iterator dirent = dir.find(t);
  if (dirent == dir.end())
    {      
      node = new_dnode();
      dir.insert(std::make_pair(t, node));
    }
  else
    node = dirent->second;
  return node;
}


  
//
// this takes a path of the form
//
//  "p[0]/p[1]/.../p[n-1]/p[n]"
//
// and fills in a vector of paths corresponding to p[0] ... p[n-1],
// along with a separate "leaf path" for element p[n]. 
//

static void 
split_path(file_path const & p,
	   std::vector<file_path> & components)
{
  components.clear();
  fs::path tmp = mkpath(p());
  std::copy(tmp.begin(), tmp.end(), std::inserter(components, components.begin()));
}

static void 
split_path(file_path const & p,
	   std::vector<file_path> & prefix,
	   file_path & leaf_path)
{
  split_path(p, prefix);
  I(prefix.size() > 0);
  leaf_path = prefix.back();
  prefix.pop_back();
}

/*
static bool
lookup_path(std::vector<file_path> const & pth,
	    directory_map const & dir,
	    change_set::tid & t)
{
  t = root_tid;
  for (std::vector<file_path>::const_iterator i = pth.begin();
       i != pth.end(); ++i)
    {
      directory_map::const_iterator dirent = dir.find(t);
      if (dirent != dir.end())
	{
	  boost::shared_ptr<directory_node> node = dirent->second;
	  directory_node::const_iterator entry = node->find(*i);
	  if (entry == node->end())
	    return false;
	  t = directory_entry_tid(entry);
	}
      else
	return false;
    }
  return true;
}

static bool
lookup_path(file_path const & pth,
	    directory_map const & dir,
	    change_set::tid & t)
{
  std::vector<file_path> vec;
  fs::path tmp = mkpath(pth());
  std::copy(tmp.begin(), tmp.end(), std::inserter(vec, vec.begin()));
  return lookup_path(vec, dir, t);
}
*/

// this takes a path in the path space defined by input_dir and rebuilds it
// in the path space defined by output_space, including any changes to
// parents in the path (rather than directly to the path leaf name).  it
// therefore *always* succeeds; sometimes it does nothing if there's no
// affected parent, but you always get a rebuilt path in the output space.

static void
reconstruct_path(file_path const & input,
		 directory_map const & input_dir,
		 change_set::path_state const & output_space,
		 file_path & output)
{
  std::vector<file_path> vec;
  std::vector<file_path> rebuilt;

  L(F("reconstructing path '%s' under rearrangement\n") % input);
  
  fs::path tmp = mkpath(input());
  std::copy(tmp.begin(), tmp.end(), std::inserter(vec, vec.begin()));

  change_set::tid t = root_tid;
  std::vector<file_path>::const_iterator pth = vec.begin();
  while (pth != vec.end())
    {	  
      directory_map::const_iterator dirent = input_dir.find(t);
      if (dirent == input_dir.end())
	break;
      
      boost::shared_ptr<directory_node> node = dirent->second;
      directory_node::const_iterator entry = node->find(*pth);
      if (entry == node->end())
	break;

      {
	// check to see if this is the image of an added or deleted entry
	// (i.e. null name in output space), if so it terminates our
	// search.
	change_set::path_state::const_iterator i = output_space.find(directory_entry_tid(entry));
	I(i != output_space.end());
	if (null_name(path_item_name(path_state_item(i))))
	  {
	    L(F("input path element '%s' is null in output space, mapping truncated\n") % *pth);
	    break;
	  }
      }
 
      L(F("resolved entry '%s' in reconstruction\n") % *pth);
      ++pth;
      t = directory_entry_tid(entry);

      if (directory_entry_type(entry) != change_set::ptype_directory)
	break;
    }
      
  get_full_path(output_space, t, rebuilt);
  
  while(pth != vec.end())
    {
      L(F("copying tail entry '%s' in reconstruction\n") % *pth);
      rebuilt.push_back(*pth);
      ++pth;
    }

  compose_path(rebuilt, output);
  L(F("reconstructed path '%s' as '%s'\n") % input % output);
}


static void
build_directory_map(change_set::path_state const & state,
		    directory_map & dir)
{
  sanity_check_path_state(state);
  dir.clear();
  L(F("building directory map for %d entries\n") % state.size());
  for (change_set::path_state::const_iterator i = state.begin(); i != state.end(); ++i)
    {
      change_set::tid curr = path_state_tid(i);
      change_set::path_item item = path_state_item(i);
      change_set::tid parent = path_item_parent(item);
      file_path name = path_item_name(item);
      change_set::ptype type = path_item_type(item);	    
      L(F("adding entry %s (%s %d) to directory node %d\n") 
	% name % (type == change_set::ptype_directory ? "dir" : "file") % curr % parent);
      dnode(dir, parent)->insert(std::make_pair(name,std::make_pair(type, curr)));
    }
}

  //
  // this takes a paths of the form
  //
  // (path[0], path[1], ..., path[n-1], path[n])
  // 
  // and returns the tid referring to the element path[n], adding any
  // intermediate directories to the provided directory maps and path
  // states in the process.
  //
  // this is more complicated than it looks, because we are working with
  // imperfect information: the elements of the path prefix may exist or
  // not exist in the maps already; and may have the same tids or different
  // tids.
  //
  // both contexts are extended during resolving as necessary. both
  // contexts should always contain the identical set of tids (at different
  // names, perhaps).
  //

static void 
resolve_path(std::vector<file_path> const & self_path,
	     change_set::tid & self_tid,
	     directory_map & self_dir,
	     directory_map & other_dir,
	     change_set::path_state & self_state,
	     change_set::path_state & other_state,
	     tid_source & ts)
{
  self_tid = root_tid;

  for (std::vector<file_path>::const_iterator p = self_path.begin();
       p != self_path.end(); ++p)
    {
      boost::shared_ptr<directory_node> self_node = dnode(self_dir, self_tid);
      boost::shared_ptr<directory_node> other_node = dnode(other_dir, self_tid);

      directory_node::const_iterator self_entry = self_node->find(*p);
      directory_node::const_iterator other_entry = other_node->find(*p);

      bool found_self = (self_entry != self_node->end());
      bool found_other = (other_entry != other_node->end());

      if (found_self && found_other)
	{
	  I(self_entry->second.first == change_set::ptype_directory);
	  I(other_entry->second.first == change_set::ptype_directory);
	  self_tid = self_entry->second.second;
	}
      else if (found_self)	 
	{
	  I(self_entry->second.first == change_set::ptype_directory);
	  change_set::tid child_tid = self_entry->second.second;
	  other_node->insert(std::make_pair(*p, std::make_pair(change_set::ptype_directory, child_tid)));
	  other_state.insert(std::make_pair(child_tid, change_set::path_item(self_tid, change_set::ptype_directory, *p)));
	  self_tid = child_tid;
	}
      else if (found_other)
	{
	  I(other_entry->second.first == change_set::ptype_directory);
	  change_set::tid child_tid = other_entry->second.second;
	  self_node->insert(std::make_pair(*p, std::make_pair(change_set::ptype_directory, child_tid)));
	  self_state.insert(std::make_pair(child_tid, change_set::path_item(self_tid, change_set::ptype_directory, *p)));
	  self_tid = child_tid;
	}
      else
	{
	  change_set::tid new_tid = ts.next();
	  self_node->insert(std::make_pair(*p, std::make_pair(change_set::ptype_directory, new_tid)));
	  self_state.insert(std::make_pair(new_tid, change_set::path_item(self_tid, change_set::ptype_directory, *p)));
	  self_tid = new_tid;
	}
    }
}


struct
path_edit_analyzer : public path_edit_consumer
{
  change_set::path_rearrangement & pr;
  tid_source ts;

  directory_map old_dir;
  directory_map new_dir;

  path_edit_analyzer(change_set::path_rearrangement & pr) 
    : pr(pr), ts(pr)
  {
    build_directory_map(pr.first, old_dir);
    build_directory_map(pr.second, new_dir);
  }

  virtual ~path_edit_analyzer()
  {}

  virtual void resolve_conflict(change_set::ptype ty, 
				std::vector<file_path> const & old_prefix, file_path old_leaf_path, 
				std::vector<file_path> const & new_prefix, file_path new_leaf_path,
				bool found_old_entry, bool found_new_entry,
				change_set::tid existing_old_tid, change_set::ptype existing_old_ty,
				change_set::tid existing_new_tid, change_set::ptype existing_new_ty)
  {
    throw informative_failure("tree layout conflict\n");
  }
  
  //
  // we're trying to record a pair of paths X and Y, making up a
  // rearrangement entry. the paths are of the form
  //
  //  x[0], x[1], ... , x[n]
  //  y[0], y[1], ... , y[m]
  //
  // when we are appending, we want to look X and Y[0..m-1] up in the
  // rearrangement destination map, then change x[n] to have parent y[m-1]
  // and name y[m]. we have a conflict if x[n] has a different type than
  // y[m].
  //
  // when we are merging, we want to look X[0..n-1] up in the rearrangement
  // source map, and look Y[0..m-1] up in the rearrangement destination map.
  // we then ensure that either x[n] and y[m] both exist and have the same
  // tid, or neither exists (and we add them).
  //


  void resolve_merge_parents(std::vector<file_path> const & old_parent,
			     std::vector<file_path> const & new_parent,
			     change_set::tid & old_tid,
			     change_set::tid & new_tid)
  {
    resolve_path(old_parent, old_tid, old_dir, new_dir, pr.first, pr.second, ts);
    resolve_path(new_parent, new_tid, new_dir, old_dir, pr.second, pr.first, ts);
  }

  void merge_rearrangement(change_set::ptype ty, 
			   std::vector<file_path> const & old_prefix, 
			   file_path old_leaf_path, 
			   std::vector<file_path> const & new_prefix, 
			   file_path new_leaf_path)
  {
    change_set::tid old_parent_tid, new_parent_tid;
    resolve_merge_parents(old_prefix, new_prefix, old_parent_tid, new_parent_tid);

    boost::shared_ptr<directory_node> old_dnode = dnode(old_dir, old_parent_tid);
    boost::shared_ptr<directory_node> new_dnode = dnode(new_dir, new_parent_tid);

    directory_node::const_iterator existing_old_entry = 
      (null_name(old_leaf_path) ? old_dnode->end() : old_dnode->find(old_leaf_path));

    directory_node::const_iterator existing_new_entry = 
      (null_name(new_leaf_path) ? new_dnode->end() : new_dnode->find(new_leaf_path));

    bool found_old_entry = (existing_old_entry != old_dnode->end());
    bool found_new_entry = (existing_new_entry != new_dnode->end());

    change_set::ptype existing_old_ty = ty, existing_new_ty = ty;
    change_set::tid existing_old_tid = root_tid, existing_new_tid = root_tid;

    if (found_old_entry)
      {
	existing_old_ty = existing_old_entry->second.first;
	existing_old_tid = existing_old_entry->second.second;
      }
    
    if (found_new_entry)
      {
	existing_new_ty = existing_new_entry->second.first;
	existing_new_tid = existing_new_entry->second.second;
      }

    if (found_old_entry && found_new_entry)
      {
	if ((existing_old_ty != ty || existing_new_ty != ty)
	    || (existing_old_tid != existing_new_tid))	  
	  {
	    resolve_conflict(ty, 
			     old_prefix, old_leaf_path, 
			     new_prefix, new_leaf_path,
			     found_old_entry, found_new_entry,
			     existing_old_tid, existing_old_ty,
			     existing_new_tid, existing_new_ty);
	  }
	else
	  {
	    L(F("rearrangement already exists\n"));
	  }
      }    
    else if (found_old_entry || found_old_entry)
      {
	I(! (found_new_entry && found_old_entry));
	resolve_conflict(ty, 
			 old_prefix, old_leaf_path, 
			 new_prefix, new_leaf_path,
			 found_old_entry, found_new_entry,
			 existing_old_tid, existing_old_ty,
			 existing_new_tid, existing_new_ty);
      }
    else
      {
	I(! (found_new_entry || found_old_entry));
	change_set::tid leaf_tid = ts.next();
	
	pr.first.insert(std::make_pair(leaf_tid, change_set::path_item(old_parent_tid, ty, old_leaf_path)));
	pr.second.insert(std::make_pair(leaf_tid, change_set::path_item(new_parent_tid, ty, new_leaf_path)));
	
	old_dnode->insert(std::make_pair(old_leaf_path, std::make_pair(ty, leaf_tid)));
	new_dnode->insert(std::make_pair(new_leaf_path, std::make_pair(ty, leaf_tid)));
      }
  }


  void resolve_append_parents(std::vector<file_path> const & src_parent,
			      std::vector<file_path> const & dst_parent,
			      change_set::tid & src_tid,
			      change_set::tid & dst_tid)
  {
    resolve_path(src_parent, src_tid, new_dir, old_dir, pr.second, pr.first, ts);
    resolve_path(dst_parent, dst_tid, new_dir, old_dir, pr.second, pr.first, ts);
  }
  
  virtual void append_rearrangement(change_set::ptype ty, 
				    std::vector<file_path> const & src_prefix, 
				    file_path src_leaf_path, 
				    std::vector<file_path> const & dst_prefix, 
				    file_path dst_leaf_path)
  {
    change_set::tid src_parent_tid, dst_parent_tid;

    resolve_append_parents(src_prefix, dst_prefix, src_parent_tid, dst_parent_tid);

    // both our lookups happen in the context of the *new* directory map
    boost::shared_ptr<directory_node> src_dnode = dnode(new_dir, src_parent_tid);
    boost::shared_ptr<directory_node> dst_dnode = dnode(new_dir, dst_parent_tid);

    directory_node::const_iterator existing_src_entry = 
      (null_name(src_leaf_path) ? src_dnode->end() : src_dnode->find(src_leaf_path));

    directory_node::const_iterator existing_dst_entry = 
      (null_name(dst_leaf_path) ? dst_dnode->end() : dst_dnode->find(dst_leaf_path));

    bool found_src_entry = (existing_src_entry != src_dnode->end());
    bool found_dst_entry = (existing_dst_entry != dst_dnode->end());

    L(F("resolved entries for appended rearrangement: src %s, dst %s\n")
      % (found_src_entry ? "yes" : "no") % (found_dst_entry ? "yes" : "no"));

    change_set::ptype existing_src_ty = ty, existing_dst_ty = ty;
    change_set::tid existing_src_tid = root_tid, existing_dst_tid = root_tid;

    if (found_src_entry)
      {
	existing_src_ty = existing_src_entry->second.first;
	existing_src_tid = existing_src_entry->second.second;
      }
    
    if (found_dst_entry)
      {
	existing_dst_ty = existing_dst_entry->second.first;
	existing_dst_tid = existing_dst_entry->second.second;
      }

    if (found_src_entry && found_dst_entry)
      {
	if ((existing_src_ty != ty || existing_dst_ty != ty)
	    || (existing_src_tid != existing_dst_tid))	  
	  {
	    resolve_conflict(ty, 
			     src_prefix, src_leaf_path, 
			     dst_prefix, dst_leaf_path,
			     found_src_entry, found_dst_entry,
			     existing_src_tid, existing_src_ty,
			     existing_dst_tid, existing_dst_ty);
	  }
	else
	  {
	    L(F("rearrangement already exists\n"));
	  }
      }    
    else if (found_src_entry)
      {
	if (existing_src_ty != existing_dst_ty)
	  resolve_conflict(ty, 
			   src_prefix, src_leaf_path, 
			   dst_prefix, dst_leaf_path,
			   found_src_entry, found_dst_entry,
			   existing_src_tid, existing_src_ty,
			   existing_dst_tid, existing_dst_ty);
	else
	  {
	    // here we have for example rename(usr/lib, usr/local/lib),
	    // and usr/local does not currently have an entry in it called
	    // lib. that's good. all we need to do is remove lib from usr
	    // and add it to usr/local: replace a directory entry and a
	    // rearrangement entry (both in the "new" maps)
	    dst_dnode->insert(*existing_src_entry);
	    src_dnode->erase(existing_src_entry->first);
	    change_set::path_state::const_iterator i = pr.second.find(existing_src_tid);
	    I(i != pr.second.end());
	    change_set::path_item tmp = i->second;
	    pr.second.erase(existing_src_tid);
	    tmp.parent = dst_parent_tid;
	    tmp.name = dst_leaf_path;
	    pr.second.insert(std::make_pair(existing_src_tid, tmp));
	  }
      }
    else if (found_dst_entry)
      {
	// the rearrangement involves moving from some unknown source
	// to an existing destination. this is a conflict.
	I(found_dst_entry);
	resolve_conflict(ty, 
			 src_prefix, src_leaf_path, 
			 dst_prefix, dst_leaf_path,
			 found_src_entry, found_dst_entry,
			 existing_src_tid, existing_src_ty,
			 existing_dst_tid, existing_dst_ty);
      }
    else
      {
	// the rearrangement does not involve any "new" entries, so 
	// it is equivalent to a merge request. fall back to that.
	merge_rearrangement(ty, 
			    src_prefix, src_leaf_path,
			    dst_prefix, dst_leaf_path);
      }
  }
  
  virtual void record_rearrangement(change_set::ptype ty, 
				    std::vector<file_path> const & old_prefix, 
				    file_path old_leaf_path, 
				    std::vector<file_path> const & new_prefix, 
				    file_path new_leaf_path) = 0;  

  virtual void add_file(file_path const & a) 
  {
    L(F("processing add_file(%s)\n") % a);
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(a, prefix, leaf_path);
    record_rearrangement(change_set::ptype_file, 
			 prefix, null_path, 
			 prefix, leaf_path);
  }

  virtual void delete_file(file_path const & d)
  {
    L(F("processing delete_file(%s)\n") % d);
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(d, prefix, leaf_path);
    record_rearrangement(change_set::ptype_file, 
			 prefix, leaf_path, 
			 prefix, null_path);
  }


  virtual void delete_dir(file_path const & d)
  {
    L(F("processing delete_dir(%s)\n") % d);
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(d, prefix, leaf_path);
    record_rearrangement(change_set::ptype_directory, 
			 prefix, leaf_path, 
			 prefix, null_path);
  }

  virtual void rename_file(file_path const & a, file_path const & b)
  {
    L(F("processing rename_file(%s, %s)\n") % a % b);
    std::vector<file_path> old_prefix, new_prefix;
    file_path old_leaf_path, new_leaf_path;
    split_path(a, old_prefix, old_leaf_path);
    split_path(b, new_prefix, new_leaf_path);
    record_rearrangement(change_set::ptype_file, 
			 old_prefix, old_leaf_path, 
			 new_prefix, new_leaf_path);
  }

  virtual void rename_dir(file_path const & a, file_path const & b)
  {
    L(F("processing rename_dir(%s, %s)\n") % a % b);
    std::vector<file_path> old_prefix, new_prefix;
    file_path old_leaf_path, new_leaf_path;
    split_path(a, old_prefix, old_leaf_path);
    split_path(b, new_prefix, new_leaf_path);
    record_rearrangement(change_set::ptype_directory, 
			 old_prefix, old_leaf_path, 
			 new_prefix, new_leaf_path);
  }
};

struct
path_edit_appender : public path_edit_analyzer
{
  path_edit_appender(change_set::path_rearrangement & pr) 
    : path_edit_analyzer(pr)
  {
  }

  virtual ~path_edit_appender() {}

  virtual void record_rearrangement(change_set::ptype ty, 
				    std::vector<file_path> const & old_prefix, 
				    file_path old_leaf_path, 
				    std::vector<file_path> const & new_prefix, 
				    file_path new_leaf_path)
  {
    append_rearrangement(ty, 
			 old_prefix, old_leaf_path,
			 new_prefix, new_leaf_path);
  }

  virtual void resolve_conflict(change_set::ptype ty, 
				std::vector<file_path> const & old_prefix, file_path old_leaf_path, 
				std::vector<file_path> const & new_prefix, file_path new_leaf_path,
				bool found_old_entry, bool found_new_entry,
				change_set::tid existing_old_tid, change_set::ptype existing_old_ty,
				change_set::tid existing_new_tid, change_set::ptype existing_new_ty)
  {
    std::vector<file_path> f1(old_prefix);
    f1.push_back(old_leaf_path);

    std::vector<file_path> f2(new_prefix);
    f2.push_back(new_leaf_path);
    
    file_path p1, p2;
    compose_path(f1, p1);
    compose_path(f2, p2);
      
    L(F("detected append conflict between %s and %s\n") % p1 % p2);
    L(F("old entry found: %d, new entry found: %d\n") % found_old_entry % found_new_entry);
    L(F("old tid: %d, new tid: %d\n") % existing_old_tid % existing_new_tid);
    L(F("old type: %d, new type: %d\n") % existing_old_ty % existing_new_ty);
    throw informative_failure("tree layout conflict in appender\n");
    // FIXME: put in some conflict resolution logic here 
  }    
};

boost::shared_ptr<path_edit_consumer> 
new_rearrangement_builder(change_set::path_rearrangement & pr)
{
  return boost::shared_ptr<path_edit_consumer>(new path_edit_appender(pr));
}


struct
path_edit_merger : public path_edit_analyzer
{
  path_edit_merger(change_set::path_rearrangement & pr) 
    : path_edit_analyzer(pr)
  {
  }

  virtual ~path_edit_merger() {}  


  virtual void record_rearrangement(change_set::ptype ty, 
				    std::vector<file_path> const & old_prefix, 
				    file_path old_leaf_path, 
				    std::vector<file_path> const & new_prefix, 
				    file_path new_leaf_path)
  {
    merge_rearrangement(ty, 
			old_prefix, old_leaf_path,
			new_prefix, new_leaf_path);
  }

  virtual void resolve_conflict(change_set::ptype ty, 
				std::vector<file_path> const & old_prefix, file_path old_leaf_name, 
				std::vector<file_path> const & new_prefix, file_path new_leaf_name,
				bool found_old_entry, bool found_new_entry,
				change_set::tid existing_old_tid, change_set::ptype existing_old_ty,
				change_set::tid existing_new_tid, change_set::ptype existing_new_ty)
  {
    std::vector<file_path> f1(old_prefix);
    f1.push_back(old_leaf_name);

    std::vector<file_path> f2(new_prefix);
    f2.push_back(new_leaf_name);
    
    file_path p1, p2;
    compose_path(f1, p1);
    compose_path(f2, p2);
      
    L(F("detected merge conflict between %s and %s\n") % p1 % p2);
    throw informative_failure("tree layout conflict in merger\n");
    // FIXME: put in some conflict resolution logic here 
  } 
};

static void
normalize_change_set(change_set & norm)
{
  L(F("normalizing changeset with %d rearrangements\n") 
    % norm.rearrangement.first.size());
  change_set tmp(norm);
  norm.rearrangement.first.clear();
  norm.rearrangement.second.clear();
  path_edit_merger normalizer(norm.rearrangement);
  play_back_rearrangement(tmp.rearrangement, normalizer);
  L(F("normalized changeset has %d rearrangements\n") 
    % norm.rearrangement.first.size());
}


static void
rename_deltas_under_merge(change_set const & a, 
			  change_set const & merged, 
			  change_set::delta_map & a_deltas_renamed)
{
  directory_map a_dst_map, merged_src_map;

  build_directory_map(a.rearrangement.second, a_dst_map);
  build_directory_map(merged.rearrangement.first, merged_src_map);

  a_deltas_renamed.clear();

  for (change_set::delta_map::const_iterator i = a.deltas.begin(); 
       i != a.deltas.end(); ++i)
    {
      file_path a_dst_path = delta_entry_path(i);
      file_path src_path, merged_dst_path;

      reconstruct_path(a_dst_path, a_dst_map, a.rearrangement.first, src_path);
      reconstruct_path(src_path, merged_src_map, merged.rearrangement.second, merged_dst_path);
      L(F("renamed delta '%s' -> '%s' from path '%s' to '%s'\n")
	% delta_entry_src(i)
	% delta_entry_src(i)
	% delta_entry_path(i)
	% merged_dst_path);
      a_deltas_renamed.insert(std::make_pair(merged_dst_path,
					     std::make_pair(delta_entry_src(i),
							    delta_entry_dst(i))));      
    }
}

void
merge_change_sets(change_set const & a,
		  change_set const & b,
		  merge_provider & file_merger,
		  change_set & merged)
{
  merged.rearrangement = a.rearrangement;
  merged.deltas.clear();

  path_edit_merger mer(merged.rearrangement);
  play_back_rearrangement(b.rearrangement, mer);

  //
  // a is a change from X->Y,
  // b is a change from X->Z,
  //
  // merged is a change from X->M
  // 
  // we take each delta d in a and look up its tid p in Y, map it (via a)
  // to path q in X, then map that to tid r in M (via merged). that is the
  // merged path for d.
  //
  // we do the same thing for deltas coming from b. this process yields two
  // "renamed" delta maps. we then reconcile them: if two deltas occur on
  // the same path we resolve it via the file merger, otherwise we copy the
  // delta over to the result.
  // 

  change_set::delta_map a_deltas_renamed, b_deltas_renamed;
  rename_deltas_under_merge(a, merged, a_deltas_renamed);
  rename_deltas_under_merge(b, merged, b_deltas_renamed);

  for (change_set::delta_map::const_iterator i = a_deltas_renamed.begin(); 
       i != a_deltas_renamed.end(); ++i)
    {
      change_set::delta_map::const_iterator j = b_deltas_renamed.find(delta_entry_path(i));
      if (j != b_deltas_renamed.end())
	{
	  L(F("found simultaneous deltas '%s' -> '%s' and '%s' -> '%s' on '%s'\n") 
	    % delta_entry_src(i)
	    % delta_entry_dst(i)
	    % delta_entry_src(j)
	    % delta_entry_dst(j)
	    % delta_entry_path(i));
	  I(delta_entry_src(i) == delta_entry_src(j));
	  if (delta_entry_dst(i) == delta_entry_dst(j))
	    {
	      L(F("simultaneous deltas are identical: '%s' -> '%s'\n")
		% delta_entry_src(i) % delta_entry_dst(i));
	      merged.deltas.insert(*i);
	      b_deltas_renamed.erase(delta_entry_path(i));
	    }
	  else
	    {
	      L(F("simultaneous deltas differ: '%s' -> '%s' and '%s'\n")
		% delta_entry_src(i) % delta_entry_dst(i) % delta_entry_dst(j));
	      file_id merged_id;
	      N(file_merger.try_to_merge_files(delta_entry_path(i),
					       delta_entry_src(i),
					       delta_entry_dst(i),
					       delta_entry_dst(j),
					       merged_id),
		F("merge3 failed: %s -> %s and %s on %s\n")
		% delta_entry_src(i) 
		% delta_entry_dst(i) 
		% delta_entry_dst(j) 
		% delta_entry_path(i));
	      merged.deltas.insert(std::make_pair(delta_entry_path(i),
						  std::make_pair(delta_entry_src(i),
								 merged_id)));
	      b_deltas_renamed.erase(delta_entry_path(i));
	    }
	}
      else
	{
	  L(F("copying delta '%s' -> '%s' on '%s' from first changeset\n")
	    % delta_entry_src(i) % delta_entry_dst(i) % delta_entry_path(i));
	  merged.deltas.insert(*i);
	}
    }

  // copy all remaining deltas from b as well
  for (change_set::delta_map::const_iterator i = b_deltas_renamed.begin(); 
       i != b_deltas_renamed.end(); ++i)
    {
      L(F("copying delta '%s' -> '%s' on '%s' from second changeset\n")
	% delta_entry_src(i) % delta_entry_dst(i) % delta_entry_path(i));
      merged.deltas.insert(*i);
    }

  normalize_change_set(merged);
}

void
concatenate_change_sets(change_set const & a,
			change_set const & b,
			change_set & concatenated)
{
  L(F("concatenating rearrangements with %d and %d entries\n") 
    % a.rearrangement.first.size()
    % b.rearrangement.first.size());

  concatenated = a;
  path_edit_appender app(concatenated.rearrangement);
  play_back_rearrangement(b.rearrangement, app);

  // now process the deltas

  concatenated.deltas.clear();
  directory_map b_src_map;
  L(F("concatenating %d and %d deltas\n")
    % a.deltas.size() % b.deltas.size());
  build_directory_map(b.rearrangement.first, b_src_map);

  // first rename a's deltas under the rearrangement of b
  for (change_set::delta_map::const_iterator del = a.deltas.begin();
       del != a.deltas.end(); ++del)
    {
      file_path new_pth;
      L(F("processing delta on %s\n") % delta_entry_path(del));
      reconstruct_path(delta_entry_path(del), b_src_map, b.rearrangement.second, new_pth);
      L(F("delta on %s in first changeset renamed to %s\n")
	% delta_entry_path(del) % new_pth);
      concatenated.deltas.insert(std::make_pair(new_pth,
						std::make_pair(delta_entry_src(del),
							       delta_entry_dst(del))));
    }

  // next fuse any deltas id1->id2 and id2->id3 to id1->id3
  for (change_set::delta_map::const_iterator del = b.deltas.begin();
       del != b.deltas.end(); ++del)
    {
      change_set::delta_map::const_iterator existing = 
	concatenated.deltas.find(delta_entry_path(del));
      if (existing != concatenated.deltas.end())
	{
	  I(delta_entry_dst(existing) == delta_entry_src(del));
	  L(F("fusing deltas on %s : %s -> %s -> %s\n")
	    % delta_entry_path(del) 
	    % delta_entry_src(existing) 
	    % delta_entry_dst(existing)
	    % delta_entry_dst(del));
	  std::pair<file_id, file_id> fused = std::make_pair(delta_entry_src(existing),
							     delta_entry_dst(del));      
	  concatenated.deltas.erase(delta_entry_path(del));
	  concatenated.deltas.insert(std::make_pair(delta_entry_path(del), fused));
	}
      else
	{
	  L(F("delta on %s in second changeset copied forward\n")
	    % delta_entry_path(del));
	  concatenated.deltas.insert(*del);
	}
    }

  normalize_change_set(concatenated);

  L(F("concatenated changeset has %d rearrangement entries, %d deltas\n") 
    % concatenated.rearrangement.first.size() % concatenated.deltas.size());
}


// application stuff


void
apply_path_rearrangement(path_set const & old_ps,
			 change_set::path_rearrangement const & pr,
			 path_set & new_ps)
{
  new_ps.clear();

  change_set::path_rearrangement composed;

  {
    // first turn the path_set into an identity path_rearrangement
    path_edit_appender pa(composed);
    for(path_set::const_iterator i = old_ps.begin();
	i != old_ps.end(); ++i)
      pa.add_file(*i);
    composed.first = composed.second;
  }

  path_edit_appender pa(composed);  
  play_back_rearrangement(pr, pa);
  
  for (change_set::path_state::const_iterator i = composed.second.begin();
       i != composed.second.end(); ++i)
    {
      if (!null_name(path_item_name(path_state_item(i)))
	  && (path_item_type(path_state_item(i)) == change_set::ptype_file))
	{
	  file_path pth;
	  get_full_path(composed.second, path_state_tid(i), pth);
	  I(new_ps.find(pth) == new_ps.end());
	  new_ps.insert(pth);	  
	}
    }
}

// this function rearranges a manifest map under a path_rearrangement
// but does *not* apply any deltas to it. so notably, if a file was 
// added the new file will have an empty id, since all we know is that
// it was added. 

void
apply_path_rearrangement(manifest_map const & m_old,
			 change_set::path_rearrangement const & pr,
			 manifest_map & m_old_rearranged)
{
  m_old_rearranged.clear();

  change_set::path_rearrangement composed;
  
  {
    // first turn the manifest_map into an identity path_rearrangement
    path_edit_appender pa(composed);
    for(manifest_map::const_iterator i = m_old.begin();
	i != m_old.end(); ++i)
      pa.add_file(manifest_entry_path(i));
    composed.first = composed.second;
  }

  path_edit_appender pa(composed);  
  play_back_rearrangement(pr, pa);
  
  for (change_set::path_state::const_iterator i = composed.second.begin();
       i != composed.second.end(); ++i)
    {
      if (!null_name(path_item_name(path_state_item(i)))
	  && (path_item_type(path_state_item(i)) == change_set::ptype_file))
	{
	  file_path new_pth;
	  get_full_path(composed.second, path_state_tid(i), new_pth);

	  change_set::path_state::const_iterator j = composed.first.find(path_state_tid(i));
	  I(j != composed.first.end());

	  if (null_name(path_item_name(path_state_item(j))))
	    {
	      // case 1: the file was added, the best we can do is leave an 
	      // empty entry in the manifest map
	      m_old_rearranged.insert(std::make_pair(new_pth, file_id()));
	    }
	  else
	    {
	      // case 2: the file was not added, copy its old file id forwards
	      file_path old_pth;
	      get_full_path(composed.first, path_state_tid(i), old_pth);
	      manifest_map::const_iterator old = m_old.find(old_pth);	
	      I(old != m_old.end());
	      m_old_rearranged.insert(std::make_pair(new_pth, manifest_entry_id(old)));
	    }
	}
    }
}

struct change_set_builder :
  public change_set_consumer
{
  change_set & target;
  path_edit_appender pae;
  change_set_builder(change_set & t) : 
    target(t), pae(path_edit_appender(target.rearrangement)) 
  {
    target.rearrangement.first.clear();
    target.rearrangement.second.clear();
    target.deltas.clear();
  }
  virtual void add_file(file_path const & pth, file_id const & ident)
  {
    pae.add_file(pth);
    apply_delta(pth, file_id(), ident);
  }
  virtual void apply_delta(file_path const & path, 
			   file_id const & src, 
			   file_id const & dst)
  {
    target.deltas.insert(std::make_pair(path, std::make_pair(src, dst)));
  }
  virtual void delete_file(file_path const & d) { pae.delete_file(d); }
  virtual void delete_dir(file_path const & d) { pae.delete_dir(d); }
  virtual void rename_file(file_path const & a, file_path const & b) 
  { pae.rename_file(a,b); }
  virtual void rename_dir(file_path const & a, file_path const & b) 
  { pae.rename_dir(a,b); }
};

void
build_pure_addition_change_set(manifest_map const & man,
			       change_set & cs)
{
  change_set_builder b(cs);
  for (manifest_map::const_iterator i = man.begin(); i != man.end(); ++i)
    {
      b.add_file(manifest_entry_path(i), manifest_entry_id(i));
    }
}


void
apply_change_set(manifest_map const & old_man,
		 change_set const & cs,
		 manifest_map & new_man)
{
  new_man.clear();

  change_set::path_rearrangement composed;
  
  {
    // first turn the manifest_map into an identity path_rearrangement
    path_edit_appender pa(composed);
    for(manifest_map::const_iterator i = old_man.begin();
	i != old_man.end(); ++i)
      pa.add_file(manifest_entry_path(i));
    composed.first = composed.second;
  }

  path_edit_appender pa(composed);  
  play_back_rearrangement(cs.rearrangement, pa);
  
  for (change_set::path_state::const_iterator i = composed.second.begin();
       i != composed.second.end(); ++i)
    {
      if (!null_name(path_item_name(path_state_item(i)))
	  && (path_item_type(path_state_item(i)) == change_set::ptype_file))
	{
	  file_path new_pth;
	  get_full_path(composed.second, path_state_tid(i), new_pth);

	  change_set::path_state::const_iterator j = composed.first.find(path_state_tid(i));
	  I(j != composed.first.end());

	  if (null_name(path_item_name(path_state_item(j))))
	    {
	      // case 1: the file was added, there's an entry in the delta
	      // map with empty preimage
	      change_set::delta_map::const_iterator del = cs.deltas.find(new_pth);
	      I(del != cs.deltas.end());
	      I(delta_entry_src(del).inner()().empty());
	      new_man.insert(std::make_pair(new_pth, delta_entry_dst(del)));
	    }
	  else
	    {
	      change_set::delta_map::const_iterator del = cs.deltas.find(new_pth);
	      if (del == cs.deltas.end())
		{
		  // case 2: the file was not edited, copy the old
		  // manifest's id forward (whether there was a move or
		  // not)
		  file_path old_pth;
		  get_full_path(composed.first, path_state_tid(i), old_pth);
		  manifest_map::const_iterator old = old_man.find(old_pth);	
		  I(old != old_man.end());
		  new_man.insert(std::make_pair(new_pth, manifest_entry_id(old)));
		}
	      else
		{
		  // case 3: the file was moved and edited, there's an
		  // entry in the delta map.
		  I(! delta_entry_src(del).inner()().empty());
		  new_man.insert(std::make_pair(new_pth, delta_entry_dst(del)));
		}
	    }
	}
    }
}

static void
subtract_pure_renames(change_set const & abc,
		      change_set const & ab,
		      directory_map const & abc_dst_map,
		      directory_map const & ab_src_map,
		      std::map<file_path,file_path> const & abc_renamed,
		      std::map<file_path,file_path> const & ab_renamed,
		      std::map<file_path,file_path> & bc_renamed)
{
  bc_renamed.clear();
  for (std::map<file_path,file_path>::const_iterator abc_rename = abc_renamed.begin();
       abc_rename != abc_renamed.end(); ++abc_rename)
    {
      file_path src_pth_in_a(abc_rename->first);
      file_path dst_pth_in_c(abc_rename->second);

      std::map<file_path,file_path>::const_iterator ab_rename = ab_renamed.find(src_pth_in_a);
      if (ab_rename != ab_renamed.end())
	{
	  if (ab_rename->second == dst_pth_in_c)
	    L(F("subtracting duplicate rename '%s' -> '%s'\n") % src_pth_in_a % dst_pth_in_c);
	  else
	    {
	      L(F("reduced rename from '%s' -> '%s' to '%s' -> '%s'\n") 
		% src_pth_in_a % dst_pth_in_c % ab_rename->second % dst_pth_in_c);
	      bc_renamed.insert(std::make_pair(ab_rename->second, dst_pth_in_c));
	    }
	}
      else
	{
	  file_path src_pth_in_b;
	  reconstruct_path(src_pth_in_a, ab_src_map, ab.rearrangement.second, src_pth_in_b);
	  L(F("preserving rename '%s' -> '%s' as '%s' -> '%s'\n") 
	    % src_pth_in_a % dst_pth_in_c % src_pth_in_b % dst_pth_in_c);
	  bc_renamed.insert(std::make_pair(src_pth_in_b, dst_pth_in_c));
	}
    }
}


void
subtract_change_sets(change_set const & abc,
		     change_set const & ab,		     
		     change_set & bc)
{

  bc.rearrangement.first.clear();
  bc.rearrangement.second.clear();
  bc.deltas.clear();

  directory_map abc_src_map, abc_dst_map, ab_src_map, ab_dst_map;
  build_directory_map(abc.rearrangement.first, abc_src_map);
  build_directory_map(abc.rearrangement.second, abc_dst_map);
  build_directory_map(ab.rearrangement.first, ab_src_map);
  build_directory_map(ab.rearrangement.second, ab_dst_map);

  std::set<file_path> abc_deleted_files, ab_deleted_files, bc_deleted_files;
  std::set<file_path> abc_deleted_dirs, ab_deleted_dirs, bc_deleted_dirs;
  std::map<file_path, file_path> abc_renamed_files, ab_renamed_files, bc_renamed_files;
  std::map<file_path, file_path> abc_renamed_dirs, ab_renamed_dirs, bc_renamed_dirs;
  std::set<file_path> abc_added_files, ab_added_files, bc_added_files;

  analyze_rearrangement(abc.rearrangement, 
			abc_deleted_files, 
			abc_deleted_dirs,
			abc_renamed_files, 
			abc_renamed_dirs,
			abc_added_files);

  analyze_rearrangement(ab.rearrangement, 
			ab_deleted_files, 
			ab_deleted_dirs,
			ab_renamed_files, 
			ab_renamed_dirs,
			ab_added_files);

  // subtract deletes

  std::set_difference(abc_deleted_files.begin(), abc_deleted_files.end(),
		      ab_deleted_files.begin(), ab_deleted_files.end(),
		      std::inserter(bc_deleted_files, bc_deleted_files.begin()));

  std::set_difference(abc_deleted_dirs.begin(), abc_deleted_dirs.end(),
		      ab_deleted_dirs.begin(), ab_deleted_dirs.end(),
		      std::inserter(bc_deleted_dirs, bc_deleted_dirs.begin()));

  // subtract renames
  
  subtract_pure_renames(abc, ab, abc_dst_map, ab_src_map, 
			abc_renamed_files, ab_renamed_files, bc_renamed_files);

  subtract_pure_renames(abc, ab, abc_dst_map, ab_src_map, 
			abc_renamed_dirs, ab_renamed_dirs, bc_renamed_dirs);

  // subtract adds

  for (std::set<file_path>::const_iterator abc_add = abc_added_files.begin();
       abc_add != abc_added_files.end(); ++abc_add)
    {
      // we cannot *exactly* go by way of the a name map, because it's possible
      // that ab has some other business happening with the name added here
      // (for example, ab could contain a rename starting from our path; that is
      // legal as we can add a new file in bc in the position moved away from 
      // in ab). what we do instead is work out the target *directory* via the
      // a name map, then check for adds with the current leaf. afaik this is
      // the most sensible interpretation, but it might need revisiting.
      
      std::vector<file_path> tmp; 
      file_path parent_in_c, parent_in_a, parent_in_b, leaf, ab_add;
      split_path(*abc_add, tmp, leaf);
      compose_path(tmp, parent_in_c);
      reconstruct_path(parent_in_c, abc_dst_map, abc.rearrangement.first, parent_in_a);
      reconstruct_path(parent_in_a, ab_src_map, ab.rearrangement.second, parent_in_b);
      split_path(parent_in_b, tmp);
      tmp.push_back(leaf);
      compose_path(tmp, ab_add);
      if (ab_added_files.find(ab_add) != ab_added_files.end())
	{
	  change_set::delta_map::const_iterator abc_delta = abc.deltas.find(*abc_add);
	  change_set::delta_map::const_iterator ab_delta = ab.deltas.find(ab_add);
	  I(abc_delta != abc.deltas.end());
	  I(ab_delta != ab.deltas.end());
	  I(delta_entry_src(abc_delta).inner()().empty());
	  I(delta_entry_src(ab_delta).inner()().empty());
	  if (delta_entry_dst(abc_delta) == delta_entry_dst(ab_delta))
	    {
	      L(F("subtracting add of '%s' at '%s', matched with add at '%s'\n")
		% delta_entry_dst(abc_delta) % *abc_add % ab_add);
	    }
	  else
	    {
	      L(F("converting adds '%s' at '%s' and '%s' at '%s' to delta\n")
		% delta_entry_dst(abc_delta) % *abc_add 
		% delta_entry_dst(ab_delta) % ab_add);
	      bc.deltas.insert(std::make_pair(ab_add, 
					      std::make_pair(delta_entry_dst(ab_delta),
							     delta_entry_dst(abc_delta))));
	    }
	}
    }

  // subtract non-addition deltas

  for (change_set::delta_map::const_iterator abc_delta = abc.deltas.begin();
       abc_delta != abc.deltas.end(); ++abc_delta)
    {
      if (delta_entry_src(abc_delta).inner()().empty())
	continue;
      file_path delta_in_c(delta_entry_path(abc_delta)), delta_in_a, delta_in_b;
      reconstruct_path(delta_in_c, abc_dst_map, abc.rearrangement.first, delta_in_a);
      if (ab_deleted_files.find(delta_in_a) != ab_deleted_files.end())
	L(F("subtracting delta on '%s' due to deletion on preimage '%s'\n") 
	  % delta_in_c % delta_in_a);
      else
	{
	  reconstruct_path(delta_in_a, ab_src_map, ab.rearrangement.second, delta_in_b);
	  change_set::delta_map::const_iterator ab_delta = ab.deltas.find(delta_in_b);
	  if (ab_delta != ab.deltas.end())
	    {
	      I(delta_entry_src(ab_delta) == delta_entry_src(abc_delta));
	      if (delta_entry_dst(ab_delta) == delta_entry_dst(abc_delta))
		L(F("subtracting duplicate delta '%s' -> '%s' on '%s'\n") 
		  % delta_entry_src(ab_delta) % delta_entry_dst(ab_delta) % delta_in_c);
	      else
		{
		  L(F("reduced delta on '%s' from '%s' -> '%s' to '%s' -> '%s'\n")
		    % delta_in_c 
		    % delta_entry_src(abc_delta) % delta_entry_dst(abc_delta)
		    % delta_entry_dst(ab_delta) % delta_entry_dst(abc_delta));
		  bc.deltas.insert(std::make_pair(delta_in_b,
						  std::make_pair(delta_entry_dst(ab_delta),
								 delta_entry_dst(abc_delta))));		  
		}
	    }
	  else
	    {
	      L(F("preserving delta '%s' -> '%s' on '%s'\n")		
		% delta_entry_src(abc_delta) % delta_entry_dst(abc_delta) % delta_in_b);
		  bc.deltas.insert(std::make_pair(delta_in_b,
						  std::make_pair(delta_entry_src(abc_delta),
								 delta_entry_dst(abc_delta))));
	    }	  
	}
    }  
}

void 
invert_change_set(change_set const & a2b,
		  manifest_map const & a_map,
		  change_set & b2a)
{

  L(F("inverting change set\n"));
  b2a.rearrangement.first = a2b.rearrangement.second;
  b2a.rearrangement.second = a2b.rearrangement.first;
  b2a.deltas.clear();

  // existing deltas are in "b space"
  for (change_set::path_state::const_iterator b = b2a.rearrangement.first.begin();
       b != b2a.rearrangement.first.end(); ++b)
    {
      change_set::path_state::const_iterator a = b2a.rearrangement.second.find(path_state_tid(b));
      I(a != b2a.rearrangement.second.end());
      if (path_item_type(path_state_item(b)) == change_set::ptype_file)
	{
	  file_path b_pth, a_pth;
	  get_full_path(b2a.rearrangement.first, path_state_tid(b), b_pth);

	  if (null_name(path_item_name(path_state_item(b))) &&
	      ! null_name(path_item_name(path_state_item(a))))
	    {
	      // b->a represents an add in "a space"
	      get_full_path(b2a.rearrangement.second, path_state_tid(a), a_pth);
	      manifest_map::const_iterator i = a_map.find(a_pth);
	      I(i != a_map.end());
	      b2a.deltas.insert(std::make_pair(a_pth, 
					       std::make_pair(file_id(), 
							      manifest_entry_id(i))));
	      L(F("converted 'delete %s' to 'add as %s' in inverse\n")
		% a_pth 
		% manifest_entry_id(i));
	    }
	  else if (! null_name(path_item_name(path_state_item(b))) &&
		   null_name(path_item_name(path_state_item(a))))
	    {
	      // b->a represents a del from "b space"
	      get_full_path(b2a.rearrangement.first, path_state_tid(b), b_pth);
	      L(F("converted add %s to delete in inverse\n") % b_pth );
	    }
	  else
	    {
	      get_full_path(b2a.rearrangement.first, path_state_tid(b), b_pth);
	      get_full_path(b2a.rearrangement.second, path_state_tid(a), a_pth);
	      change_set::delta_map::const_iterator del = a2b.deltas.find(b_pth);
	      if (del == a2b.deltas.end())
		continue;
	      file_id src_id(delta_entry_src(del)), dst_id(delta_entry_dst(del));
	      L(F("converting delta %s -> %s on %s\n")
		% src_id % dst_id % b_pth);
	      L(F("inverse is delta %s -> %s on %s\n")
		% dst_id % src_id % a_pth);
	      b2a.deltas.insert(std::make_pair(a_pth, std::make_pair(dst_id, src_id)));
	    }
	}
    }

  // some deltas might not have been renamed, however. these we just invert the
  // direction on
  for (change_set::delta_map::const_iterator del = a2b.deltas.begin();
       del != a2b.deltas.end(); ++del)
    {
      // check to make sure this isn't one of the already-moved deltas
      if (b2a.deltas.find(delta_entry_path(del)) != b2a.deltas.end())
	continue;
      b2a.deltas.insert(std::make_pair(delta_entry_path(del),
				       std::make_pair(delta_entry_dst(del),
						      delta_entry_src(del))));
    }
}

// i/o stuff

namespace
{
  namespace syms
  {
    std::string const change_set("change_set");
    std::string const paths("paths");
    std::string const add_file("add_file");
    std::string const delete_file("delete_file");
    std::string const delete_dir("delete_dir");
    std::string const rename_file("rename_file");
    std::string const rename_dir("rename_dir");
    std::string const src("src");
    std::string const dst("dst");
    std::string const deltas("deltas");
    std::string const delta("delta");
    std::string const path("path");
  }
}

static void 
parse_path_edits(basic_io::parser & parser,
		 path_edit_consumer & pc)
{
  while (parser.symp())
    {
      std::string t1, t2;
      if (parser.symp(syms::add_file)) 
	{ 
	  parser.key(syms::add_file);
	  parser.str(t1);
	  pc.add_file(file_path(t1));
	}
      else if (parser.symp(syms::delete_file)) 
	{ 
	  parser.key(syms::delete_file);
	  parser.str(t1);
	  pc.delete_file(file_path(t1));
	}
      else if (parser.symp(syms::delete_dir)) 
	{ 
	  parser.key(syms::delete_dir);
	  parser.str(t1);
	  pc.delete_dir(file_path(t1));
	}
      else if (parser.symp(syms::rename_file)) 
	{ 
	  parser.key(syms::rename_file);
	  parser.bra();
	  parser.key(syms::src);
	  parser.str(t1);
	  parser.key(syms::dst);
	  parser.str(t2);
	  parser.ket();
	  pc.rename_file(file_path(t1),
			 file_path(t2));
	}
      else if (parser.symp(syms::rename_dir)) 
	{ 
	  parser.key(syms::rename_dir);
	  parser.bra();
	  parser.key(syms::src);
	  parser.str(t1);
	  parser.key(syms::dst);
	  parser.str(t2);
	  parser.ket();
	  pc.rename_dir(file_path(t1),
			file_path(t2));
	}
    }
}


void 
parse_path_rearrangement(basic_io::parser & pa,
			 change_set::path_rearrangement & pr)
{
  path_edit_merger mer(pr);
  parse_path_edits(pa, mer);
}

struct
path_edit_printer : public path_edit_consumer
{
  basic_io::printer & printer;
  path_edit_printer (basic_io::printer & printer)
    : printer(printer)
  {
  }

  virtual void add_file(file_path const & a)
  {
    printer.print_key(syms::add_file);
    printer.print_str(a());
  }

  virtual void delete_file(file_path const & d)
  {
    printer.print_key(syms::delete_file);
    printer.print_str(d());
  }

  virtual void delete_dir(file_path const & d)
  {
    printer.print_key(syms::delete_dir);
    printer.print_str(d());
  }

  virtual void rename_file(file_path const & a, file_path const & b)
  {
    printer.print_key(syms::rename_file, true);
    {
      basic_io::scope sc(printer);
      printer.print_key(syms::src);
      printer.print_str(a());
      printer.print_key(syms::dst);      
      printer.print_str(b());
    }
  }

  virtual void rename_dir(file_path const & a, file_path const & b)
  {
    printer.print_key(syms::rename_dir, true);
    {
      basic_io::scope sc(printer);
      printer.print_key(syms::src);
      printer.print_str(a());
      printer.print_key(syms::dst);      
      printer.print_str(b());
    }
  }

  virtual ~path_edit_printer() {}
};

void 
print_path_rearrangement(basic_io::printer & basic_printer,
			 change_set::path_rearrangement const & pr)
{
  path_edit_printer printer(basic_printer);
  play_back_rearrangement(pr, printer);
}

void 
parse_change_set(basic_io::parser & parser,
		 change_set & cs)
{
  cs.rearrangement.first.clear();
  cs.rearrangement.second.clear();
  cs.deltas.clear();

  parser.key(syms::change_set);
  parser.bra();

  {
    parser.key(syms::paths);
    parser.bra();
    parse_path_rearrangement(parser, cs.rearrangement);    
    parser.ket();

    parser.key(syms::deltas);
    parser.bra();
    while (parser.symp(syms::delta))
      {
	std::string path, src, dst;
	parser.key(syms::delta);
	parser.bra();
	parser.key(syms::path);
	parser.str(path);
	parser.key(syms::src);
	parser.hex(src);
	parser.key(syms::dst);
	parser.hex(dst);
	parser.ket();
	cs.deltas.insert(std::make_pair(file_path(path),
					std::make_pair(file_id(src),
						       file_id(dst))));
      }
    parser.ket();
  }
  parser.ket();
}

void 
print_change_set(basic_io::printer & printer,
		 change_set const & cs)
{
  printer.print_key(syms::change_set, true);
  {
    basic_io::scope s0(printer);

    printer.print_key(syms::paths, true);
    {
      basic_io::scope s1(printer);
      print_path_rearrangement(printer, cs.rearrangement);
    }

    printer.print_key(syms::deltas, true);
    {
      basic_io::scope s1(printer);
      for (change_set::delta_map::const_iterator i = cs.deltas.begin();
	   i != cs.deltas.end(); ++i)
	{
	  printer.print_key(syms::delta, true);
	  {
	    basic_io::scope s2(printer);
	    printer.print_key(syms::path);
	    printer.print_str(i->first());
	    printer.print_key(syms::src);
	    printer.print_hex(i->second.first.inner()());
	    printer.print_key(syms::dst);
	    printer.print_hex(i->second.second.inner()());
	  }
	}
    }
  }
}

void
read_path_rearrangement(data const & dat,
			change_set::path_rearrangement & re)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss);
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_path_rearrangement(pars, re);
}

void
read_change_set(data const & dat,
		change_set & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss);
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_change_set(pars, cs);
}

void
write_change_set(change_set const & cs,
		 data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_change_set(pr, cs);
  dat = data(oss.str());  
}

void
write_path_rearrangement(change_set::path_rearrangement const & re,
			 data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_path_rearrangement(pr, re);
  dat = data(oss.str());  
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"

static void dump_change_set(std::string const & ctx,
			    change_set const & cs)
{
  data tmp;
  write_change_set(cs, tmp);
  std::clog << "[begin changeset '" << ctx << "']" << std::endl 
	    << tmp 
	    << "[end changeset '" << ctx << "']" << std::endl;  
}

static void
spin_change_set(change_set const & cs)
{
  data tmp1;
  change_set cs1;
  write_change_set(cs, tmp1);
  dump_change_set("normalized", cs);
  read_change_set(tmp1, cs1);
  for (int i = 0; i < 5; ++i)
    {
      data tmp2;
      change_set cs2;
      write_change_set(cs1, tmp2);
      BOOST_CHECK(tmp1 == tmp2);
      read_change_set(tmp2, cs2);
      BOOST_CHECK(cs1.rearrangement == cs2.rearrangement);
      BOOST_CHECK(cs1.deltas == cs2.deltas);
      cs1 = cs2;      
    }
}

static void 
basic_change_set_test()
{
  try
    {
      
      change_set cs;
      path_edit_appender pa(cs.rearrangement);
      pa.delete_file(file_path("usr/lib/zombie"));
      pa.add_file(file_path("usr/bin/cat"));
      pa.add_file(file_path("usr/local/bin/dog"));
      pa.rename_file(file_path("usr/local/bin/dog"), file_path("usr/bin/dog"));
      pa.rename_file(file_path("usr/bin/cat"), file_path("usr/local/bin/chicken"));
      pa.add_file(file_path("usr/lib/libc.so"));
      pa.rename_dir(file_path("usr/lib"), file_path("usr/local/lib"));
      cs.deltas.insert(std::make_pair(file_path("usr/local/lib/libc.so"), 
				      std::make_pair(file_id(hexenc<id>("")),
						     file_id(hexenc<id>("435e816c30263c9184f94e7c4d5aec78ea7c028a")))));
      cs.deltas.insert(std::make_pair(file_path("usr/local/bin/chicken"), 
				      std::make_pair(file_id(hexenc<id>("c6a4a6196bb4a744207e1a6e90273369b8c2e925")),
						     file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892")))));
      spin_change_set(cs);
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

static void 
neutralize_change_test()
{
  try
    {
      
      change_set cs1, cs2, csa;
      path_edit_appender pa1(cs1.rearrangement);
      pa1.add_file(file_path("usr/lib/zombie"));
      pa1.rename_file(file_path("usr/lib/apple"),
		      file_path("usr/lib/orange"));
      pa1.rename_dir(file_path("usr/lib/moose"),
		     file_path("usr/lib/squirrel"));

      dump_change_set("neutralize target", cs1);

      path_edit_appender pa2(cs2.rearrangement);
      pa2.delete_file(file_path("usr/lib/zombie"));
      pa2.rename_file(file_path("usr/lib/orange"),
		      file_path("usr/lib/apple"));
      pa2.rename_dir(file_path("usr/lib/squirrel"),
		     file_path("usr/lib/moose"));

      dump_change_set("neutralizer", cs2);
      
      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("neutralized", csa);

      BOOST_CHECK(csa.rearrangement.first.empty());
      BOOST_CHECK(csa.rearrangement.second.empty());
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

static void 
non_interfering_change_test()
{
  try
    {
      
      change_set cs1, cs2, csa;
      path_edit_appender pa1(cs1.rearrangement);
      pa1.delete_file(file_path("usr/lib/zombie"));
      pa1.rename_file(file_path("usr/lib/orange"),
		      file_path("usr/lib/apple"));
      pa1.rename_dir(file_path("usr/lib/squirrel"),
		     file_path("usr/lib/moose"));

      dump_change_set("non-interference A", cs1);

      path_edit_appender pa2(cs2.rearrangement);
      pa2.add_file(file_path("usr/lib/zombie"));
      pa2.rename_file(file_path("usr/lib/pear"),
		      file_path("usr/lib/orange"));
      pa2.rename_dir(file_path("usr/lib/spy"),
		     file_path("usr/lib/squirrel"));
      
      dump_change_set("non-interference B", cs2);

      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("non-interference combined", csa);

      BOOST_CHECK(csa.rearrangement.first.size() == 8);
      BOOST_CHECK(csa.rearrangement.second.size() == 8);
    }
  catch (informative_failure & exn)
    {
      L(F("informative failure: %s\n") % exn.what);
    }
  catch (std::runtime_error & exn)
    {
      L(F("runtime error: %s\n") % exn.what());
    }
}

void 
add_change_set_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&basic_change_set_test));
  suite->add(BOOST_TEST_CASE(&neutralize_change_test));
  suite->add(BOOST_TEST_CASE(&non_interfering_change_test));
}


#endif // BUILD_UNIT_TESTS
