#ifndef __CHANGE_SET_HH__
#define __CHANGE_SET_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include <boost/shared_ptr.hpp>

#include "interner.hh"
#include "manifest.hh"
#include "vocab.hh"

// a change_set is a text object. It has a precise, normalizable serial form
// as UTF-8 text.
// 
// note that this object is made up of two important sub-components: 
// path_edits and deltas. "path_edits" is exactly the same object stored
// in MT/work. 

struct
change_set
{  

  typedef std::map<file_path, std::pair<file_id, file_id> > delta_map;

  struct
  path_rearrangement
  {
    std::set<file_path> deleted_files;
    std::set<file_path> deleted_dirs;
    std::map<file_path, file_path> renamed_files;
    std::map<file_path, file_path> renamed_dirs;
    std::set<file_path> added_files;

    path_rearrangement() {}
    path_rearrangement(path_rearrangement const & other);
    path_rearrangement const & operator=(path_rearrangement const & other);
    bool operator==(path_rearrangement const & other) const;
    bool empty() const;
    void check_sane() const;
    void check_sane(delta_map const &) const;

    bool has_added_file(file_path const & file) const;
    bool has_deleted_file(file_path const & file) const;
    bool has_renamed_file_dst(file_path const & file) const;
    bool has_renamed_file_src(file_path const & file) const;
  };
  
  path_rearrangement rearrangement;
  delta_map deltas;

  change_set() {}
  change_set(change_set const & other);
  change_set const &operator=(change_set const & other);
  bool operator==(change_set const & other) const;
  void check_sane() const;
  bool empty() const;
  void add_file(file_path const & a);
  void add_file(file_path const & a, file_id const & ident);
  void apply_delta(file_path const & path, 
                   file_id const & src, 
                   file_id const & dst);
  void delete_file(file_path const & d);
  void delete_dir(file_path const & d);
  void rename_file(file_path const & a, file_path const & b);
  void rename_dir(file_path const & a, file_path const & b);
};

inline bool 
null_name(file_path const & p)
{
  return p().empty();
}

inline bool 
null_id(file_id const & i)
{
  return i.inner()().empty();
}

inline bool 
null_id(manifest_id const & i)
{
  return i.inner()().empty();
}

inline bool 
null_id(revision_id const & i)
{
  return i.inner()().empty();
}

inline file_path const & 
delta_entry_path(change_set::delta_map::const_iterator i)
{
  return i->first;
}

inline file_id const & 
delta_entry_src(change_set::delta_map::const_iterator i)
{
  return i->second.first;
}

inline file_id const & 
delta_entry_dst(change_set::delta_map::const_iterator i)
{
  return i->second.second;
}

void
apply_rearrangement_to_filesystem(change_set::path_rearrangement const & re,
                                  local_path const & temporary_root);


// merging and concatenating 

void
normalize_change_set(change_set & n);

void
normalize_path_rearrangement(change_set::path_rearrangement & n);

void
concatenate_rearrangements(change_set::path_rearrangement const & a,
                           change_set::path_rearrangement const & b,
                           change_set::path_rearrangement & concatenated);

void
concatenate_change_sets(change_set const & a,
                        change_set const & b,
                        change_set & concatenated);

struct merge_provider;

void
merge_change_sets(change_set const & a,
                  change_set const & b,
                  change_set & a_merged,
                  change_set & b_merged,
                  merge_provider & merger,
                  app_state & app);

// value-oriented access to printers and parsers

void
read_path_rearrangement(data const & dat,
                        change_set::path_rearrangement & re);

void
write_path_rearrangement(change_set::path_rearrangement const & re,
                         data & dat);

void
read_change_set(data const & dat,
                change_set & cs);

void
write_change_set(change_set const & cs,
                 data & dat);

void
apply_path_rearrangement(path_set const & old_ps,
                         change_set::path_rearrangement const & pr,
                         path_set & new_ps);

void 
complete_change_set(manifest_map const & m_old,
                    manifest_map const & m_new,
                    change_set & cs);

void
build_pure_addition_change_set(manifest_map const & man,
                               change_set & cs);

void
apply_change_set(manifest_map const & old_man,
                 change_set const & cs,
                 manifest_map & new_man);

// quick, optimistic and destructive versions

void
apply_path_rearrangement(change_set::path_rearrangement const & pr,
                         path_set & ps);

file_path
apply_change_set_inverse(change_set const & cs, 
                         file_path const & file_in_second);

void
apply_change_set(change_set const & cs,
                 manifest_map & man);

void 
invert_change_set(change_set const & a2b,
                  manifest_map const & a_map,
                  change_set & b2a);


// basic_io access to printers and parsers

namespace basic_io { struct printer; struct parser; }

void 
print_change_set(basic_io::printer & printer,
                 change_set const & cs);

void 
parse_change_set(basic_io::parser & parser,
                 change_set & cs);

void 
print_path_rearrangement(basic_io::printer & basic_printer,
                         change_set::path_rearrangement const & pr);

void 
parse_path_rearrangement(basic_io::parser & pa,
                         change_set::path_rearrangement & pr);

#endif // __CHANGE_SET_HH__
