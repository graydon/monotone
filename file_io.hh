#ifndef __FILE_IO_H__
#define __FILE_IO_H__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include "paths.hh"
#include "sanity.hh"
#include "platform-wrapped.hh"
#include "vector.hh"

// this layer deals with talking to the filesystem, loading and saving
// files, walking trees, etc.

// this code mostly deals in any_path's, because these operations are too low
// level for us to say whether applying them in any given case is valid or
// not.

// use I()
void assert_path_is_nonexistent(any_path const & path);
void assert_path_is_file(any_path const & path);
void assert_path_is_directory(any_path const & path);

// use N()
void require_path_is_nonexistent(any_path const & path,
                                 i18n_format const & message);
void require_path_is_file(any_path const & path,
                          i18n_format const & message_if_nonexistent,
                          i18n_format const & message_if_directory);
void require_path_is_directory(any_path const & path,
                               i18n_format const & message_if_nonexistent,
                               i18n_format const & message_if_file);

// returns true if there is a file or directory at 'path'
bool path_exists(any_path const & path);
// returns true if there is a directory at 'path'
bool directory_exists(any_path const & path);
// returns true if there is a file at 'path'
bool file_exists(any_path const & path);
// returns true if there is a directory at 'path' with no files or sub-directories
bool directory_empty(any_path const & path);


// returns true if the string content is binary according to monotone heuristic
bool guess_binary(std::string const & s);

void mkdir_p(any_path const & path);
void make_dir_for(any_path const & p);

void delete_file(any_path const & path);
void delete_dir_shallow(any_path const & path);
void delete_file_or_dir_shallow(any_path const & path);
void delete_dir_recursive(any_path const & path);

void move_file(any_path const & old_path,
               any_path const & new_path);

void move_dir(any_path const & old_path,
              any_path const & new_path);

// calls move_file or move_dir as appropriate
void move_path(any_path const & old_path,
               any_path const & new_path);

void read_data(any_path const & path, data & data);

void read_directory(any_path const & path,
                    std::vector<path_component> & files,
                    std::vector<path_component> & dirs);

void read_data_stdin(data & dat);

// This function knows that "-" means "stdin".
void read_data_for_command_line(utf8 const & path, data & dat);

// These are not any_path's because we make our write somewhat atomic -- we
// first write to a temp file in _MTN/ (and it must be in _MTN/, not like /tmp
// or something, because we can't necessarily atomic rename from /tmp to the
// workspace).  But that means we can't use it in general, only for the
// workspace.
void write_data(file_path const & path, data const & data);
void write_data(bookkeeping_path const & path, data const & data);

// Version that takes a system_path. To work with the "somewhat atomic"
// goal, it also takes as an argument the place to put the temp file. Whoever
// uses this is responsible to make sure that the tmpdir argument is somewhere
// that the file can be atomically renamed from (same file system)
void write_data(system_path const & path,
                data const & data,
                system_path const & tmpdir);

// Identical to the above, but the file will be inaccessible to anyone but
// the user.  Use for things like private keys.
void write_data_userprivate(system_path const & path,
                            data const & data,
                            system_path const & tmpdir);

class tree_walker
{
public:
  // returns true if the directory should be descended into
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path) = 0;
  virtual ~tree_walker();
};

// from some safe sub-dir of cwd
// file_path of "" means cwd
void walk_tree(file_path const & path,
               tree_walker & walker);


bool ident_existing_file(file_path const & p, file_id & ident);
bool ident_existing_file(file_path const & p, file_id & ident, path::status status);

void calculate_ident(file_path const & file,
                     hexenc<id> & ident);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __FILE_IO_H__
