// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this is how you "ask for" the C99 constant constructor macros.  *and*
// you have to do so before any other files accidentally include
// stdint.h. awesome.
#define __STDC_CONSTANT_MACROS

#include <algorithm>
#include <iterator>
#include <iostream>
#include <list>
#include <vector>
#include <ext/hash_map>

#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "constants.hh"
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

typedef unsigned long path_component;

struct
path_component_maker
{
  path_component make(std::string const & s)
  {
    bool is_new;
    path_component pc = intern.intern(s, is_new);
    // sanity check new ones
    if (is_new)
    {
      // must be a valid file_path
      file_path tmp_file_path = file_path(s);
      // must contain exactly 0 or 1 components
      fs::path tmp_fs_path = mkpath(s);
      I(null_name(s) || ++(tmp_fs_path.begin()) == tmp_fs_path.end());
    }
    return pc;
  }
  std::string lookup(path_component pc) const
  {
    return intern.lookup(pc);
  }
private:
  interner<path_component> intern;
};

static path_component_maker the_path_component_maker;

static path_component
make_null_component()
{
  static path_component null_pc = the_path_component_maker.make("");
  return null_pc;
}

static bool
null_name(path_component pc)
{
  return pc == make_null_component();
}

struct
path_item
{
  tid parent;
  ptype ty;
  path_component name;      
  path_item() {}
  path_item(tid p, ptype t, path_component n);
  path_item(path_item const & other);
  path_item const & operator=(path_item const & other);
  bool operator==(path_item const & other) const;
};


template<typename T> struct identity 
{
  size_t operator()(T const & v) const 
  { 
    return static_cast<size_t>(v);
  }
};

typedef __gnu_cxx::hash_map<tid, path_item, identity<tid> > path_state;
typedef __gnu_cxx::hash_map<tid, tid, identity<tid> > state_renumbering;
typedef std::pair<path_state, path_state> path_analysis;

// nulls and tests

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

typedef __gnu_cxx::hash_map<path_component, 
                            std::pair<ptype,tid>,
                            identity<path_component> > directory_node;

typedef __gnu_cxx::hash_map<tid, boost::shared_ptr<directory_node>,
                            identity<tid> > directory_map;

static path_component
directory_entry_name(directory_node::const_iterator const & i)
{
  return i->first;
}

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
  I(rearrangement.added_files.find(a) == rearrangement.added_files.end());
  rearrangement.added_files.insert(a);
}

void 
change_set::add_file(file_path const & a, file_id const & ident)
{
  I(rearrangement.added_files.find(a) == rearrangement.added_files.end());
  I(deltas.find(a) == deltas.end());
  rearrangement.added_files.insert(a);
  deltas.insert(std::make_pair(a, std::make_pair(null_ident, ident)));
}

void 
change_set::apply_delta(file_path const & path, 
                        file_id const & src, 
                        file_id const & dst)
{
  I(deltas.find(path) == deltas.end());
  deltas.insert(std::make_pair(path, std::make_pair(src, dst)));
}

void 
change_set::delete_file(file_path const & d)
{
  I(rearrangement.deleted_files.find(d) == rearrangement.deleted_files.end());
  rearrangement.deleted_files.insert(d);
}

void 
change_set::delete_dir(file_path const & d)
{
  I(rearrangement.deleted_dirs.find(d) == rearrangement.deleted_dirs.end());
  rearrangement.deleted_dirs.insert(d);
}

void 
change_set::rename_file(file_path const & a, file_path const & b)
{
  I(rearrangement.renamed_files.find(a) == rearrangement.renamed_files.end());
  rearrangement.renamed_files.insert(std::make_pair(a,b));
}

