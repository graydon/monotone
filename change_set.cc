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
#include <boost/lexical_cast.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "interner.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"

// our analyses in this file happen on one of two families of
// related structures: a path_analysis or a directory_map.
//
// a path_analysis corresponds exactly to a normalized 
// path_rearrangement; they are two ways of writing the
// same information
//
// the path_analysis stores two path_states. each path_state is a map from
// transient identifiers (tids) to items. each item represents a semantic
// element of a filesystem which has a type (file or directory), a name,
// and a parent link (another tid). tids should be unique across a
// path_analysis.

typedef enum { ptype_directory, ptype_file } ptype;
typedef u64 tid;
static tid root_tid = 0;

struct
tid_source
{
  tid ctr;
  tid_source() : ctr(root_tid + 1) {}
  tid next() { I(ctr != UINT64_C(0xffffffffffffffff)); return ctr++; }
};

struct
path_item
{
  tid parent;
  ptype ty;
  file_path name;      
  path_item() {}
  path_item(tid p, ptype t, file_path const & n);
  path_item(path_item const & other);
  path_item const & operator=(path_item const & other);
  bool operator==(path_item const & other) const;
};

typedef std::map<tid, path_item> path_state;
typedef std::map<tid,tid> state_renumbering;
typedef std::pair<path_state, path_state> path_analysis;

// nulls and tests

static file_path null_path;
static file_id null_ident;

// a directory_map is a more "normal" representation of a directory tree,
// which you can traverse more conveniently from root to tip
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]
//
//     tid ->  [ name -> (ptype, tid),
//               name -> (ptype, tid),
//               ...                  ]

typedef std::map<file_path, std::pair<ptype,tid> > directory_node;
typedef std::map<tid, boost::shared_ptr<directory_node> > directory_map;

static ptype
directory_entry_type(directory_node::const_iterator const & i)
{
  return i->second.first;
}

static tid
directory_entry_tid(directory_node::const_iterator const & i)
{
  return i->second.second;
}


void 
change_set::add_file(file_path const & a)
{
  rearrangement.added_files.insert(a);
}

void 
change_set::add_file(file_path const & a, file_id const & ident)
{
  rearrangement.added_files.insert(a);
  deltas.insert(std::make_pair(a, std::make_pair(null_ident, ident)));
}

void 
change_set::apply_delta(file_path const & path, 
			file_id const & src, 
			file_id const & dst)
{
  deltas.insert(std::make_pair(path, std::make_pair(src, dst)));
}

void 
change_set::delete_file(file_path const & d)
{
  rearrangement.deleted_files.insert(d);
}

void 
change_set::delete_dir(file_path const & d)
{
  rearrangement.deleted_dirs.insert(d);
}

void 
change_set::rename_file(file_path const & a, file_path const & b)
{
  rearrangement.renamed_files.insert(std::make_pair(a,b));
}

void 
change_set::rename_dir(file_path const & a, file_path const & b)
{
  rearrangement.renamed_dirs.insert(std::make_pair(a,b));
}


bool 
change_set::path_rearrangement::operator==(path_rearrangement const & other) const
{
  return deleted_files == other.deleted_files &&
    deleted_dirs == other.deleted_dirs &&
    renamed_files == other.renamed_files &&
    renamed_dirs == other.renamed_dirs &&
    added_files == other.added_files;
}

bool 
change_set::path_rearrangement::empty() const
{
  return deleted_files.empty() &&
    deleted_dirs.empty() &&
    renamed_files.empty() &&
    renamed_dirs.empty() &&
    added_files.empty();
}

bool 
change_set::empty() const
{
  return deltas.empty() && rearrangement.empty();
}

bool 
change_set::operator==(change_set const & other) const
{
  return rearrangement == other.rearrangement &&
    deltas == other.deltas;    
}


// simple accessors

inline tid const & 
path_item_parent(path_item const & p) 
{ 
  return p.parent; 
}

inline ptype const & 
path_item_type(path_item const & p) 
{ 
  return p.ty; 
}

inline file_path const & 
path_item_name(path_item const & p) 
{ 
  return p.name; 
}

inline tid
path_state_tid(path_state::const_iterator i)
{
  return i->first;
}

inline path_item const &
path_state_item(path_state::const_iterator i)
{
  return i->second;
}



// structure dumping 
/*

static void
dump_renumbering(std::string const & s,
		 state_renumbering const & r)
{
  L(F("BEGIN dumping renumbering '%s'\n") % s);
  for (state_renumbering::const_iterator i = r.begin();
       i != r.end(); ++i)
    {
      L(F("%d -> %d\n") % i->first % i->second);
    }
  L(F("END dumping renumbering '%s'\n") % s);
}

static void
dump_state(std::string const & s,
	   path_state const & st)
{
  L(F("BEGIN dumping state '%s'\n") % s);
  for (path_state::const_iterator i = st.begin();
       i != st.end(); ++i)
    {
      L(F("state '%s': tid %d, parent %d, type %s, name %s\n")
	% s
	% path_state_tid(i) 
	% path_item_parent(path_state_item(i))
	% (path_item_type(path_state_item(i)) == ptype_directory ? "dir" : "file")
	% path_item_name(path_state_item(i)));
    }
  L(F("END dumping state '%s'\n") % s);
}

static void
dump_analysis(std::string const & s,
	      path_analysis const & t)
{
  L(F("BEGIN dumping tree '%s'\n") % s);
  dump_state(s + " first", t.first);
  dump_state(s + " second", t.second);
  L(F("END dumping tree '%s'\n") % s);
}
*/


//  sanity checking 

static void
sanity_check_path_item(path_item const & pi)
{
  fs::path tmp = mkpath(pi.name());
  I(null_name(pi.name) || ++(tmp.begin()) == tmp.end());
}

static void
confirm_proper_tree(path_state const & ps)
{
  std::set<tid> confirmed;
  I(ps.find(root_tid) == ps.end());
  for (path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      tid curr = i->first;
      path_item item = i->second;
      std::set<tid> ancs; 

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
	      path_state::const_iterator j = ps.find(curr);
	      I(j != ps.end());
	      item = path_state_item(j);
	      I(path_item_type(item) == ptype_directory);
	    }
	}
      std::copy(ancs.begin(), ancs.end(), 
		inserter(confirmed, confirmed.begin()));      
    }
  I(confirmed.find(root_tid) == confirmed.end());
}

static void
confirm_unique_entries_in_directories(path_state const & ps)
{  
  std::set< std::pair<tid,file_path> > entries;
  for (path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      std::pair<tid,file_path> p = std::make_pair(i->first, path_item_name(i->second));
      I(entries.find(p) == entries.end());
      entries.insert(p);
    }
}

static void
sanity_check_path_state(path_state const & ps)
{
  confirm_proper_tree(ps);
  confirm_unique_entries_in_directories(ps);
}

path_item::path_item(tid p, ptype t, file_path const & n) 
  : parent(p), ty(t), name(n) 
{
  sanity_check_path_item(*this);
}

path_item::path_item(path_item const & other) 
  : parent(other.parent), ty(other.ty), name(other.name) 
{
  sanity_check_path_item(*this);
}

path_item const & path_item::operator=(path_item const & other)
{
  parent = other.parent;
  ty = other.ty;
  name = other.name;
  sanity_check_path_item(*this);
  return *this;
}

bool path_item::operator==(path_item const & other) const
{
  return this->parent == other.parent &&
    this->ty == other.ty &&
    this->name == other.name;
}


static void
check_states_agree(path_state const & p1,
		   path_state const & p2)
{
  path_analysis pa;
  pa.first = p1;
  pa.second = p2;
  // dump_analysis("agreement", pa);
  for (path_state::const_iterator i = p1.begin(); i != p1.end(); ++i)
    {
      path_state::const_iterator j = p2.find(i->first);
      I(j != p2.end());
      I(path_item_type(i->second) == path_item_type(j->second));
      //       I(! (null_name(path_item_name(i->second))
      // 	   &&
      // 	   null_name(path_item_name(j->second))));
    }
}

