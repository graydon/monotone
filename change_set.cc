// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "basic_io.hh"
#include "change_set.hh"
#include "interner.hh"
#include "sanity.hh"

#include <algorithm>
#include <iterator>
#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>

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

change_set::tid const & 
path_item_parent(change_set::path_item const & p) 
{ 
  return p.parent; 
}

change_set::ptype const & 
path_item_type(change_set::path_item const & p) 
{ 
  return p.ty; 
}

file_path const & 
path_item_name(change_set::path_item const & p) 
{ 
  return p.name; 
}

change_set::tid
path_state_tid(change_set::path_state::const_iterator i)
{
  return i->first;
}

change_set::path_item const &
path_state_item(change_set::path_state::const_iterator i)
{
  return i->second;
}

static void
sanity_check_path_item(change_set::path_item const & pi)
{
  fs::path tmp(pi.name());
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

static void
check_states_agree(change_set::path_state const & p1,
		   change_set::path_state const & p2)
{
  for (change_set::path_state::const_iterator i = p1.begin(); i != p1.end(); ++i)
    {
      change_set::path_state::const_iterator j = p2.find(i->first);
      I(j != p2.end());
      I(path_item_type(i->second) == path_item_type(j->second));
      I(! (null_name(path_item_name(i->second))
	   &&
	   null_name(path_item_name(j->second))));
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
compose_path(std::vector<file_path> const & names,
	     file_path & path)
{
  try
    {      
      std::vector<file_path>::const_iterator i = names.begin();
      I(i != names.end());
      boost::filesystem::path p((*i)());
      for ( ; i != names.end(); ++i)
	p /= (*i)();
      path = file_path(p.string());
    }
  catch (std::runtime_error &e)
    {
      throw informative_failure(e.what());
    }
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

struct
path_edit_consumer
{
  virtual void add_file(file_path const & a) = 0;
  virtual void delete_file(file_path const & d) = 0;
  virtual void delete_dir(file_path const & d) = 0;
  virtual void rename_file(file_path const & a, file_path const & b) = 0;
  virtual void rename_dir(file_path const & a, file_path const & b) = 0;
  virtual ~path_edit_consumer() {}
};

static void
play_back_rearrangement(change_set::path_rearrangement const & pr,
			path_edit_consumer & pc)
{
  sanity_check_path_rearrangement(pr);

  L(F("playing back path_rearrangement with %d entries\n") 
    % pr.first.size());

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
	  L(F("skipping preserved %s %d : %s\n")
	    % (path_item_type(old_item) == change_set::ptype_directory ? "directory" : "file")
	    % curr % old_path);
	  continue;
	}
      
      L(F("playing back %s %d : %s -> %s\n")
	% (path_item_type(old_item) == change_set::ptype_directory ? "directory" : "file")
	% curr % old_path % new_path);
      
      if (null_name(path_item_name(old_item)))
	{
	  // an addition (which must be a file, not a directory)
	  I(! null_name(path_item_name(new_item)));
	  I(path_item_type(new_item) != change_set::ptype_directory);
	  pc.add_file(new_path);
	}
      else if (null_name(path_item_name(new_item)))
	{
	  // a deletion
	  I(! null_name(path_item_name(old_item)));
	  switch (path_item_type(new_item))
	    {
	    case change_set::ptype_directory:
	      pc.delete_dir(old_path);
	    case change_set::ptype_file:
	      pc.delete_file(old_path);
	    }	  
	}
      else
	{
	  // a generic rename
	  switch (path_item_type(new_item))
	    {
	    case change_set::ptype_directory:
	      pc.rename_dir(old_path, new_path);
	    case change_set::ptype_file:
	      pc.rename_file(old_path, new_path);
	    }
	}
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


static void
build_directory_map(change_set::path_state const & state,
		    directory_map & dir)
{
  dir.clear();
  for (change_set::path_state::const_iterator i = state.begin(); i != state.end(); ++i)
    {
      change_set::tid curr = path_state_tid(i);
      change_set::path_item item = path_state_item(i);
      change_set::tid parent = path_item_parent(item);
      file_path name = path_item_name(item);
      change_set::ptype type = path_item_type(item);	    
      dnode(dir, parent)->insert(std::make_pair(name,std::make_pair(type, curr)));
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


  void resolve_path_helper(std::vector<file_path> const & self_path,
			   change_set::tid & self_tid,
			   directory_map & self_dir,
			   directory_map & other_dir,
			   change_set::path_state & self_state,
			   change_set::path_state & other_state)
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

  //
  // this takes a pair of paths of the form
  //
  // (old_path[0], old_path[1], ..., old_path[n-1], old_path[n])
  // (new_path[0], new_path[1], ..., new_path[m-1], new_path[m])
  // 
  // and returns the 2 tids referring to the elements old_path[n] and
  // new_path[m], adding any intermediate directories to this->old_dir,
  // this->new_dir, and both sides of this->pr in the process.
  //
  // this is more complicated than it looks, because we are working with
  // imperfect information: the elements of each path prefix may have the
  // same names or different names; may exist or not exist in the mappings
  // already; and may have the same tids or different tids in the mappings.
  //
  // thus we take the strategy of using a helper function which resolves
  // each path in terms of both the context it's intended for *and the
  // opposing context* : new paths are resolved in terms of the new path
  // context and the old one.
  //
  // both contexts are extended during resolving as necessary. both
  // contexts should always contain the identical set of tids (at different
  // names, perhaps).
  //

  void resolve_paths(std::vector<file_path> const & old_path,
		     std::vector<file_path> const & new_path,
		     change_set::tid & old_tid,
		     change_set::tid & new_tid)
  {
    resolve_path_helper(old_path, old_tid, old_dir, new_dir, pr.first, pr.second);
    resolve_path_helper(new_path, new_tid, new_dir, old_dir, pr.second, pr.first);
  }
  
  //
  // this takes a path of the form
  //
  //  "p[0]/p[1]/.../p[n-1]/p[n]"
  //
  // and fills in a vector of paths corresponding to p[0] ... p[n-1],
  // along with a separate "leaf path" for element p[n]. 
  //

  void split_path(file_path const & p,
		  std::vector<file_path> & prefix,
		  file_path & leaf_path)
  {
    prefix.clear();
    fs::path tmp(p());
    std::copy(tmp.begin(), tmp.end(), std::inserter(prefix, prefix.begin()));
    I(prefix.size() > 0);
    leaf_path = prefix.back();
    prefix.pop_back();
  }

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
  // this takes a ptype indicator and a pair of (prefix,leaf) pairs and
  // records them in both the directory_map and path_rearrangement
  // structures managed by this path_edit_analyzer.
  //

  void record_rearrangement(change_set::ptype ty, 
			    std::vector<file_path> const & old_prefix, 
			    file_path old_leaf_path, 
			    std::vector<file_path> const & new_prefix, 
			    file_path new_leaf_path)
  {
    change_set::tid old_parent_tid, new_parent_tid;
    resolve_paths(old_prefix, new_prefix, 
		  old_parent_tid, new_parent_tid);

    boost::shared_ptr<directory_node> old_dnode = dnode(old_dir, old_parent_tid);
    boost::shared_ptr<directory_node> new_dnode = dnode(new_dir, new_parent_tid);

    directory_node::const_iterator existing_old_entry = old_dnode->find(old_leaf_path);
    directory_node::const_iterator existing_new_entry = new_dnode->find(new_leaf_path);

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

  virtual void add_file(file_path const & a) 
  {
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(a, prefix, leaf_path);
    record_rearrangement(change_set::ptype_file, 
			 prefix, null_path, 
			 prefix, leaf_path);
  }

  virtual void delete_file(file_path const & d)
  {
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(d, prefix, leaf_path);
    record_rearrangement(change_set::ptype_file, 
			 prefix, leaf_path, 
			 prefix, null_path);
  }


  virtual void delete_dir(file_path const & d)
  {
    std::vector<file_path> prefix;
    file_path leaf_path;
    split_path(d, prefix, leaf_path);
    record_rearrangement(change_set::ptype_directory, 
			 prefix, leaf_path, 
			 prefix, null_path);
  }

  virtual void rename_file(file_path const & a, file_path const & b)
  {
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
  virtual void resolve_conflict(change_set::ptype ty, 
				std::vector<file_path> const & old_prefix, file_path old_leaf_path, 
				std::vector<file_path> const & new_prefix, file_path new_leaf_path,
				bool found_old_entry, bool found_new_entry,
				change_set::tid existing_old_tid, change_set::ptype existing_old_ty,
				change_set::tid existing_new_tid, change_set::ptype existing_new_ty)
  {
    throw informative_failure("tree layout conflict in appender\n");
    // FIXME: put in some conflict resolution logic here 
  }    
};


struct
path_edit_merger : public path_edit_analyzer
{
  path_edit_merger(change_set::path_rearrangement & pr) 
    : path_edit_analyzer(pr)
  {
  }
  virtual ~path_edit_merger() {}  
  virtual void resolve_conflict(change_set::ptype ty, 
				std::vector<file_path> const & old_prefix, file_path old_leaf_name, 
				std::vector<file_path> const & new_prefix, file_path new_leaf_name,
				bool found_old_entry, bool found_new_entry,
				change_set::tid existing_old_tid, change_set::ptype existing_old_ty,
				change_set::tid existing_new_tid, change_set::ptype existing_new_ty)
  {
    throw informative_failure("tree layout conflict in merger\n");
    // FIXME: put in some conflict resolution logic here 
  } 
};


void
merge_change_sets(change_set const & a,
		  change_set const & b,
		  change_set & merged)
{
  merged = a;
  path_edit_merger mer(merged.rearrangement);
  play_back_rearrangement(b.rearrangement, mer);

  // FIXME: need to actually merge deltas here
  //  delta_renamer dn(merged.deltas);
  // play_back_rearrangement(b.rearrangement, dn);  
}

void
concatenate_change_sets(change_set const & a,
			change_set const & b,
			change_set & concatenated)
{
  concatenated = a;
  path_edit_appender app(concatenated.rearrangement);
  play_back_rearrangement(b.rearrangement, app);

  // FIXME: need to actually concatenate deltas here
  //  delta_renamer dn(concatenated.deltas);
  // play_back_rearrangement(b.rearrangement, dn);  
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
  path_edit_appender app(pr);
  parse_path_edits(pa, app);
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