void 
change_set::rename_dir(file_path const & a, file_path const & b)
{
  I(rearrangement.renamed_dirs.find(a) == rearrangement.renamed_dirs.end());
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
change_set::path_rearrangement::has_added_file(file_path const & file) const
{
  return added_files.find(file) != added_files.end();
}

bool
change_set::path_rearrangement::has_deleted_file(file_path const & file) const
{
  return deleted_files.find(file) != deleted_files.end();
}

bool
change_set::path_rearrangement::has_renamed_file_dst(file_path const & file) const
{
  // FIXME: this is inefficient, but improvements would require a different
  // structure for renamed_files (or perhaps a second reverse map). For now
  // we'll assume that few files will be renamed per changeset.
  for (std::map<file_path,file_path>::const_iterator rf = renamed_files.begin();
       rf != renamed_files.end(); ++rf)
    if (rf->second == file)
      return true;
  return false;
}

bool
change_set::path_rearrangement::has_renamed_file_src(file_path const & file) const
{
  return renamed_files.find(file) != renamed_files.end();
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

inline path_component
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
        % the_path_component_maker.lookup(path_item_name(path_state_item(i))));
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
check_sets_disjoint(std::set<file_path> const & a,
                    std::set<file_path> const & b)
{
  std::set<file_path> isect;
  std::set_intersection(a.begin(), a.end(),
                        b.begin(), b.end(),
                        std::inserter(isect, isect.begin()));
  if (!global_sanity.relaxed)
    {
      I(isect.empty());
    }
}

change_set::path_rearrangement::path_rearrangement(path_rearrangement const & other)
{
  other.check_sane();
  deleted_files = other.deleted_files;
  deleted_dirs = other.deleted_dirs;
  renamed_files = other.renamed_files;
  renamed_dirs = other.renamed_dirs;
  added_files = other.added_files;
}

change_set::path_rearrangement const &
change_set::path_rearrangement::operator=(path_rearrangement const & other)
{
  other.check_sane();
  deleted_files = other.deleted_files;
  deleted_dirs = other.deleted_dirs;
  renamed_files = other.renamed_files;
  renamed_dirs = other.renamed_dirs;
  added_files = other.added_files;
  return *this;
}

static void
extract_pairs_and_insert(std::map<file_path, file_path> const & in,
                         std::set<file_path> & firsts,
                         std::set<file_path> & seconds)
{
  for (std::map<file_path, file_path>::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      firsts.insert(i->first);
      seconds.insert(i->second);
    }
}

template <typename A, typename B>
static void
extract_first(std::map<A, B> const & m, std::set<A> & s)
{
  s.clear();
  for (typename std::map<A, B>::const_iterator i = m.begin();
       i != m.end(); ++i)
    {
      s.insert(i->first);
    }
}

static void
extract_killed(path_analysis const & a,
               std::set<file_path> & killed);


static void
check_no_deltas_on_killed_files(path_analysis const & pa,
                                change_set::delta_map const & del)
{
  std::set<file_path> killed;
  std::set<file_path> delta_paths;

  extract_killed(pa, killed);
  extract_first(del, delta_paths);
  check_sets_disjoint(killed, delta_paths);
}

static void
check_delta_entries_not_directories(path_analysis const & pa,
                                    change_set::delta_map const & dels);

void 
analyze_rearrangement(change_set::path_rearrangement const & pr,
                      path_analysis & pa,
                      tid_source & ts);

void
sanity_check_path_analysis(path_analysis const & pr);

void 
change_set::path_rearrangement::check_sane() const
{
  delta_map del;
  this->check_sane(del);
}

void 
change_set::path_rearrangement::check_sane(delta_map const & deltas) const
{
  tid_source ts;
  path_analysis pa;
  analyze_rearrangement(*this, pa, ts);
  sanity_check_path_analysis (pa);

  check_no_deltas_on_killed_files(pa, deltas);
  check_delta_entries_not_directories(pa, deltas);

  // FIXME: extend this as you manage to think of more invariants
  // which are cheap enough to check at this level.
  std::set<file_path> renamed_srcs, renamed_dsts;
  extract_pairs_and_insert(renamed_files, renamed_srcs, renamed_dsts);
  extract_pairs_and_insert(renamed_dirs, renamed_srcs, renamed_dsts);

  // Files cannot be split nor joined by renames.
  I(renamed_files.size() + renamed_dirs.size() == renamed_srcs.size());
  I(renamed_files.size() + renamed_dirs.size() == renamed_dsts.size());

  check_sets_disjoint(deleted_files, deleted_dirs);
  check_sets_disjoint(deleted_files, renamed_srcs);
  check_sets_disjoint(deleted_dirs, renamed_srcs);

  check_sets_disjoint(added_files, renamed_dsts);
}

change_set::change_set(change_set const & other)
{
  other.check_sane();
  rearrangement = other.rearrangement;
  deltas = other.deltas;
}

change_set const &change_set::operator=(change_set const & other)
{
  other.check_sane();
  rearrangement = other.rearrangement;
  deltas = other.deltas;
  return *this;
}

void 
change_set::check_sane() const
{
  // FIXME: extend this as you manage to think of more invariants
  // which are cheap enough to check at this level.

  rearrangement.check_sane(this->deltas);

  for (std::set<file_path>::const_iterator i = rearrangement.added_files.begin(); 
       i != rearrangement.added_files.end(); ++i)
    {
      delta_map::const_iterator j = deltas.find(*i);
      if (!global_sanity.relaxed)
        {
          I(j != deltas.end());
          I(null_id(delta_entry_src(j)));
          I(!null_id(delta_entry_dst(j)));
        }
    }

  for (delta_map::const_iterator i = deltas.begin(); 
       i != deltas.end(); ++i)
    {
      if (!global_sanity.relaxed)
        {
          I(!null_name(delta_entry_path(i)));
          I(!null_id(delta_entry_dst(i)));
          I(!(delta_entry_src(i) == delta_entry_dst(i)));
          if (null_id(delta_entry_src(i)))
            I(rearrangement.added_files.find(delta_entry_path(i))
              != rearrangement.added_files.end());
        }
    }

}

static void
sanity_check_path_item(path_item const & pi)
{
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

              // if we're null, our parent must also be null
              if (null_name(item.name))
                I(null_name(path_state_item(j).name));

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
  std::set< std::pair<tid,path_component> > entries;
  for (path_state::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      if (null_name(path_item_name(i->second)))
        {
          I(path_item_parent(i->second) == root_tid);
          continue;
        }
          
      std::pair<tid,path_component> p = std::make_pair(path_item_parent(i->second), 
                                                       path_item_name(i->second));
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

path_item::path_item(tid p, ptype t, path_component n) 
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
      //           &&
      //           null_name(path_item_name(j->second))));
    }
}

void
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


// This function takes a vector of path components and joins them into a
// single file_path.  Valid input may be a single-element vector whose sole
// element is the empty path component (""); this represents the null path,
// which we use to represent non-existent files.  Alternatively, input may be
// a multi-element vector, in which case all elements of the vector are
// required to be non-null.  The following are valid inputs (with strings
// replaced by their interned version, of course):
//    - [""]
//    - ["foo"]
//    - ["foo", "bar"]
// The following are not:
//    - []
//    - ["foo", ""]
//    - ["", "bar"]
static void 
compose_path(std::vector<path_component> const & names,
             file_path & path)
{
  try
    {      
      std::vector<path_component>::const_iterator i = names.begin();
      I(i != names.end());
      fs::path p = mkpath(the_path_component_maker.lookup(*i));
      ++i;
      if (names.size() > 1)
          I(!null_name(*i));
      for ( ; i != names.end(); ++i)
        {
          I(!null_name(*i));
          p /= mkpath(the_path_component_maker.lookup(*i));
        }
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
              std::vector<path_component> & pth)
{
  std::vector<path_component> tmp;
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
  std::vector<path_component> tmp;
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
      std::vector<path_component> old_name, new_name;
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
           std::vector<path_component> & components)
{
  components.clear();
  fs::path tmp = mkpath(p());
  for (fs::path::iterator i = tmp.begin(); i != tmp.end(); ++i)
    components.push_back(the_path_component_maker.make(*i));
}

static void 
split_path(file_path const & p,
           std::vector<path_component> & prefix,
           path_component & leaf_path)
{
  split_path(p, prefix);
  I(prefix.size() > 0);
  leaf_path = prefix.back();
  prefix.pop_back();
}

static bool
lookup_path(std::vector<path_component> const & pth,
            directory_map const & dir,
            tid & t)
{
  t = root_tid;
  for (std::vector<path_component>::const_iterator i = pth.begin();
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
  std::vector<path_component> vec;
  split_path(pth, vec);
  return lookup_path(vec, dir, t);
}

static tid
ensure_entry(directory_map & dmap,
             path_state & state,             
             tid dir_tid,
             ptype entry_ty,
             path_component entry,
             tid_source & ts)
{
  I(! null_name(entry));

  if (dir_tid != root_tid)
    {
      path_state::const_iterator parent = state.find(dir_tid);      
      I( parent != state.end());

      // if our parent is null, we immediately become null too, and attach to
      // the root node (where all null entries reside)
      if (null_name(path_item_name(path_state_item(parent))))
        {
          tid new_tid = ts.next();
          state.insert(std::make_pair(new_tid, path_item(root_tid, entry_ty, make_null_component())));
          return new_tid;
        }        
    }

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
ensure_dir_in_map (std::vector<path_component> pth,
                   directory_map & dmap,
                   path_state & state,
                   tid_source & ts)
{
  tid dir_tid = root_tid;
  for (std::vector<path_component>::const_iterator p = pth.begin();
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
  std::vector<path_component> components;
  split_path(path, components);
  return ensure_dir_in_map(components, dmap, state, ts);
}

static tid
ensure_file_in_map (file_path const & path,
                    directory_map & dmap,
                    path_state & state,
                    tid_source & ts)
{
  std::vector<path_component> prefix;  
  path_component leaf_path;
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
  std::vector<path_component> vec;
  std::vector<path_component> rebuilt;

  // L(F("reconstructing path '%s' under analysis\n") % input);
  
  split_path(input, vec);

  tid t = root_tid;
  std::vector<path_component>::const_iterator pth = vec.begin();
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
      path_component name = path_item_name(item);
      ptype type = path_item_type(item);            
      //       L(F("adding entry %s (%s %d) to directory node %d\n") 
      //        % name % (type == ptype_directory ? "dir" : "file") % curr % parent);
      dnode(dir, parent)->insert(std::make_pair(name,std::make_pair(type, curr)));

      // also, make sure to add current node if it's a directory, even if
      // there are no entries in it
      if (type == ptype_directory)
        dnode(dir, curr);        
    }
}


void 
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
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_file, make_null_component())));
      damaged_in_first.insert(x);
    }

  for (std::set<file_path>::const_iterator d = pr.deleted_dirs.begin();
       d != pr.deleted_dirs.end(); ++d)
    {
      tid x = ensure_dir_in_map(*d, first_map, pa.first, ts);
      pa.second.insert(std::make_pair(x, path_item(root_tid, ptype_directory, make_null_component())));
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
      pa.first.insert(std::make_pair(x, path_item(root_tid, ptype_file, make_null_component())));
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
normalize_path_rearrangement(change_set::path_rearrangement & norm)
{
  path_analysis tmp;
  tid_source ts;

  analyze_rearrangement(norm, tmp, ts);
  clear_rearrangement(norm);
  compose_rearrangement(tmp, norm);
}

void
normalize_change_set(change_set & norm)
{  
  normalize_path_rearrangement(norm.rearrangement);
  change_set::delta_map tmp = norm.deltas;
  for (change_set::delta_map::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      if (delta_entry_src(i) == delta_entry_dst(i))
        norm.deltas.erase(delta_entry_path(i));
    }
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
          dirs.insert(std::make_pair(full, path_state_tid(i)));
          break;

        case ptype_file:
          files.insert(std::make_pair(full, path_state_tid(i)));
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
      I(renumbering.find(i->second) == renumbering.end());
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
extract_killed(path_analysis const & a,
               std::set<file_path> & killed)

{
  killed.clear();
  directory_map first_map, second_map;

  build_directory_map(a.first, first_map);
  build_directory_map(a.second, second_map);

  for (directory_map::const_iterator i = first_map.begin();
       i != first_map.end(); ++i)
    {
      tid dir_tid = i->first;
      directory_map::const_iterator j = second_map.find(dir_tid);
      I(j != second_map.end());

      // a path P = DIR/LEAF is "killed" by a path_analysis iff the
      // directory node named DIR in the post-state contains LEAF in the
      // pre-state, and does not contain LEAF in the post-state

      boost::shared_ptr<directory_node> first_node = i->second;
      boost::shared_ptr<directory_node> second_node = j->second;

      for (directory_node::const_iterator p = first_node->begin();
           p != first_node->end(); ++p)
        {
          path_component first_name = directory_entry_name(p);
          directory_node::const_iterator q = second_node->find(first_name);
          if (q == second_node->end())
            {
              // found a killed entry
              std::vector<path_component> killed_name;
              file_path killed_path;
              get_full_path(a.second, dir_tid, killed_name);
              killed_name.push_back(first_name);
              compose_path(killed_name, killed_path);
              killed.insert(killed_path);
            }
        }
    }
}

static void
check_delta_entries_not_directories(path_analysis const & pa,
                                    change_set::delta_map const & dels)
{
  directory_map dmap;
  build_directory_map(pa.second, dmap);
  for (change_set::delta_map::const_iterator i = dels.begin();
       i != dels.end(); ++i)
    {
      tid delta_tid;
      if (lookup_path(delta_entry_path(i), dmap, delta_tid))
        {
          path_state::const_iterator j = pa.second.find(delta_tid);
          I(j != pa.second.end());
          I(path_item_type(path_state_item(j)) == ptype_file);
        }
    }
}

static void
concatenate_disjoint_analyses(path_analysis const & a,
                              path_analysis const & b,
                              std::set<file_path> const & a_killed,
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

  {
    std::set<file_path> 
      a_second_file_set, a_second_dir_set, 
      b_first_file_set, b_first_dir_set;
    
    extract_first(a_second_files, a_second_file_set);
    extract_first(a_second_dirs, a_second_dir_set);
    extract_first(b_first_files, b_first_file_set);
    extract_first(b_first_dirs, b_first_dir_set);
    
    // check that there are no entry-type mismatches
    check_sets_disjoint(a_second_file_set, b_first_dir_set);
    check_sets_disjoint(a_second_dir_set, b_first_file_set);

    // check that there's no use of killed entries
    check_sets_disjoint(a_killed, b_first_dir_set);
    check_sets_disjoint(a_killed, b_first_file_set);
  }

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
concatenate_rearrangements(change_set::path_rearrangement const & a,
                           change_set::path_rearrangement const & b,
                           change_set::path_rearrangement & concatenated)
{
  a.check_sane();
  b.check_sane();
  concatenated = change_set::path_rearrangement();
  
  tid_source ts;
  path_analysis a_analysis, b_analysis, concatenated_analysis;

  analyze_rearrangement(a, a_analysis, ts);
  analyze_rearrangement(b, b_analysis, ts);

  std::set<file_path> a_killed;
  extract_killed(a_analysis, a_killed);

  concatenate_disjoint_analyses(a_analysis, 
                                b_analysis,
                                a_killed,
                                concatenated_analysis);

  compose_rearrangement(concatenated_analysis, 
                        concatenated);

  concatenated.check_sane();
}

void
concatenate_change_sets(change_set const & a,
                        change_set const & b,
                        change_set & concatenated)
{
  a.check_sane();
  b.check_sane();

  L(F("concatenating change sets\n"));

  tid_source ts;
  path_analysis a_analysis, b_analysis, concatenated_analysis;

  analyze_rearrangement(a.rearrangement, a_analysis, ts);
  analyze_rearrangement(b.rearrangement, b_analysis, ts);

  std::set<file_path> a_killed;
  extract_killed(a_analysis, a_killed);

  concatenate_disjoint_analyses(a_analysis, 
                                b_analysis,
                                a_killed,
                                concatenated_analysis);

  compose_rearrangement(concatenated_analysis, 
                        concatenated.rearrangement);

  // now process the deltas

  concatenated.deltas.clear();
  directory_map a_dst_map, b_src_map;
  L(F("concatenating %d and %d deltas\n")
    % a.deltas.size() % b.deltas.size());
  build_directory_map(a_analysis.second, a_dst_map);
  build_directory_map(b_analysis.first, b_src_map);

  // first rename a's deltas under the rearrangement of b
  for (change_set::delta_map::const_iterator del = a.deltas.begin();
       del != a.deltas.end(); ++del)
    {
      file_path new_pth;
      L(F("processing delta on %s\n") % delta_entry_path(del));

      // work out the name of entry in b.first
      reconstruct_path(delta_entry_path(del), b_src_map, b_analysis.second, new_pth);
      L(F("delta on %s in first changeset renamed to %s\n")
        % delta_entry_path(del) % new_pth);

      if (b.rearrangement.has_deleted_file(delta_entry_path(del)))
        // the delta should be removed if the file is going to be deleted
        L(F("discarding delta [%s]->[%s] for deleted file '%s'\n")
          % delta_entry_src(del) % delta_entry_dst(del) % delta_entry_path(del));
      else
        concatenated.deltas.insert(std::make_pair(new_pth,
                                                  std::make_pair(delta_entry_src(del),
                                                                 delta_entry_dst(del))));
    }

  // next fuse any deltas id1->id2 and id2->id3 to id1->id3
  for (change_set::delta_map::const_iterator del = b.deltas.begin();
       del != b.deltas.end(); ++del)
    {

      file_path del_pth = delta_entry_path(del);
      change_set::delta_map::const_iterator existing = 
        concatenated.deltas.find(del_pth);
      if (existing != concatenated.deltas.end())
        {
          L(F("fusing deltas on %s : %s -> %s and %s -> %s\n")
            % del_pth
            % delta_entry_src(existing) 
            % delta_entry_dst(existing)
            % delta_entry_src(del)
            % delta_entry_dst(del));
          I(delta_entry_dst(existing) == delta_entry_src(del));
          std::pair<file_id, file_id> fused = std::make_pair(delta_entry_src(existing),
                                                             delta_entry_dst(del));      
          concatenated.deltas.erase(del_pth);
          concatenated.deltas.insert(std::make_pair((del_pth), fused));
        }
      else
        {
          L(F("delta on %s in second changeset copied forward\n") % del_pth);
          // in general don't want deltas on deleted files. however if a
          // file has been deleted then re-added, then a delta is valid
          // (it applies to the newly-added file)
          if (!b.rearrangement.has_deleted_file(del_pth)
              || b.rearrangement.has_added_file(del_pth)
              || b.rearrangement.has_renamed_file_dst(del_pth))
            concatenated.deltas.insert(*del);
        }
    }
  
  normalize_change_set(concatenated);
  concatenated.check_sane();
  
  L(F("finished concatenation\n")); 
}

// end stuff related to concatenation


// begin stuff related to merging


static void
extend_renumbering_via_added_files(path_analysis const & a, 
                                   path_analysis const & b, 
                                   state_renumbering & existing_renumbering,
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
          path_component leaf_name = path_item_name(path_state_item(j));

          I(path_item_type(path_state_item(j)) == ptype_file);
          if (! null_name(leaf_name))
            {
              tid added_parent_tid = path_item_parent(path_state_item(j));
              state_renumbering::const_iterator ren = existing_renumbering.find(added_parent_tid);
              if (ren != existing_renumbering.end())
                added_parent_tid = ren->second;
              directory_map::const_iterator dirent = a_second_map.find(added_parent_tid);
              if (dirent != a_second_map.end())
                {
                  boost::shared_ptr<directory_node> node = dirent->second;
                  directory_node::const_iterator entry = node->find(leaf_name);
                  if (entry != node->end() && directory_entry_type(entry) == ptype_file)
                    {
                      I(renumbering.find(path_state_tid(i)) == renumbering.end());
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

  path_item a_item, b_item;
  find_item(t, a_tmp.second, a_item);
  find_item(t, b_tmp.second, b_item);

  file_path anc, a, b, res;
  get_full_path(a_tmp.first, t, anc);
  get_full_path(a_tmp.second, t, a);
  get_full_path(b_tmp.second, t, b);
  
  if (i != resolved_conflicts.end())
    {
      resolved = path_state_item(i);
    }
  else if (null_name(path_item_name(a_item)) &&
           ! null_name(path_item_name(b_item)))
    {
      L(F("delete of %s dominates rename to %s\n") % anc % b);
      resolved = a_item;
      resolved_conflicts.insert(std::make_pair(t, resolved));
    }
  else if (null_name(path_item_name(b_item)) &&
           ! null_name(path_item_name(a_item)))
    {
      L(F("delete of %s dominates rename to %s\n") % anc % a);
      resolved = b_item;
      resolved_conflicts.insert(std::make_pair(t, resolved));
    }
  else
    {
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
                        tid_source & ts,
                        app_state & app)
{
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
    build_directory_map(b_tmp.second, b_second_map);
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
    extend_renumbering_via_added_files(a_tmp, b_tmp, renumbering, aux_renumbering);
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

    std::set<file_path> anc_a_killed, anc_b_killed;
    extract_killed(anc_a_check, anc_a_killed);
    extract_killed(anc_b_check, anc_b_killed);

    concatenate_disjoint_analyses(anc_a_check, a_merge_check, anc_a_killed, a_check);
    concatenate_disjoint_analyses(anc_b_check, b_merge_check, anc_b_killed, b_check);
    compose_rearrangement(a_check, a_re);
    compose_rearrangement(b_check, b_re);
    I(a_re == b_re);
  }

}

static void
merge_deltas(file_path const & anc_path, 
             file_path const & left_path, 
             file_path const & right_path, 
             file_path const & path_in_merged, 
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
      if (null_id(anc))
        {
          N(merger.try_to_merge_files(left_path, right_path, path_in_merged, left, right, finalist),
            F("merge of '%s' : '%s' vs. '%s' (no common ancestor) failed")
            % path_in_merged % left % right);
        }
      else
        {
          N(merger.try_to_merge_files(anc_path, left_path, right_path, path_in_merged, 
                                      anc, left, right, finalist),
            F("merge of '%s' : '%s' -> '%s' vs '%s' failed") 
            % path_in_merged % anc % left % right);
        }

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
      file_path path_in_merged, path_in_anc, path_in_b_second;

      // we have a fork like this:
      //
      //
      //            +--> [a2]
      //     [a1==b1]
      //            +--> [b2]
      //
      // and we have a delta applied to a file in a2. we want to
      // figure out what to call this delta's path in b2. this means
      // reconstructing it in a1==b1, then reconstructing it *again*
      // in b2.

      // first work out what the path in a.first == b.first is
      reconstruct_path(delta_entry_path(i), a_second_map, 
                       a_analysis.first, path_in_anc);

      // first work out what the path in b.second is
      reconstruct_path(path_in_anc, b_first_map, 
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
          I(b.deltas.find(path_in_merged) == b.deltas.end());
          if (b.rearrangement.has_deleted_file(path_in_merged))
            // if the file was deleted on the other fork of the merge, then
            // we don't want to keep this delta.
            L(F("skipping delta '%s'->'%s' on deleted file '%s'\n")
                % delta_entry_src(i) % delta_entry_dst(i) % path_in_merged);
          else
            b_merged.apply_delta(path_in_merged, delta_entry_src(i), delta_entry_dst(i));
        }
      else 
        {
          // if so, either... 

          if (!(delta_entry_src(i) == delta_entry_src(j)))
            {
              // This is a bit of a corner case where a file was added then deleted on one
              // of the forks. The src for the addition fork will be null_id, but the src
              // for the other fork will be the ancestor file's id.

              // if neither of the forks involved a file addition delta (null_id to something)
              // then something bad happened.
              I(null_id(delta_entry_src(i)) || null_id(delta_entry_src(j)));

              if (null_id(delta_entry_src(i)))
                {
                  // ... use the delta from 'a'
                  // 'a' change_set included a delta []->[...], ie file added. We want to
                  // follow this fork so it gets added to the b_merged changeset
                  L(F("propagating new file addition delta on '%s' : '%s' -> '%s'\n")
                    % path_in_merged
                    % delta_entry_src(j) 
                    % delta_entry_dst(i));        
                  b_merged.apply_delta(path_in_merged, delta_entry_src(i), delta_entry_dst(i));
                }
              else if (null_id(delta_entry_src(j)))
                {
                  // ... ignore the delta
                  // 'b' change_set included a delta []->[...], ie file added. We don't need
                  // to add it to the b_merged changeset, since any delta in 'a' will be
                  // ignored (as 'b' includes deletions).
                  L(F("skipping new file addition delta on '%s' : '' -> '%s'\n")
                    % path_in_merged
                    % delta_entry_dst(j));        
                }
            }
          else if (delta_entry_dst(i) == delta_entry_dst(j))
            {
              // ... absorb identical deltas
              L(F("skipping common delta '%s' : '%s' -> '%s'\n") 
                % path_in_merged % delta_entry_src(i) % delta_entry_dst(i));
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

              merge_deltas(path_in_anc,
                           delta_entry_path(i), // left_path
                           delta_entry_path(j), // right_path
                           path_in_merged, 
                           merge_finalists,
                           delta_entry_src(i), // anc
                           delta_entry_dst(i), // left
                           delta_entry_dst(j), // right
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
  a.check_sane();
  b.check_sane();

  L(F("merging change sets\n"));

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
                          ts, app);

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

  normalize_change_set(a_merged);
  normalize_change_set(b_merged);

  a_merged.check_sane();
  b_merged.check_sane();

  L(F("finished merge\n"));  
}

// end stuff related to merging

void 
invert_change_set(change_set const & a2b,
                  manifest_map const & a_map,
                  change_set & b2a)
{
  a2b.check_sane();
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
      // check to make sure this isn't the image of an add (now a delete)
      if (null_id(delta_entry_src(del)))
        continue;
      // check to make sure this isn't one of the already-moved deltas
      if (b2a.deltas.find(delta_entry_path(del)) != b2a.deltas.end())
        continue;
      b2a.deltas.insert(std::make_pair(delta_entry_path(del),
                                       std::make_pair(delta_entry_dst(del),
                                                      delta_entry_src(del))));
    }
  normalize_change_set(b2a);
  b2a.check_sane();
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
              if (file_exists(src))
                move_file(src, dst);
              break;
            case ptype_directory:
              if (directory_exists(src))
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
          
          switch (path_item_type(item))
            {
            case ptype_file:
              if (file_exists(src))
                {
                  P(F("moving file %s -> %s\n") % src % dst);
                  make_dir_for(path);
                  move_file(src, dst);
                }
              break;
            case ptype_directory:
              if (directory_exists(src))
                {
                  P(F("moving dir %s -> %s\n") % src % dst);
                  make_dir_for(path);
                  move_dir(src, dst);
                }
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
  re.check_sane();
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
  pr.check_sane();
  change_set::path_rearrangement a, b, c;
  a.added_files = old_ps;
  concatenate_rearrangements(a, pr, c);
  new_ps = c.added_files;
}

void
build_pure_addition_change_set(manifest_map const & man,
                               change_set & cs)
{
  for (manifest_map::const_iterator i = man.begin(); i != man.end(); ++i)
    cs.add_file(manifest_entry_path(i), manifest_entry_id(i));
  cs.check_sane();
}

// this function takes the rearrangement sitting in cs and "completes" the
// changeset by filling in all the deltas

void 
complete_change_set(manifest_map const & m_old,
                    manifest_map const & m_new,
                    change_set & cs)
{
  cs.rearrangement.check_sane();
  tid_source ts;
  path_analysis analysis;
  directory_map first_dmap, second_dmap;

  analyze_rearrangement(cs.rearrangement, analysis, ts);
  build_directory_map(analysis.first, first_dmap);
  build_directory_map(analysis.second, second_dmap);

  std::set<file_path> paths;
  extract_path_set(m_new, paths);

  for (std::set<file_path>::const_iterator i = cs.rearrangement.added_files.begin();
       i != cs.rearrangement.added_files.end(); ++i)
    {
      manifest_map::const_iterator j = m_new.find(*i);
      I(j != m_new.end());
      cs.deltas.insert(std::make_pair(*i,
                                      std::make_pair(null_ident,
                                                     manifest_entry_id(j))));
      paths.erase(*i);
    }

  for (std::set<file_path>::const_iterator i = paths.begin();
       i != paths.end(); ++i)
    {
      file_path old_path;
      reconstruct_path(*i, second_dmap, analysis.first, old_path);
      manifest_map::const_iterator j = m_old.find(old_path);
      manifest_map::const_iterator k = m_new.find(*i);
      I(j != m_old.end());
      I(k != m_new.end());
      if (!(manifest_entry_id(j) == manifest_entry_id(k)))
        cs.deltas.insert(std::make_pair(*i, std::make_pair(manifest_entry_id(j),
                                                           manifest_entry_id(k))));
    }

  cs.check_sane();    
}


void
apply_change_set(manifest_map const & old_man,
                 change_set const & cs,
                 manifest_map & new_man)
{
  cs.check_sane();
  change_set a, b;
  build_pure_addition_change_set(old_man, a);
  concatenate_change_sets(a, cs, b);

  // If the composed change_set still has renames or deletions in it, then
  // they referred to things that weren't in the original manifest, and this
  // change_set should never have been applied to this manifest in the first
  // place.
  I(b.rearrangement.deleted_files.empty());
  I(b.rearrangement.renamed_files.empty());
  // Furthermore, all deltas should be add deltas
  for (change_set::delta_map::const_iterator i = b.deltas.begin();
      i != b.deltas.end(); ++i)
    {
      I(null_id(delta_entry_src(i)));
      I(b.rearrangement.added_files.find(delta_entry_path(i))
        != b.rearrangement.added_files.end());
    }

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
  cs.check_sane();
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
  cs.check_sane();
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
    std::string const patch("patch");
    std::string const from("from");
    std::string const to("to");
    std::string const add_file("add_file");
    std::string const delete_file("delete_file");
    std::string const delete_dir("delete_dir");
    std::string const rename_file("rename_file");
    std::string const rename_dir("rename_dir");
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
          parser.sym();
          parser.str(t1);
          cs.add_file(file_path(t1));
        }
      else if (parser.symp(syms::delete_file)) 
        { 
          parser.sym();
          parser.str(t1);
          cs.delete_file(file_path(t1));
        }
      else if (parser.symp(syms::delete_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          cs.delete_dir(file_path(t1));
        }
      else if (parser.symp(syms::rename_file)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cs.rename_file(file_path(t1),
                         file_path(t2));
        }
      else if (parser.symp(syms::rename_dir)) 
        { 
          parser.sym();
          parser.str(t1);
          parser.esym(syms::to);
          parser.str(t2);
          cs.rename_dir(file_path(t1),
                        file_path(t2));
        }
      else
        break;
    }
  cs.rearrangement.check_sane();
}


void 
print_path_rearrangement(basic_io::printer & printer,
                         change_set::path_rearrangement const & pr)
{

  pr.check_sane();
  for (std::set<file_path>::const_iterator i = pr.deleted_files.begin();
       i != pr.deleted_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::delete_file, (*i)());
      printer.print_stanza(st);
    }

  for (std::set<file_path>::const_iterator i = pr.deleted_dirs.begin();
       i != pr.deleted_dirs.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::delete_dir, (*i)());
      printer.print_stanza(st);
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_files.begin();
       i != pr.renamed_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::rename_file, i->first());
      st.push_str_pair(syms::to, i->second());
      printer.print_stanza(st);
    }

  for (std::map<file_path,file_path>::const_iterator i = pr.renamed_dirs.begin();
       i != pr.renamed_dirs.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::rename_dir, i->first());
      st.push_str_pair(syms::to, i->second());
      printer.print_stanza(st);
    }

  for (std::set<file_path>::const_iterator i = pr.added_files.begin();
       i != pr.added_files.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::add_file, (*i)());
      printer.print_stanza(st);
    }
}

void 
parse_change_set(basic_io::parser & parser,
                 change_set & cs)
{
  clear_change_set(cs);

  parse_path_rearrangement(parser, cs);    

  while (parser.symp(syms::patch))
    {
      std::string path, src, dst;
      parser.sym();
      parser.str(path);
      parser.esym(syms::from);
      parser.hex(src);
      parser.esym(syms::to);
      parser.hex(dst);
      cs.deltas.insert(std::make_pair(file_path(path),
                                      std::make_pair(file_id(src),
                                                     file_id(dst))));
    }
  cs.check_sane();
}

void 
print_change_set(basic_io::printer & printer,
                 change_set const & cs)
{
  cs.check_sane();
  print_path_rearrangement(printer, cs.rearrangement);
  
  for (change_set::delta_map::const_iterator i = cs.deltas.begin();
       i != cs.deltas.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::patch, i->first());
      st.push_hex_pair(syms::from, i->second.first.inner()());
      st.push_hex_pair(syms::to, i->second.second.inner()());
      printer.print_stanza(st);
    }
}

void
read_path_rearrangement(data const & dat,
                        change_set::path_rearrangement & re)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "path_rearrangement");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  change_set cs;
  parse_path_rearrangement(pars, cs);
  re = cs.rearrangement;
  I(src.lookahead == EOF);
  re.check_sane();
}