static void
sanity_check_path_analysis(path_analysis const & pr)
{
  sanity_check_path_state(pr.first);
  sanity_check_path_state(pr.second);
  check_states_agree(pr.first, pr.second);
  check_states_agree(pr.second, pr.first);
}


// construction helpers

static boost::shared_ptr<directory_node>
new_dnode()
{
  return boost::shared_ptr<directory_node>(new directory_node());
}

static boost::shared_ptr<directory_node>
dnode(directory_map & dir, tid t)
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
get_full_path(path_state const & state,
	      tid t,
	      std::vector<file_path> & pth)
{
  std::vector<file_path> tmp;
  while(t != root_tid)
    {
      path_state::const_iterator i = state.find(t);
      I(i != state.end());
      tmp.push_back(path_item_name(i->second));
      t = path_item_parent(i->second);
    }
  pth.clear();
  std::copy(tmp.rbegin(), tmp.rend(), inserter(pth, pth.begin()));
}

static void
get_full_path(path_state const & state,
	      tid t,
	      file_path & pth)
{
  std::vector<file_path> tmp;
  get_full_path(state, t, tmp);
  // L(F("got %d-entry path for tid %d\n") % tmp.size() % t);
  compose_path(tmp, pth);
}

static void
clear_rearrangement(change_set::path_rearrangement & pr)
{
  pr.deleted_files.clear();
  pr.deleted_dirs.clear();
  pr.renamed_files.clear();
  pr.renamed_dirs.clear();
  pr.added_files.clear();
}

static void
clear_change_set(change_set & cs)
{
  clear_rearrangement(cs.rearrangement);
  cs.deltas.clear();
}

static void 
compose_rearrangement(path_analysis const & pa,
		      change_set::path_rearrangement & pr)
{
  clear_rearrangement(pr);

  for (path_state::const_iterator i = pa.first.begin();
       i != pa.first.end(); ++i)
    {      
      tid curr(path_state_tid(i));
      std::vector<file_path> old_name, new_name;
      file_path old_path, new_path;
     
      path_state::const_iterator j = pa.second.find(curr);
      I(j != pa.second.end());
      path_item old_item(path_state_item(i));
      path_item new_item(path_state_item(j));

      // compose names
      if (!null_name(path_item_name(old_item)))
	{
	  get_full_path(pa.first, curr, old_name);
	  compose_path(old_name, old_path);
	}

      if (!null_name(path_item_name(new_item)))      
	{
	  get_full_path(pa.second, curr, new_name);
	  compose_path(new_name, new_path);
	}

      if (old_path == new_path)
	{
	  L(F("skipping preserved %s %d : '%s'\n")
	    % (path_item_type(old_item) == ptype_directory ? "directory" : "file")
	    % curr % old_path);
	  continue;
	}
      
      L(F("analyzing %s %d : '%s' -> '%s'\n")
	% (path_item_type(old_item) == ptype_directory ? "directory" : "file")
	% curr % old_path % new_path);
      
      if (null_name(path_item_name(old_item)))
	{
	  // an addition (which must be a file, not a directory)
	  I(! null_name(path_item_name(new_item)));
	  I(path_item_type(new_item) != ptype_directory);
	  pr.added_files.insert(new_path);
	}
      else if (null_name(path_item_name(new_item)))
	{
	  // a deletion
	  I(! null_name(path_item_name(old_item)));
	  switch (path_item_type(new_item))
	    {
	    case ptype_directory:
	      pr.deleted_dirs.insert(old_path);
	      break;
	    case ptype_file:
	      pr.deleted_files.insert(old_path);
	      break;
	    }	  
	}
      else
	{
	  // a generic rename
	  switch (path_item_type(new_item))
	    {
	    case ptype_directory:
	      pr.renamed_dirs.insert(std::make_pair(old_path, new_path));
	      break;
	    case ptype_file:
	      pr.renamed_files.insert(std::make_pair(old_path, new_path));
	      break;
	    }
	}
    }
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

static bool
lookup_path(std::vector<file_path> const & pth,
	    directory_map const & dir,
	    tid & t)
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
	    tid & t)
{
  std::vector<file_path> vec;
  fs::path tmp = mkpath(pth());
  std::copy(tmp.begin(), tmp.end(), std::inserter(vec, vec.begin()));
  return lookup_path(vec, dir, t);
}

static tid
ensure_entry(directory_map & dmap,
	     path_state & state,	     
	     tid dir_tid,
	     ptype entry_ty,
	     file_path const & entry,
	     tid_source & ts)
{
  I(! null_name(entry));

  boost::shared_ptr<directory_node> node = dnode(dmap, dir_tid);
  directory_node::const_iterator node_entry = node->find(entry);

  if (node_entry != node->end())
    {
      I(node_entry->second.first == entry_ty);
      return node_entry->second.second;
    }
  else
    {
      tid new_tid = ts.next();
      state.insert(std::make_pair(new_tid, path_item(dir_tid, entry_ty, entry)));
      node->insert(std::make_pair(entry, std::make_pair(entry_ty, new_tid)));
      return new_tid;
    }
}

static tid
ensure_dir_in_map (std::vector<file_path> pth,
		   directory_map & dmap,
		   path_state & state,
		   tid_source & ts)
{
  tid dir_tid = root_tid;
  for (std::vector<file_path>::const_iterator p = pth.begin();
       p != pth.end(); ++p)
    {
      dir_tid = ensure_entry(dmap, state, dir_tid, 
			     ptype_directory, *p, ts);
    }
  return dir_tid;
}

static tid
ensure_dir_in_map (file_path const & path,
		   directory_map & dmap,
		   path_state & state,
		   tid_source & ts)
{
  std::vector<file_path> components;
  split_path(path, components);
  return ensure_dir_in_map(components, dmap, state, ts);
}

static tid
ensure_file_in_map (file_path const & path,
		    directory_map & dmap,
		    path_state & state,
		    tid_source & ts)
{
  std::vector<file_path> prefix;  
  file_path leaf_path;
  split_path(path, prefix, leaf_path);
  
  I(! null_name(leaf_path));
  tid dir_tid = ensure_dir_in_map(prefix, dmap, state, ts);
  return ensure_entry(dmap, state, dir_tid, ptype_file, leaf_path, ts);
}

static void
ensure_entries_exist (path_state const & self_state,
		      directory_map & other_dmap,
		      path_state & other_state,
		      tid_source & ts)
{
  for (path_state::const_iterator i = self_state.begin(); 
       i != self_state.end(); ++i)
    {
      if (other_state.find(path_state_tid(i)) != other_state.end())
	continue;

      if (null_name(path_item_name(path_state_item(i))))
	continue;

      file_path full;
      get_full_path(self_state, path_state_tid(i), full);
      switch (path_item_type(path_state_item(i)))
	{
	case ptype_directory:
	  ensure_dir_in_map(full, other_dmap, other_state, ts);
	  break;

	case ptype_file:
	  ensure_file_in_map(full, other_dmap, other_state, ts);
	  break;
	}
    }
}


static void
apply_state_renumbering(state_renumbering const & renumbering,
			path_state & state)
{
  sanity_check_path_state(state);  
  path_state tmp(state);
  state.clear();

  for (path_state::const_iterator i = tmp.begin(); i != tmp.end(); ++i)
    {
      path_item item = path_state_item(i);
      tid t = path_state_tid(i);

      state_renumbering::const_iterator j = renumbering.find(t);
      if (j != renumbering.end())
	t = j->second;

      j = renumbering.find(item.parent);
      if (j != renumbering.end())
	item.parent = j->second;

      state.insert(std::make_pair(t, item));
    }
  sanity_check_path_state(state);
}

static void
apply_state_renumbering(state_renumbering const & renumbering,
			path_analysis & pa)
{
  apply_state_renumbering(renumbering, pa.first);
  apply_state_renumbering(renumbering, pa.second);
}
			

// this takes a path in the path space defined by input_dir and rebuilds it
// in the path space defined by output_space, including any changes to
// parents in the path (rather than directly to the path leaf name).  it
// therefore *always* succeeds; sometimes it does nothing if there's no
// affected parent, but you always get a rebuilt path in the output space.

