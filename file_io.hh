#ifndef __FILE_IO_H__
#define __FILE_IO_H__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"

// this layer deals with talking to the filesystem, loading and saving
// files, walking trees, etc.
//
// we have *three* types of file path we're willing to deal with.
// 
// - a boost::filesystem::path (fs::path)
//   [ defined in boost/filesystem/path.hpp ]
//   this is a generally portable path to anywhere in the fs
//
// - a local_path
//   [ defined in vocab.{hh,cc} ]
//   this is a path to a file in-or-under the current directory; it doesn't
//   escape cwd, and it's name restrictions are tighter than an fs::path,
//   as far as illegal characters and junk
//
// - a file_path
//   [ defined in vocab.{hh,cc} ]
//   this is a local_path which is *not* in the MT/ book-keeping
//   directory. in other words, it's a local_path which may permissibly be
//   part of a manifest.
//
// several functions in *this* file are defined on more than one of these
// sorts of paths. the reason for the multiple path types is to avoid ever
// constructing (and "forgetting to check the validity" of) an illegal
// value in *other* parts of the code. this file contains stuff which is so
// low level we can't mostly know whether what's being asked for is legal.

struct lua_hooks;

void save_initial_path();
bool find_working_copy(fs::path const & search_root, 
                       fs::path & working_copy_root, 
                       fs::path & working_copy_restriction);

fs::path mkpath(std::string const & s);

std::string get_homedir();
std::string absolutify(std::string const & path);
std::string absolutify_for_command_line(std::string const & path);
std::string tilde_expand(std::string const & path);

extern std::string const book_keeping_dir;

//   - file is inside the private MT/ directory
bool book_keeping_file(local_path const & path);

bool directory_exists(local_path const & path);
bool directory_exists(file_path const & path);
bool file_exists(local_path const & path);
bool file_exists(file_path const & path);

void mkdir_p(local_path const & path);
void mkdir_p(file_path const & path);
void make_dir_for(file_path const & p);

void delete_file(file_path const & path);
void delete_file(local_path const & path);
void delete_dir_recursive(file_path const & path);
void delete_dir_recursive(local_path const & path);

void move_file(file_path const & old_path,
               file_path const & new_path);
void move_file(local_path const & old_path,
               local_path const & new_path);

void move_dir(file_path const & old_path,
              file_path const & new_path);
void move_dir(local_path const & old_path,
              local_path const & new_path);

void read_data(local_path const & path, data & data);
void read_data(local_path const & path, base64< gzip<data> > & data);
void read_data(file_path const & path, data & data);
void read_data(file_path const & path, base64< gzip<data> > & data);
void read_localized_data(file_path const & path, 
                         data & dat, 
                         lua_hooks & lua);
void read_localized_data(file_path const & path,
                         base64< gzip<data> > & dat,
                         lua_hooks & lua);

// This function knows that "-" means "stdin".
void read_data_for_command_line(utf8 const & path, data & dat);

void write_data(local_path const & path, data const & data);
void write_data(local_path const & path, base64< gzip<data> > const & data);
void write_data(file_path const & path, data const & data);
void write_data(file_path const & path, base64< gzip<data> > const & data);
void write_localized_data(file_path const & path, 
                          data const & dat, 
                          lua_hooks & lua);
void write_localized_data(file_path const & path,
                          base64< gzip<data> > const & dat,
                          lua_hooks & lua);

class tree_walker
{
public:
  virtual void visit_file(file_path const & path) = 0;
  virtual ~tree_walker();
};

// from cwd (nb: we can't describe cwd as a file_path)
void walk_tree(tree_walker & walker);

// from some safe sub-dir of cwd
void walk_tree(file_path const & path,
               tree_walker & walker,
               bool require_existing_path = true);



#endif // __FILE_IO_H__