void
read_change_set(data const & dat,
                change_set & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "change_set");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_change_set(pars, cs);
  I(src.lookahead == EOF);
  cs.check_sane();
}

void
write_change_set(change_set const & cs,
                 data & dat)
{
  cs.check_sane();
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_change_set(pr, cs);
  dat = data(oss.str());  
}

void
write_path_rearrangement(change_set::path_rearrangement const & re,
                         data & dat)
{
  re.check_sane();
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
  L(F("[begin changeset %s]\n") % ctx);
  L(F("%s") % tmp);
  L(F("[end changeset %s]\n") % ctx);
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

  manifest_map dummy;

  merge_provider merger(app, dummy, dummy, dummy);
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
    ("rename_file \"foo\"\n"
     "         to \"bar\"\n",

     "rename_file \"apple\"\n"
     "         to \"orange\"\n");

  disjoint_merge_test
    ("rename_file \"foo/a.txt\"\n"
     "         to \"bar/b.txt\"\n",

     "rename_file \"bar/c.txt\"\n"
     "         to \"baz/d.txt\"\n");

  disjoint_merge_test
    ("patch \"foo/file.txt\"\n"
     " from [c6a4a6196bb4a744207e1a6e90273369b8c2e925]\n"
     "   to [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892]\n",

     "rename_file \"foo/file.txt\"\n"
     "         to \"foo/apple.txt\"\n");

  disjoint_merge_test
    (
     "rename_file \"apple.txt\"\n"
     "         to \"pear.txt\"\n"
     "\n"
     "patch \"foo.txt\"\n"
     " from [c6a4a6196bb4a744207e1a6e90273369b8c2e925]\n"
     "   to [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892]\n",
     
     "rename_file \"foo.txt\"\n"
     "         to \"bar.txt\"\n"
     "\n"
     "patch \"apple.txt\"\n"
     " from [fe18ec0c55cbc72e4e51c58dc13af515a2f3a892]\n"
     "   to [435e816c30263c9184f94e7c4d5aec78ea7c028a]\n");  
}