static void
reconstruct_path(file_path const & input,
		 directory_map const & input_dir,
		 path_state const & output_space,
		 file_path & output)
{
  std::vector<file_path> vec;
  std::vector<file_path> rebuilt;

  // L(F("reconstructing path '%s' under analysis\n") % input);
  
  fs::path tmp = mkpath(input());
  std::copy(tmp.begin(), tmp.end(), std::inserter(vec, vec.begin()));

  tid t = root_tid;
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
	path_state::const_iterator i = output_space.find(directory_entry_tid(entry));
	I(i != output_space.end());
	if (null_name(path_item_name(path_state_item(i))))
	  {
	    // L(F("input path element '%s' is null in output space, mapping truncated\n") % *pth);
	    break;
	  }
      }
 
      // L(F("resolved entry '%s' in reconstruction\n") % *pth);
      ++pth;
      t = directory_entry_tid(entry);

      if (directory_entry_type(entry) != ptype_directory)
	break;
    }
      
  get_full_path(output_space, t, rebuilt);
  
  while(pth != vec.end())
    {
      // L(F("copying tail entry '%s' in reconstruction\n") % *pth);
      rebuilt.push_back(*pth);
      ++pth;
    }

  compose_path(rebuilt, output);
  // L(F("reconstructed path '%s' as '%s'\n") % input % output);
}


static void
build_directory_map(path_state const & state,
		    directory_map & dir)
{
  sanity_check_path_state(state);
  dir.clear();
  // L(F("building directory map for %d entries\n") % state.size());
  for (path_state::const_iterator i = state.begin(); i != state.end(); ++i)
    {
      tid curr = path_state_tid(i);
      path_item item = path_state_item(i);
      tid parent = path_item_parent(item);
      file_path name = path_item_name(item);
      ptype type = path_item_type(item);	    
      //       L(F("adding entry %s (%s %d) to directory node %d\n") 
      // 	% name % (type == ptype_directory ? "dir" : "file") % curr % parent);
      dnode(dir, parent)->insert(std::make_pair(name,std::make_pair(type, curr)));
    }
}


static void 
analyze_rearrangement(change_set::path_rearrangement const & pr,
		      path_analysis & pa,
		      tid_source & ts)
{
  directory_map first_map, second_map;
  state_renumbering renumbering;
  std::set<tid> damaged_in_first, damaged_in_second;

  pa.first.clear();
  pa.second.clear();

  for (std::set<file_path>::const_iterator f = pr.deleted_files.begin();
       f != pr.deleted_files.end(); ++f)
    {
      tid x = ensure_file_in_map(*f, first_map, pa.first, ts);
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_file, null_path)));
      damaged_in_first.insert(x);
    }

  for (std::set<file_path>::const_iterator d = pr.deleted_dirs.begin();
       d != pr.deleted_dirs.end(); ++d)
    {
      tid x = ensure_dir_in_map(*d, first_map, pa.first, ts);
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_directory, null_path)));
      damaged_in_first.insert(x);
    }

  for (std::map<file_path,file_path>::const_iterator rf = pr.renamed_files.begin();
       rf != pr.renamed_files.end(); ++rf)
    {
      tid a = ensure_file_in_map(rf->first, first_map, pa.first, ts);
      tid b = ensure_file_in_map(rf->second, second_map, pa.second, ts);
      I(renumbering.find(a) == renumbering.end());
      renumbering.insert(std::make_pair(b,a));
      damaged_in_first.insert(a);
      damaged_in_second.insert(b);
    }

  for (std::map<file_path,file_path>::const_iterator rd = pr.renamed_dirs.begin();
       rd != pr.renamed_dirs.end(); ++rd)
    {
      tid a = ensure_dir_in_map(rd->first, first_map, pa.first, ts);
      tid b = ensure_dir_in_map(rd->second, second_map, pa.second, ts);
      I(renumbering.find(a) == renumbering.end());
      renumbering.insert(std::make_pair(b,a));
      damaged_in_first.insert(a);
      damaged_in_second.insert(b);
    }

  for (std::set<file_path>::const_iterator a = pr.added_files.begin();
       a != pr.added_files.end(); ++a)
    {
      tid x = ensure_file_in_map(*a, second_map, pa.second, ts);
      pa.first.insert(std::make_pair(x, path_item(root_tid, ptype_file, null_path)));
      damaged_in_second.insert(x);
    }

  // we now have two states which probably have a number of entries in
  // common. we know already of an interesting set of entries they have in
  // common: all the renamed_foo entries. for each such renamed_foo(a,b)
  // entry, we made an entry in our state_renumbering of the form b->a,
  // while building the states.

  // dump_analysis("analyzed", pa);
  // dump_renumbering("first", renumbering);
  apply_state_renumbering(renumbering, pa.second);
  build_directory_map(pa.first, first_map);
  build_directory_map(pa.second, second_map);
  renumbering.clear();
  // dump_analysis("renumbered once", pa);
  
  // that only gets us half way, though:
  //
  // - every object which was explicitly moved (thus stayed alive) has been
  //   renumbered in re.second to have the same tid as in re.first
  //
  // - every object which was merely mentionned in passing -- say due to
  //   being an intermediate directory in a path -- and was not moved, still 
  //   has differing tids in re.first and re.second (or worse, may only
  //   even have an *entry* in one of them)
  //
  // the second point here is what we need to correct: if a path didn't
  // move, wasn't destroyed, and wasn't added, we want it to have the same
  // tid. but that's a relatively easy condition to check; we've been
  // keeping sets of all the objects which were damaged on each side of
  // this business anyways.


  // pass #1 makes sure that all the entries in each state *exist* within
  // the other state, even if they have the wrong numbers

  ensure_entries_exist (pa.first, second_map, pa.second, ts);
  ensure_entries_exist (pa.second, first_map, pa.first, ts);

  // pass #2 identifies common un-damaged elements from 2->1 and inserts
  // renumberings

  for (path_state::const_iterator i = pa.second.begin(); 
       i != pa.second.end(); ++i)
    {
      tid first_tid, second_tid;
      second_tid = path_state_tid(i);
      file_path full;
      if (pa.first.find(second_tid) != pa.first.end())
	continue;
      get_full_path(pa.second, second_tid, full);
      if (damaged_in_second.find(second_tid) != damaged_in_second.end())
	continue;
      if (null_name(path_item_name(path_state_item(i))))
	continue;
      I(lookup_path(full, first_map, first_tid));
      renumbering.insert(std::make_pair(second_tid, first_tid));
    }

  // dump_renumbering("second", renumbering);
  apply_state_renumbering(renumbering, pa.second);
  // dump_analysis("renumbered again", pa);

  // that should be the whole deal; if we don't have consensus at this
  // point we have done something wrong.
  sanity_check_path_analysis (pa);
}


void
normalize_change_set(change_set & norm)
{  
  path_analysis tmp;
  tid_source ts;

  // L(F("normalizing changeset\n"));
  analyze_rearrangement(norm.rearrangement, tmp, ts);
  clear_rearrangement(norm.rearrangement);
  compose_rearrangement(tmp, norm.rearrangement);
}


// begin stuff related to concatenation

static void 
index_entries(path_state const & state, 
	      std::map<file_path, tid> & files, 
	      std::map<file_path, tid> & dirs)
{
  for (path_state::const_iterator i = state.begin(); 
       i != state.end(); ++i)
    {
      file_path full;
      path_item item = path_state_item(i);
      get_full_path(state, path_state_tid(i), full);

      if (null_name(path_item_name(item))) 
	continue;

      switch (path_item_type(item))
	{
	case ptype_directory:
	  files.insert(std::make_pair(full, path_state_tid(i)));
	  break;

	case ptype_file:
	  dirs.insert(std::make_pair(full, path_state_tid(i)));
	  break;
	}
    }  
}