static void 
basic_change_set_test()
{
  try
    {
      
      change_set cs;
      cs.delete_file(file_path("usr/lib/zombie"));
      cs.add_file(file_path("usr/bin/cat"),
                  file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs.add_file(file_path("usr/local/bin/dog"),
                  file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
      cs.rename_file(file_path("usr/local/bin/dog"), file_path("usr/bin/dog"));
      cs.rename_file(file_path("usr/bin/cat"), file_path("usr/local/bin/chicken"));
      cs.add_file(file_path("usr/lib/libc.so"),
                  file_id(hexenc<id>("435e816c30263c9184f94e7c4d5aec78ea7c028a")));
      cs.rename_dir(file_path("usr/lib"), file_path("usr/local/lib"));
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
      cs1.add_file(file_path("usr/lib/zombie"),
                   file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
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

      cs2.add_file(file_path("usr/lib/zombie"),
                   file_id(hexenc<id>("adc83b19e793491b1c6ea0fd8b46cd9f32e592fc")));
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

static const file_id fid_null;
static const file_id fid1 = file_id(hexenc<id>("aaaa3831e5eb74e6cd50b94f9e99e6a14d98d702"));
static const file_id fid2 = file_id(hexenc<id>("bbbb3831e5eb74e6cd50b94f9e99e6a14d98d702"));
static const file_id fid3 = file_id(hexenc<id>("cccc3831e5eb74e6cd50b94f9e99e6a14d98d702"));

typedef enum { in_a, in_b } which_t;
struct bad_concatenate_change_test
{
  change_set a;
  change_set b;
  change_set combined;
  change_set concat;
  bool do_combine;
  std::string ident;
  bad_concatenate_change_test(char const *file, int line) : 
    do_combine(false),
    ident((F("%s:%d") % file % line).str())
  {    
    L(F("BEGINNING concatenation test %s\n") % ident);
  }

  ~bad_concatenate_change_test()
  {
    L(F("FINISHING concatenation test %s\n") % ident);
  }

  change_set & getit(which_t which)
  {
    if (which == in_a)
      return a;
    return b;
  }
  // Call combine() if you want to make sure that the things that are bad when
  // concatenated are also bad when all stuck together into a single
  // changeset.
  void combine() { do_combine = true; }
  void add_file(which_t which, std::string const & path, file_id fid = fid1)
  {
    getit(which).add_file(file_path(path), fid);
    if (do_combine)
      combined.add_file(file_path(path), fid);
  }
  void apply_delta(which_t which, std::string const & path,
                   file_id from_fid,
                   file_id to_fid)
  {
    getit(which).apply_delta(file_path(path), from_fid, to_fid);
    if (do_combine)
      combined.apply_delta(file_path(path), from_fid, to_fid);
  }
  void delete_file(which_t which, std::string const & path)
  {
    getit(which).delete_file(file_path(path));
    if (do_combine)
      combined.delete_file(file_path(path));
  }
  void delete_dir(which_t which, std::string const & path)
  {
    getit(which).delete_dir(file_path(path));
    if (do_combine)
      combined.delete_dir(file_path(path));
  }
  void rename_file(which_t which,
                   std::string const & path1, std::string const & path2)
  {
    getit(which).rename_file(file_path(path1), file_path(path2));
    if (do_combine)
      combined.rename_file(file_path(path1), file_path(path2));
  }
  void rename_dir(which_t which,
                  std::string const & path1, std::string const & path2)
  {
    getit(which).rename_dir(file_path(path1), file_path(path2));
    if (do_combine)
      combined.rename_dir(file_path(path1), file_path(path2));
  }
  void run()
  {
    L(F("RUNNING bad_concatenate_change_test %s\n") % ident);
    try
      {
        dump_change_set("a", a);
        dump_change_set("b", b);
      }
    catch (std::logic_error e)
      {
        L(F("skipping change_set printing, one or both are not sane\n"));
      }
    BOOST_CHECK_THROW(concatenate_change_sets(a, b, concat),
                      std::logic_error);
    try { dump_change_set("concat", concat); }
    catch (std::logic_error e) { L(F("concat change_set is insane\n")); }
    if (do_combine)
      {
        L(F("Checking combined change set\n"));
        change_set empty_cs, combined_concat;
        BOOST_CHECK_THROW(concatenate_change_sets(combined,
                                                  empty_cs,
                                                  combined_concat),
                          std::logic_error);
        try { dump_change_set("combined_concat", combined_concat); }
        catch (std::logic_error e) { L(F("combined_concat is insane\n")); }
      }
  }
  void run_both()
  {
    run();
    L(F("RUNNING bad_concatenate_change_test %s again backwards\n") % ident);
    BOOST_CHECK_THROW(concatenate_change_sets(a, b, concat),
                      std::logic_error);
  }
};

// We also do a number of just "bad change set" tests here, leaving one of
// them empty; this is because our main line of defense against bad
// change_sets, check_sane_history, does its checking by doing
// concatenations, so it's doing concatenations that we want to be sure does
// sanity checking.
static void
bad_concatenate_change_tests()
{
  // Files/directories can't be dropped on top of each other:
  BOOST_CHECKPOINT("on top");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target");
    t.add_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_file(in_a, "foo", "target");
    t.rename_file(in_b, "bar", "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "foo", "target");
    t.rename_dir(in_b, "bar", "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_file(in_a, "foo", "target");
    t.rename_dir(in_b, "bar", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target");
    t.rename_file(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target");
    t.rename_dir(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.add_file(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.rename_file(in_b, "foo", "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.rename_dir(in_b, "foo", "target");
    t.run_both();
  }
  // You can only delete something once
  BOOST_CHECKPOINT("delete once");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_file(in_a, "target");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_dir(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  // You can't delete something that's not there anymore
  BOOST_CHECKPOINT("delete after rename");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.rename_file(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_dir(in_a, "target");
    t.rename_file(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_dir(in_a, "target");
    t.rename_dir(in_b, "target", "foo");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.rename_dir(in_b, "target", "foo");
    t.run_both();
  }
  // Files/directories can't be split in two
  BOOST_CHECKPOINT("splitting files/dirs");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "target", "foo");
    t.rename_file(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "target", "foo");
    t.rename_dir(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "target", "foo");
    t.rename_file(in_b, "target", "bar");
    t.run_both();
  }
  // Files and directories are different
  BOOST_CHECKPOINT("files != dirs");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target/subfile");
    t.rename_file(in_b, "target", "foo");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "foo", "target");
    t.delete_dir(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.apply_delta(in_a, "target", fid1, fid2);
    t.delete_dir(in_b, "target");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "foo", "target");
    t.delete_file(in_b, "target");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.rename_dir(in_a, "foo", "target");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.apply_delta(in_a, "target", fid1, fid2);
    t.rename_dir(in_b, "target", "bar");
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_file(in_a, "foo", "target");
    t.rename_dir(in_b, "target", "bar");
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.rename_dir(in_a, "foo", "target");
    t.rename_file(in_b, "target", "bar");
    t.run();
  }
  // Directories can't be patched, and patches can't be directoried...
  BOOST_CHECKPOINT("can't patch dirs or vice versa");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.apply_delta(in_b, "target", fid_null, fid1);
    t.run_both();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.add_file(in_a, "target/subfile");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run_both();
  }
  // Deltas must be consistent
  BOOST_CHECKPOINT("consistent deltas");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid2);
    t.apply_delta(in_b, "target", fid3, fid1);
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.add_file(in_a, "target", fid1);
    t.apply_delta(in_b, "target", fid2, fid3);
    t.run();
  }
  // Can't have a null source id if it's not an add
  BOOST_CHECKPOINT("null id on non-add");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid_null, fid1);
    t.run();
  }
  // Can't have drop + delta with no add
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.combine();
    t.delete_file(in_a, "target");
    t.apply_delta(in_b, "target", fid1, fid2);
    t.run();
  }
  // Can't have a null destination id, ever, with or without a delete_file
  BOOST_CHECKPOINT("no null destinations");
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.delete_file(in_a, "target");
    t.apply_delta(in_a, "target", fid1, fid_null);
    t.run();
  }
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid_null);
    t.run();
  }
  // Can't have a patch with src == dst
  {
    bad_concatenate_change_test t(__FILE__, __LINE__);
    t.apply_delta(in_a, "target", fid1, fid1);
    t.run();
  }
}

// FIXME: Things that should be added, but can't be trivially because they
// assert too early:
//   anything repeated -- multiple adds, multiple deletes, multiple deltas
//   including <rename a b, rename a c> in one changeset, for both files and dirs
// (probably should put these in strings, and do BOOST_CHECK_THROWS in the
// parser?)

// FIXME: also need tests for the invariants in apply_manifest (and any
// invariants that should be there but aren't, of course)

void 
add_change_set_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&basic_change_set_test));
  suite->add(BOOST_TEST_CASE(&neutralize_change_test));
  suite->add(BOOST_TEST_CASE(&non_interfering_change_test));
  suite->add(BOOST_TEST_CASE(&disjoint_merge_tests));
  suite->add(BOOST_TEST_CASE(&bad_concatenate_change_tests));
}


#endif // BUILD_UNIT_TESTS