// this takes every (p1,t1) entry in b and, if (p1,t2) it exists in a, 
// inserts (t1,t2) in the rename set. in other words, it constructs the
// renumbering from b->a
static void 
extend_renumbering_from_path_identities(std::map<file_path, tid> const & a,
					std::map<file_path, tid> const & b,
					state_renumbering & renumbering)
{
  for (std::map<file_path, tid>::const_iterator i = b.begin();
       i != b.end(); ++i)
    {
      I(! null_name(i->first));
      std::map<file_path, tid>::const_iterator j = a.find(i->first);
      if (j == a.end())
	continue;
      renumbering.insert(std::make_pair(i->second, j->second));
    }
}

static void
extend_state(path_state const & src, 
	     path_state & dst)
{
  for (path_state::const_iterator i = src.begin();
       i != src.end(); ++i)
    {
      if (dst.find(path_state_tid(i)) == dst.end())
	dst.insert(*i);
    }
}

static void
ensure_tids_disjoint(path_analysis const & a, 
		     path_analysis const & b)
{
  for (path_state::const_iterator i = a.first.begin();
       i != a.first.end(); ++i)
    {
      I(b.first.find(path_state_tid(i)) == b.first.end());
    }  
  for (path_state::const_iterator i = b.first.begin();
       i != b.first.end(); ++i)
    {
      I(a.first.find(path_state_tid(i)) == a.first.end());
    }  
}

static void
concatenate_disjoint_analyses(path_analysis const & a,
			      path_analysis const & b,
			      path_analysis & concatenated)
{
  std::map<file_path, tid> a_second_files, a_second_dirs;
  std::map<file_path, tid> b_first_files, b_first_dirs;
  path_analysis a_tmp(a), b_tmp(b);
  state_renumbering renumbering;
  
  // the trick here is that a.second and b.first supposedly refer to the
  // same state-of-the-world, so all we need to do is:
  //
  // - confirm that both analyses have disjoint tids
  // - work out which tids in b to identify with tids in a
  // - renumber b
  //
  // - copy a.first -> concatenated.first
  // - insert all elements of b.first not already in concatenated.first
  // - copy b.second -> concatenated.second
  // - insert all elements of a.second not already in concatenated.second

  ensure_tids_disjoint(a_tmp, b_tmp);

  index_entries(a_tmp.second, a_second_files, a_second_dirs);
  index_entries(b_tmp.first, b_first_files, b_first_dirs);

  extend_renumbering_from_path_identities(a_second_files, b_first_files, renumbering);
  extend_renumbering_from_path_identities(a_second_dirs, b_first_dirs, renumbering);

  //   dump_analysis("state A", a_tmp);
  //   dump_analysis("state B", b_tmp);
  //   dump_renumbering("concatenation", renumbering);
  apply_state_renumbering(renumbering, b_tmp);

  concatenated.first = a_tmp.first;
  concatenated.second = b_tmp.second;

  extend_state(b_tmp.first, concatenated.first);
  extend_state(a_tmp.second, concatenated.second);

  sanity_check_path_analysis(concatenated);
}

void
concatenate_change_sets(change_set const & a,
			change_set const & b,
			change_set & concatenated)
{
  L(F("concatenating change sets\n"));

  tid_source ts;
  path_analysis a_analysis, b_analysis, concatenated_analysis;

  analyze_rearrangement(a.rearrangement, a_analysis, ts);
  analyze_rearrangement(b.rearrangement, b_analysis, ts);

  concatenate_disjoint_analyses(a_analysis, 
				b_analysis,
				concatenated_analysis);

  compose_rearrangement(concatenated_analysis, 
			concatenated.rearrangement);

  // now process the deltas

  concatenated.deltas.clear();
  directory_map b_src_map;
  L(F("concatenating %d and %d deltas\n")
    % a.deltas.size() % b.deltas.size());
  build_directory_map(b_analysis.first, b_src_map);

  // first rename a's deltas under the rearrangement of b
  for (change_set::delta_map::const_iterator del = a.deltas.begin();
       del != a.deltas.end(); ++del)
    {
      file_path new_pth;
      L(F("processing delta on %s\n") % delta_entry_path(del));
      reconstruct_path(delta_entry_path(del), b_src_map, b_analysis.second, new_pth);
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

  L(F("finished concatenation\n")); 
}

// end stuff related to concatenation


// begin stuff related to merging


static void
extend_renumbering_via_added_files(path_analysis const & a, 
				   path_analysis const & b, 
				   state_renumbering & renumbering)
{
  directory_map a_second_map;
  build_directory_map(a.second, a_second_map);
  
  for (path_state::const_iterator i = b.first.begin(); 
       i != b.first.end(); ++i)
    {
      path_item item = path_state_item(i);
      if (path_item_type(item) == ptype_file && null_name(path_item_name(item)))
	{
	  path_state::const_iterator j = b.second.find(path_state_tid(i));
	  I(j != b.second.end());
	  file_path leaf_name(path_item_name(path_state_item(j)));
	  I(path_item_type(path_state_item(j)) == ptype_file);
	  if (! null_name(leaf_name))
	    {
	      tid added_parent_tid = path_item_parent(path_state_item(j));
	      directory_map::const_iterator dirent = a_second_map.find(added_parent_tid);
	      if (dirent != a_second_map.end())
		{
		  
		  boost::shared_ptr<directory_node> node = dirent->second;
		  directory_node::const_iterator entry = node->find(leaf_name);
		  if (entry != node->end() && directory_entry_type(entry) == ptype_file)
		    {
		      renumbering.insert(std::make_pair(path_state_tid(i), 
							directory_entry_tid(entry)));
		    }
		}
	    }
	}
    }
}

static bool
find_item(tid t, path_state const & ps, 
	  path_item & item)
{
  path_state::const_iterator i = ps.find(t);
  if (i == ps.end())
    return false;
  item = path_state_item(i);
  return true;
}

static bool
find_items(tid t, path_analysis const & pa, 
	   path_item & first, path_item & second)
{
  if (find_item(t, pa.first, first))
    {
      I(find_item(t, pa.second, second));
      I(path_item_type(first) == path_item_type(second));
      return true;
    }
  else
    {
      I(!find_item(t, pa.second, second));
      return false;
    }
}

static void
resolve_conflict(tid t, ptype ty,
		 path_analysis const & a_tmp, 
		 path_analysis const & b_tmp, 
		 path_item & resolved,
		 path_state & resolved_conflicts,
		 app_state & app)
{
  path_state::const_iterator i = resolved_conflicts.find(t);
  if (i != resolved_conflicts.end())
    {
      resolved = path_state_item(i);
    }
  else
    {
      file_path anc, a, b, res;
      get_full_path(a_tmp.first, t, anc);
      get_full_path(a_tmp.second, t, a);
      get_full_path(b_tmp.second, t, b);

      switch (ty) 
	{
	case ptype_file:
	  N(app.lua.hook_resolve_file_conflict(anc, a, b, res),
	    F("unable to resolve file conflict '%s' -> '%s' vs. '%s'") % anc % a % b);
	  break;
	case ptype_directory:
	  N(app.lua.hook_resolve_dir_conflict(anc, a, b, res),
	    F("unable to resolve dir conflict '%s' -> '%s' vs. '%s'") % anc % a % b);
	  break;
	}

      N((res == a || res == b), 
	F("illegal conflict resolution '%s', wanted '%s' or '%s'\n") % res % a % b);

      if (res == a)
	I(find_item(t, a_tmp.second, resolved));
      else
	I(find_item(t, b_tmp.second, resolved));
      
      resolved_conflicts.insert(std::make_pair(t, resolved));
    }      
}

static void
ensure_no_rename_clobbers(path_analysis const & a, 
			  path_analysis const & b)
{
  // there is a special non-mergable pair of changes which we need
  // to identify here: 
  //
  //   tid i : x -> y   in change A
  //   tid j : z -> x   in change B
  //
  // on the surface it looks like it ought to be mergable, since there is
  // no conflict in the tids. except for one problem: B effectively
  // clobbered i with j. there is nothing you can append to change B to
  // revive the identity of i; in fact you risk having i and j identified
  // if you form the naive merge concatenation BA. indeed, since A and B
  // both supposedly start in the same state (in which i occupies name x),
  // it really ought not to be possible to form B; you should have to
  // accompany it with some sort of statement about the fate of i.
  //
  // as it stands, we're going to fault when we see this. if it turns out
  // that there's a legal way of constructing such changes, one option is
  // to synthesize a delete of i in B; essentially read "z->x" as an
  // implicit "delete x first if it exists in post-state".
  //
  // however, for the time being this is a fault because we believe they
  // should be globally illegal clobbers.

  directory_map b_first_map, b_second_map;
  build_directory_map (b.first, b_first_map);
  build_directory_map (b.second, b_second_map);
  tid a_tid, b_tid;

  for (path_state::const_iterator i = a.first.begin(); 
       i != a.first.end(); ++i)
    {
      file_path anc_path, a_second_path;
      a_tid = path_state_tid(i);
      get_full_path(a.first, a_tid, anc_path);

      if (! lookup_path(anc_path, b_first_map, b_tid))
	{
	  file_path b_second_path;
	  reconstruct_path(anc_path, b_first_map, b.second, b_second_path);

	  N(! lookup_path(b_second_path, b_second_map, b_tid),
	    (F("tid %d (%s) clobbered tid %d (%s)\n")
	     % b_tid % b_second_path 
	     % a_tid % anc_path));
	}
    }

}

static void
project_missing_changes(path_analysis const & a_tmp, 
			path_analysis const & b_tmp, 
			path_analysis & b_merged, 
			path_state & resolved_conflicts,
			app_state & app)
{

  // for each tid t adjusted in a:
  //   - if t exists in b:
  //     - if the change to t in b == change in a, skip
  //     - else resolve conflict
  //       - if conflict resolved in favour of a, append to merged
  //       - if resolved in favour of b, skip
  //   - else (no t in b) insert a's change to t in merged

  for (path_state::const_iterator i = a_tmp.first.begin();
       i != a_tmp.first.end(); ++i)
    {
      tid t = path_state_tid(i);
      path_item a_first_item, a_second_item;
      path_item b_first_item, b_second_item;
      I(find_items(t, a_tmp, a_first_item, a_second_item));
      if (find_items(t, b_tmp, b_first_item, b_second_item))
	{
	  I(a_first_item == b_first_item);
	  if (a_second_item == b_second_item)
	    {
	      L(F("skipping common change on %s (tid %d)\n") 
		% path_item_name(a_first_item) % t);
	    }
	  else if (a_first_item == a_second_item)
	    {
	      L(F("skipping neutral change of %s -> %s (tid %d)\n") 
		% path_item_name(a_first_item) 
		% path_item_name(a_second_item)
		% t);	      
	    }
	  else if (b_first_item == b_second_item)
	    {
	      L(F("propagating change on %s -> %s (tid %d)\n") 
		% path_item_name(b_first_item) 
		% path_item_name(b_second_item)
		% t);
	      b_merged.first.insert(std::make_pair(t, b_second_item));
	      b_merged.second.insert(std::make_pair(t, a_second_item));
	    }
	  else
	    {
	      // conflict
	      path_item resolved;
	      resolve_conflict(t, path_item_type(a_first_item), a_tmp, b_tmp, 
			       resolved, resolved_conflicts, app);
	      
	      if (resolved == a_second_item)
		{
		  L(F("conflict detected, resolved in A's favour\n"));
		  b_merged.first.insert(std::make_pair(t, b_second_item));
		  b_merged.second.insert(std::make_pair(t, a_second_item));
		}
	      else
		{
		  L(F("conflict detected, resolved in B's favour\n"));
		}
	    }
	}
      else
	{
	  // there was no entry in b at all for this tid, copy it
	  b_merged.first.insert(std::make_pair(t, a_first_item));
	  b_merged.second.insert(std::make_pair(t, a_second_item));
	}
    }

  // now drive through b.second's view of the directory structure, in case
  // some intermediate b-only directories showed up the preimages of
  // A-favoured conflicts.
  extend_state(b_tmp.second, b_merged.first);
  extend_state(b_merged.first, b_merged.second);
}

static void
rebuild_analysis(path_analysis const & src,
		 path_analysis & dst,
		 tid_source & ts)
{
  state_renumbering renumbering;
  
  for (path_state::const_iterator i = src.first.begin(); 
       i != src.first.end(); ++i)
    renumbering.insert(std::make_pair(path_state_tid(i), ts.next()));

  dst = src;
  apply_state_renumbering(renumbering, dst);
}

static void
merge_disjoint_analyses(path_analysis const & a,
			path_analysis const & b,
			path_analysis & a_renumbered,
			path_analysis & b_renumbered,
			path_analysis & a_merged,
			path_analysis & b_merged,
			app_state & app)
{
  tid_source ts;

  // we have anc->a and anc->b and we want to construct a->merged and
  // b->merged, leading to the eventual identity concatenate(a,a_merged) ==
  // concatenate(b,b_merged).  
  
  path_analysis a_tmp(a), b_tmp(b);
  state_renumbering renumbering;

  ensure_tids_disjoint(a_tmp, b_tmp);

  // fault on a particular class of mal-formed changesets
  ensure_no_rename_clobbers(a_tmp, b_tmp);
  ensure_no_rename_clobbers(b_tmp, a_tmp);

  // a.first and b.first refer to the same state-of-the-world. 
  //
  // we begin by driving all the entries in a.first into b.first and vice
  // versa.

  {
    directory_map a_first_map, b_first_map;
    build_directory_map(a_tmp.first, a_first_map);
    build_directory_map(b_tmp.first, b_first_map);
    ensure_entries_exist(a_tmp.first, b_first_map, b_tmp.first, ts);
    ensure_entries_exist(b_tmp.first, a_first_map, a_tmp.first, ts);
  }

  // we then drive any of the new arrivals in a.first to a.second, and
  // likewise on b

  {
    directory_map a_second_map, b_second_map;
    build_directory_map(a_tmp.second, a_second_map);
    build_directory_map(a_tmp.second, b_second_map);
    ensure_entries_exist(a_tmp.first, a_second_map, a_tmp.second, ts);
    ensure_entries_exist(b_tmp.first, b_second_map, b_tmp.second, ts);
  }

  // we then index, identify, and renumber all the immediately apparant
  // entries in each side.

  {
    std::map<file_path, tid> a_first_files, a_first_dirs;
    std::map<file_path, tid> b_first_files, b_first_dirs;
    index_entries(a_tmp.first, a_first_files, a_first_dirs);
    index_entries(b_tmp.first, b_first_files, b_first_dirs);    
    extend_renumbering_from_path_identities(a_first_files, b_first_files, renumbering);
    extend_renumbering_from_path_identities(a_first_dirs, b_first_dirs, renumbering);
  }

  // once renamed, b_tmp will have moved a fair bit closer to a_tmp, in
  // terms of tids. there is still one set of files we haven't accounted
  // for, however: files added in a and b.

  {
    state_renumbering aux_renumbering;
    extend_renumbering_via_added_files(a_tmp, b_tmp, aux_renumbering);
    for (state_renumbering::const_iterator i = aux_renumbering.begin(); 
	 i != aux_renumbering.end(); ++i)
      {
	I(renumbering.find(i->first) == renumbering.end());
	renumbering.insert(*i);
      }
  }

  // renumbering now contains a *complete* renumbering of b->a,
  // so we reset a_tmp and b_tmp, and renumber b_tmp under this
  // scheme. 

  a_tmp = a;
  b_tmp = b;
  apply_state_renumbering(renumbering, b_tmp);

  a_renumbered = a_tmp;
  b_renumbered = b_tmp;

  // now we're ready to merge (and resolve conflicts)
  path_state resolved_conflicts;
  project_missing_changes(a_tmp, b_tmp, b_merged, resolved_conflicts, app);
  project_missing_changes(b_tmp, a_tmp, a_merged, resolved_conflicts, app);

  {
    // now check: the merge analyses, when concatenated with their
    // predecessors, should lead to the same composite rearrangement

    tid_source ts_tmp;
    path_analysis anc_a_check, a_merge_check, a_check;
    path_analysis anc_b_check, b_merge_check, b_check;
    change_set::path_rearrangement a_re, b_re;

    rebuild_analysis(a, anc_a_check, ts_tmp);
    rebuild_analysis(b, anc_b_check, ts_tmp);
    rebuild_analysis(a_merged, a_merge_check, ts_tmp);
    rebuild_analysis(b_merged, b_merge_check, ts_tmp);

    concatenate_disjoint_analyses(anc_a_check, a_merge_check, a_check);
    concatenate_disjoint_analyses(anc_b_check, b_merge_check, b_check);
    compose_rearrangement(a_check, a_re);
    compose_rearrangement(b_check, b_re);
    I(a_re == b_re);
  }

}

static void
merge_deltas(file_path const & path_in_merged, 
	     std::map<file_path, file_id> & merge_finalists,
	     file_id const & anc,
	     file_id const & left,
	     file_id const & right,
	     file_id & finalist, 
	     merge_provider & merger)
{
  std::map<file_path, file_id>::const_iterator i = merge_finalists.find(path_in_merged);
  if (i != merge_finalists.end())
    {
      L(F("reusing merge resolution '%s' : '%s' -> '%s'\n")
	% path_in_merged % anc % i->second);
      finalist = i->second;
    }
  else
    {
      N(merger.try_to_merge_files(path_in_merged, anc, left, right, finalist),
	F("merge of '%s' : '%s' -> '%s' vs '%s' failed") 
	% path_in_merged % anc % left % right);

      L(F("merge of '%s' : '%s' -> '%s' vs '%s' resolved to '%s'\n") 
	% path_in_merged % anc % left % right % finalist);

      merge_finalists.insert(std::make_pair(path_in_merged, finalist));
    }
}

static void
project_missing_deltas(change_set const & a,
		       change_set const & b,
		       path_analysis const & a_analysis,		       
		       path_analysis const & b_analysis,		       
		       path_analysis const & a_merged_analysis,		       
		       change_set & b_merged,
		       merge_provider & merger,
		       std::map<file_path, file_id> & merge_finalists)
{
  directory_map a_second_map, b_first_map, a_merged_first_map;
  build_directory_map(a_analysis.second, a_second_map);
  build_directory_map(b_analysis.first, b_first_map);
  build_directory_map(a_merged_analysis.first, a_merged_first_map);

  for (change_set::delta_map::const_iterator i = a.deltas.begin(); 
       i != a.deltas.end(); ++i)
    {
      tid t;
      file_path path_in_merged, path_in_b_second;

      // first work out what the path in b.second is
      if (lookup_path(delta_entry_path(i), a_second_map, t) 
	  && b_analysis.second.find(t) != b_analysis.second.end())
	get_full_path(b_analysis.second, t, path_in_b_second);
      else
	reconstruct_path(delta_entry_path(i), b_first_map, 
			 b_analysis.second, path_in_b_second);

      // then work out what the path in merged is
      reconstruct_path(delta_entry_path(i), a_merged_first_map, 
		       a_merged_analysis.second, path_in_merged);

      // now check to see if there was a delta on the b.second name in b
      change_set::delta_map::const_iterator j = b.deltas.find(path_in_b_second);

      if (j == b.deltas.end())
	{
	  // if not, copy ours over using the merged name
	  L(F("merge is copying delta '%s' : '%s' -> '%s'\n") 
	    % path_in_merged % delta_entry_src(i) % delta_entry_dst(i));
	  I(b_merged.deltas.find(path_in_merged) == b_merged.deltas.end());
	  b_merged.apply_delta(path_in_merged, delta_entry_src(i), delta_entry_dst(i));
	}
      else 
	{
	  I(delta_entry_src(i) == delta_entry_src(j));
	  // if so, either... 

	  if (delta_entry_dst(i) == delta_entry_dst(j))
	    {
	      // ... absorb identical deltas
	      L(F("skipping common delta '%s' : '%s' -> '%s'\n") 
		% path_in_merged % delta_entry_src(i) % delta_entry_dst(i) % delta_entry_dst(j));
	    }

	  else if (delta_entry_src(i) == delta_entry_dst(i))
	    {
	      L(F("skipping neutral delta on '%s' : %s -> %s\n") 
		% delta_entry_path(i) 
		% delta_entry_src(i) 
		% delta_entry_dst(i));	      
	    }

	  else if (delta_entry_src(j) == delta_entry_dst(j))
	    {
	      L(F("propagating unperturbed delta on '%s' : '%s' -> '%s'\n") 
		% delta_entry_path(i) 
		% delta_entry_src(i) 
		% delta_entry_dst(i));	      
	      b_merged.apply_delta(path_in_merged, delta_entry_dst(j), delta_entry_dst(i));
	    }

	  else
	    {
	      // ... or resolve conflict
	      L(F("merging delta '%s' : '%s' -> '%s' vs. '%s'\n") 
		% path_in_merged % delta_entry_src(i) % delta_entry_dst(i) % delta_entry_dst(j));
	      file_id finalist;
	      merge_deltas(path_in_merged, 
			   merge_finalists,
			   delta_entry_src(i),
			   delta_entry_dst(i),
			   delta_entry_dst(j),
			   finalist, merger);
	      L(F("resolved merge to '%s' : '%s' -> '%s'\n")
		% path_in_merged % delta_entry_src(i) % finalist);

	      // if the conflict resolved to something other than the
	      // existing post-state of b, add a new entry to the deltas of
	      // b finishing the job.
	      if (! (finalist == delta_entry_dst(j)))
		b_merged.apply_delta(path_in_merged, delta_entry_dst(j), finalist);
	    }
	}
    }
}


void
merge_change_sets(change_set const & a,
		  change_set const & b,
		  change_set & a_merged,
		  change_set & b_merged,
		  merge_provider & merger,
		  app_state & app)
{
  L(F("concatenating change sets\n"));

  tid_source ts;
  path_analysis 
    a_analysis, b_analysis, 
    a_renumbered, b_renumbered, 
    a_merged_analysis, b_merged_analysis;

  analyze_rearrangement(a.rearrangement, a_analysis, ts);
  analyze_rearrangement(b.rearrangement, b_analysis, ts);

  merge_disjoint_analyses(a_analysis, b_analysis,
			  a_renumbered, b_renumbered,
			  a_merged_analysis, b_merged_analysis, 
			  app);

  compose_rearrangement(a_merged_analysis, 
			a_merged.rearrangement);

  compose_rearrangement(b_merged_analysis, 
			b_merged.rearrangement);

  std::map<file_path, file_id> merge_finalists;

  project_missing_deltas(a, b, 
			 a_renumbered, b_renumbered,
			 a_merged_analysis, 
			 b_merged,
			 merger, merge_finalists);

  project_missing_deltas(b, a, 
			 b_renumbered, a_renumbered,
			 b_merged_analysis, 
			 a_merged,
			 merger, merge_finalists);

  {
    // confirmation step
    change_set a_check, b_check;
    //     dump_change_set("a", a);
    //     dump_change_set("a_merged", a_merged);
    //     dump_change_set("b", b);
    //     dump_change_set("b_merged", b_merged);
    concatenate_change_sets(a, a_merged, a_check);
    concatenate_change_sets(b, b_merged, b_check);
    //     dump_change_set("a_check", a_check);
    //     dump_change_set("b_check", b_check);
    I(a_check == b_check);
  }

  L(F("finished merge\n"));  
}

// end stuff related to merging

void 
invert_change_set(change_set const & a2b,
		  manifest_map const & a_map,
		  change_set & b2a)
{
  tid_source ts;
  path_analysis a2b_analysis, b2a_analysis;

  analyze_rearrangement(a2b.rearrangement, a2b_analysis, ts);

  L(F("inverting change set\n"));
  b2a_analysis.first = a2b_analysis.second;
  b2a_analysis.second = a2b_analysis.first;
  compose_rearrangement(b2a_analysis, b2a.rearrangement);

  b2a.deltas.clear();

  // existing deltas are in "b space"
  for (path_state::const_iterator b = b2a_analysis.first.begin();
       b != b2a_analysis.first.end(); ++b)
    {
      path_state::const_iterator a = b2a_analysis.second.find(path_state_tid(b));
      I(a != b2a_analysis.second.end());
      if (path_item_type(path_state_item(b)) == ptype_file)
	{
	  file_path b_pth, a_pth;
	  get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);

	  if (null_name(path_item_name(path_state_item(b))) &&
	      ! null_name(path_item_name(path_state_item(a))))
	    {
	      // b->a represents an add in "a space"
	      get_full_path(b2a_analysis.second, path_state_tid(a), a_pth);
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
	      get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);
	      L(F("converted add %s to delete in inverse\n") % b_pth );
	    }
	  else
	    {
	      get_full_path(b2a_analysis.first, path_state_tid(b), b_pth);
	      get_full_path(b2a_analysis.second, path_state_tid(a), a_pth);
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

void 
move_files_to_tmp_bottom_up(tid t,
			    local_path const & temporary_root,
			    path_state const & state,
			    directory_map const & dmap)
{
  directory_map::const_iterator dirent = dmap.find(t);
  if (dirent != dmap.end())
    {
      boost::shared_ptr<directory_node> node = dirent->second;  
      for (directory_node::const_iterator entry = node->begin();
	   entry != node->end(); ++entry)
	{
	  tid child = directory_entry_tid(entry);
	  file_path path;
	  path_item item;
	      
	  find_item(child, state, item);

	  if (null_name(path_item_name(item)))
	    continue;

	  // recursively move all sub-entries
	  if (path_item_type(item) == ptype_directory)
	    move_files_to_tmp_bottom_up(child, temporary_root, state, dmap);

	  get_full_path(state, child, path);
	  
	  local_path src(path());
	  local_path dst((mkpath(temporary_root()) 
			  / mkpath(boost::lexical_cast<std::string>(child))).string());
	  
	  P(F("moving %s -> %s\n") % src % dst);
	  switch (path_item_type(item))
	    {
	    case ptype_file:
	      move_file(src, dst);
	      break;
	    case ptype_directory:
	      move_dir(src, dst);
	      break;
	    }
	}
    }
}

void 
move_files_from_tmp_top_down(tid t,
			     local_path const & temporary_root,
			     path_state const & state,
			     directory_map const & dmap)
{
  directory_map::const_iterator dirent = dmap.find(t);
  if (dirent != dmap.end())
    {
      boost::shared_ptr<directory_node> node = dirent->second;  
      for (directory_node::const_iterator entry = node->begin();
	   entry != node->end(); ++entry)
	{
	  tid child = directory_entry_tid(entry);
	  file_path path;
	  path_item item;
	      
	  find_item(child, state, item);

	  if (null_name(path_item_name(item)))
	    continue;

	  get_full_path(state, child, path);
	  
	  local_path src((mkpath(temporary_root()) 
			  / mkpath(boost::lexical_cast<std::string>(child))).string());
	  local_path dst(path());
	  
	  P(F("moving %s -> %s\n") % src % dst);
	  switch (path_item_type(item))
	    {
	    case ptype_file:
	      move_file(src, dst);
	      break;
	    case ptype_directory:
	      move_dir(src, dst);
	      break;
	    }

	  // recursively move all sub-entries
	  if (path_item_type(item) == ptype_directory)
	    move_files_from_tmp_top_down(child, temporary_root, state, dmap);
	}
    }
}


void
apply_rearrangement_to_filesystem(change_set::path_rearrangement const & re,
				  local_path const & temporary_root)
{
  tid_source ts;
  path_analysis analysis;
  directory_map first_dmap, second_dmap;

  analyze_rearrangement(re, analysis, ts);
  build_directory_map(analysis.first, first_dmap);
  build_directory_map(analysis.second, second_dmap);

  if (analysis.first.empty())
    return;

  move_files_to_tmp_bottom_up(root_tid, temporary_root,
			      analysis.first, first_dmap);

  move_files_from_tmp_top_down(root_tid, temporary_root,
			       analysis.second, second_dmap);
}

// application stuff


void
apply_path_rearrangement(path_set const & old_ps,
			 change_set::path_rearrangement const & pr,
			 path_set & new_ps)
{
  change_set a, b, c;
  a.rearrangement.added_files = old_ps;
  b.rearrangement = pr;
  concatenate_change_sets(a, b, c);
  new_ps = c.rearrangement.added_files;
}

void
build_pure_addition_change_set(manifest_map const & man,
			       change_set & cs)
{
  for (manifest_map::const_iterator i = man.begin(); i != man.end(); ++i)
    cs.add_file(manifest_entry_path(i), manifest_entry_id(i));
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
  change_set a, b, c;
  build_pure_addition_change_set(m_old, a);
  b.rearrangement = pr;
  concatenate_change_sets(a, b, c);

  m_old_rearranged.clear();
  for (std::set<file_path>::const_iterator i = c.rearrangement.added_files.begin();
       i != c.rearrangement.added_files.end(); ++i)
    {
      change_set::delta_map::const_iterator d = c.deltas.find(*i);
      if (d == c.deltas.end())
	// case 1: the file was added by the rearrangement, but we have
	// no idea what it was added as, so we leave its ID empty
	m_old_rearranged.insert(std::make_pair(*i, null_ident));
      else
	// case 2: we know the ID to insert
	m_old_rearranged.insert(std::make_pair(*i, delta_entry_dst(d)));	
    }
}




void
apply_change_set(manifest_map const & old_man,
		 change_set const & cs,
		 manifest_map & new_man)
{
  change_set a, b;
  build_pure_addition_change_set(old_man, a);
  concatenate_change_sets(a, cs, b);

  new_man.clear();
  for (std::set<file_path>::const_iterator i = b.rearrangement.added_files.begin();
       i != b.rearrangement.added_files.end(); ++i)
    {
      change_set::delta_map::const_iterator d = b.deltas.find(*i);
      I(d != b.deltas.end());
      new_man.insert(std::make_pair(*i, delta_entry_dst(d)));
    }
}

// quick, optimistic and destructive version for log walker
file_path
apply_change_set_inverse(change_set const & cs,
			 file_path const & file_in_second)
{
  tid_source ts;
  path_analysis analysis;
  directory_map second_dmap;
  file_path file_in_first;

  analyze_rearrangement(cs.rearrangement, analysis, ts);
  build_directory_map(analysis.second, second_dmap);
  reconstruct_path(file_in_second, second_dmap, analysis.first, file_in_first);
  return file_in_first;
}

// quick, optimistic and destructive version for rcs importer
void
apply_change_set(change_set const & cs,
		 manifest_map & man)
{
  if (cs.rearrangement.renamed_files.empty() 
      && cs.rearrangement.renamed_dirs.empty()
      && cs.rearrangement.deleted_dirs.empty())
    {
      // fast path for simple drop/add/delta file operations
      for (std::set<file_path>::const_iterator i = cs.rearrangement.deleted_files.begin();
	   i != cs.rearrangement.deleted_files.end(); ++i)
	{
	  man.erase(*i);
	}
      for (change_set::delta_map::const_iterator i = cs.deltas.begin(); 
	   i != cs.deltas.end(); ++i)
	{
	  if (!null_id(delta_entry_dst(i)))
	    man[delta_entry_path(i)] = delta_entry_dst(i);
	}
    }
  else
    {
      // fall back to the slow way
      manifest_map tmp;
      apply_change_set(man, cs, tmp);
      man = tmp;
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
parse_path_rearrangement(basic_io::parser & parser,
			 change_set & cs)
{
  while (parser.symp())
    {
      std::string t1, t2;
      if (parser.symp(syms::add_file)) 
	{ 
	  parser.key(syms::add_file);
	  parser.str(t1);
	  cs.add_file(file_path(t1));
	}
      else if (parser.symp(syms::delete_file)) 
	{ 
	  parser.key(syms::delete_file);
	  parser.str(t1);
	  cs.delete_file(file_path(t1));
	}
      else if (parser.symp(syms::delete_dir)) 
	{ 
	  parser.key(syms::delete_dir);
	  parser.str(t1);
	  cs.delete_dir(file_path(t1));
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
	  cs.rename_file(file_path(t1),
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
	  cs.rename_dir(file_path(t1),
			file_path(t2));
	}
      else
	{
	  std::string sym;
	  parser.sym(sym);
	  parser.err("unrecognized symbol: " + sym);
	}
    }
}


void 
print_path_rearrangement(basic_io::printer & printer,
			 change_set::path_rearrangement const & pr)
{

  for (std::set<file_path>::const_iterator i = pr.deleted_files.begin();
       i != pr.deleted_files.end(); ++i)
    {
      printer.print_key(syms::delete_file);
      printer.print_str((*i)());
    }

  for (std::set<file_path>::const_iterator i = pr.deleted_dirs.begin();
       i != pr.deleted_dirs.end(); ++i)
    {
      printer.print_key(syms::delete_dir);
      printer.print_str((*i)());
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_files.begin();
       i != pr.renamed_files.end(); ++i)
    {
      printer.print_key(syms::rename_file, true);
      {
	basic_io::scope sc(printer);
	printer.print_key(syms::src);
	printer.print_str(i->first());
	printer.print_key(syms::dst);      
	printer.print_str(i->second());
      }
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_dirs.begin();
       i != pr.renamed_dirs.end(); ++i)
    {
      printer.print_key(syms::rename_dir, true);
      {
	basic_io::scope sc(printer);
	printer.print_key(syms::src);
	printer.print_str(i->first());
	printer.print_key(syms::dst);      
	printer.print_str(i->second());
      }
    }

  for (std::set<file_path>::const_iterator i = pr.added_files.begin();
       i != pr.added_files.end(); ++i)
    {
      printer.print_key(syms::add_file);
      printer.print_str((*i)());
    }
}

void 
parse_change_set(basic_io::parser & parser,
		 change_set & cs)
{
  clear_change_set(cs);

  parser.key(syms::change_set);
  parser.bra();

  {
    parser.key(syms::paths);
    parser.bra();
    parse_path_rearrangement(parser, cs);    
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
  change_set cs;
  parse_path_rearrangement(pars, cs);
  re = cs.rearrangement;
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
  L(F("[begin changeset %s]\n", ctx));
  L(F("%s", tmp));
  L(F("[end changeset %s]\n", ctx));
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
disjoint_merge_test(std::string const & ab_str,
		    std::string const & ac_str)
{
  change_set ab, ac, bm, cm;

  app_state app;

  L(F("beginning disjoint_merge_test\n"));

  read_change_set(data(ab_str), ab);
  read_change_set(data(ac_str), ac);

  merge_provider merger(app);
  merge_change_sets(ab, ac, bm, cm, merger, app);

  dump_change_set("ab", ab);
  dump_change_set("ac", ac);
  dump_change_set("bm", bm);
  dump_change_set("cm", cm);

  BOOST_CHECK(bm.rearrangement == ac.rearrangement);
  BOOST_CHECK(cm.rearrangement == ab.rearrangement);

  L(F("finished disjoint_merge_test\n"));
}

static void
disjoint_merge_tests()
{
  disjoint_merge_test
    ("change_set: { paths: { rename_file: { src: \"foo\" dst: \"bar\" } } deltas: {} }",
     "change_set: { paths: { rename_file: { src: \"apple\" dst: \"orange\" } } deltas: {} }");  

  disjoint_merge_test
    ("change_set: { paths: { rename_file: { src: \"foo/a.txt\" dst: \"bar/b.txt\" } } deltas: {} }",
     "change_set: { paths: { rename_file: { src: \"bar/c.txt\" dst: \"baz/d.txt\" } } deltas: {} }");  

  disjoint_merge_test
    ("change_set: { paths: { } "
     "deltas: {"
     "delta: { path: \"foo/file.txt\" "
     "src: [c6a4a6196bb4a744207e1a6e90273369b8c2e925] "
     "dst: [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892] } } }",  
     "change_set: { paths: { rename_file: { src: \"foo/file.txt\" dst: \"foo/apple.txt\" } } deltas: {} }");  

  disjoint_merge_test
    (
     "change_set: { "
     "paths: { rename_file: { src: \"apple.txt\" dst: \"pear.txt\" } } "
     "deltas: { "
     "delta: { path: \"foo.txt\" "
     "src: [c6a4a6196bb4a744207e1a6e90273369b8c2e925] "
     "dst: [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892] } } }",
     
     "change_set: { "
     "paths: { rename_file: { src: \"foo.txt\" dst: \"bar.txt\" } } "
     "deltas: { "
     "delta: { path: \"apple.txt\" "
     "src: [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892] "
     "dst: [435e816c30263c9184f94e7c4d5aec78ea7c028a] } } }"
     );  
}

static void 
basic_change_set_test()
{
  try
    {
      
      change_set cs;
      cs.delete_file(file_path("usr/lib/zombie"));
      cs.add_file(file_path("usr/bin/cat"));
      cs.add_file(file_path("usr/local/bin/dog"));
      cs.rename_file(file_path("usr/local/bin/dog"), file_path("usr/bin/dog"));
      cs.rename_file(file_path("usr/bin/cat"), file_path("usr/local/bin/chicken"));
      cs.add_file(file_path("usr/lib/libc.so"));
      cs.rename_dir(file_path("usr/lib"), file_path("usr/local/lib"));
      cs.apply_delta(file_path("usr/local/lib/libc.so"), 
		     null_ident,
		     file_id(hexenc<id>("435e816c30263c9184f94e7c4d5aec78ea7c028a")));
      cs.apply_delta(file_path("usr/local/bin/chicken"), 
		     file_id(hexenc<id>("c6a4a6196bb4a744207e1a6e90273369b8c2e925")),
		     file_id(hexenc<id>("fe18ec0c55cbc72e4e51c58dc13af515a2f3a892")));
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
      cs1.add_file(file_path("usr/lib/zombie"));
      cs1.rename_file(file_path("usr/lib/apple"),
		      file_path("usr/lib/orange"));
      cs1.rename_dir(file_path("usr/lib/moose"),
		     file_path("usr/lib/squirrel"));

      dump_change_set("neutralize target", cs1);

      cs2.delete_file(file_path("usr/lib/zombie"));
      cs2.rename_file(file_path("usr/lib/orange"),
		      file_path("usr/lib/apple"));
      cs2.rename_dir(file_path("usr/lib/squirrel"),
		     file_path("usr/lib/moose"));

      dump_change_set("neutralizer", cs2);
      
      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("neutralized", csa);

      tid_source ts;
      path_analysis analysis;
      analyze_rearrangement(csa.rearrangement, analysis, ts);

      BOOST_CHECK(analysis.first.empty());
      BOOST_CHECK(analysis.second.empty());
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
      cs1.delete_file(file_path("usr/lib/zombie"));
      cs1.rename_file(file_path("usr/lib/orange"),
		      file_path("usr/lib/apple"));
      cs1.rename_dir(file_path("usr/lib/squirrel"),
		     file_path("usr/lib/moose"));

      dump_change_set("non-interference A", cs1);

      cs2.add_file(file_path("usr/lib/zombie"));
      cs2.rename_file(file_path("usr/lib/pear"),
		      file_path("usr/lib/orange"));
      cs2.rename_dir(file_path("usr/lib/spy"),
		     file_path("usr/lib/squirrel"));
      
      dump_change_set("non-interference B", cs2);

      concatenate_change_sets(cs1, cs2, csa);

      dump_change_set("non-interference combined", csa);

      tid_source ts;
      path_analysis analysis;
      analyze_rearrangement(csa.rearrangement, analysis, ts);

      BOOST_CHECK(analysis.first.size() == 8);
      BOOST_CHECK(analysis.second.size() == 8);
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
  suite->add(BOOST_TEST_CASE(&disjoint_merge_tests));
}


#endif // BUILD_UNIT_TESTS
