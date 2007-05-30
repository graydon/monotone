// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <fstream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>

#include "botan/botan.h"

#include "file_io.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "platform-wrapped.hh"
#include "numeric_vocab.hh"

namespace fs = boost::filesystem;

// Parts of boost::filesystem change in 1.34 . One particular
// difference is that some exceptions are different now.

#include <boost/version.hpp>

#if BOOST_VERSION < 103400
# define FS_ERROR fs::filesystem_error
# define FS_ERROR_SYSTEM native_error
#else
# define FS_ERROR fs::filesystem_path_error
# define FS_ERROR_SYSTEM system_error
#endif

// this file deals with talking to the filesystem, loading and
// saving files.

using std::cin;
using std::ifstream;
using std::ios_base;
using std::ofstream;
using std::runtime_error;
using std::string;
using std::vector;

void
assert_path_is_nonexistent(any_path const & path)
{
  I(get_path_status(path) == path::nonexistent);
}

void
assert_path_is_file(any_path const & path)
{
  I(get_path_status(path) == path::file);
}

void
assert_path_is_directory(any_path const & path)
{
  I(get_path_status(path) == path::directory);
}

void
require_path_is_nonexistent(any_path const & path,
                            i18n_format const & message)
{
  N(!path_exists(path), message);
}

void
require_path_is_file(any_path const & path,
                     i18n_format const & message_if_nonexistent,
                     i18n_format const & message_if_directory)
{
  switch (get_path_status(path))
    {
    case path::nonexistent:
      N(false, message_if_nonexistent);
      break;
    case path::file:
      return;
    case path::directory:
      N(false, message_if_directory);
      break;
    }
}

void
require_path_is_directory(any_path const & path,
                          i18n_format const & message_if_nonexistent,
                          i18n_format const & message_if_file)
{
  switch (get_path_status(path))
    {
    case path::nonexistent:
      N(false, message_if_nonexistent);
      break;
    case path::file:
      N(false, message_if_file);
    case path::directory:
      return;
      break;
    }
}

bool
path_exists(any_path const & p)
{
  return get_path_status(p) != path::nonexistent;
}

bool
directory_exists(any_path const & p)
{
  return get_path_status(p) == path::directory;
}

bool
file_exists(any_path const & p)
{
  return get_path_status(p) == path::file;
}

bool
directory_empty(any_path const & path)
{
  vector<utf8> files;
  vector<utf8> subdirs;
  
  read_directory(path, files, subdirs);
  
  return files.empty() && subdirs.empty();
}

static bool did_char_is_binary_init;
static bool char_is_binary[256];

static void
set_char_is_binary(char c, bool is_binary)
{
    char_is_binary[static_cast<uint8_t>(c)] = is_binary;
}

static void
init_char_is_binary()
{
  // these do not occur in ASCII text files
  // FIXME: this heuristic is (a) crap and (b) hardcoded. fix both these.
  // Should be calling a lua hook here that can use set_char_is_binary()
  // That will at least fix (b)
  string nontext_chars("\x01\x02\x03\x04\x05\x06\x0e\x0f"
                       "\x10\x11\x12\x13\x14\x15\x16\x17\x18"
                       "\x19\x1a\x1c\x1d\x1e\x1f");
  set_char_is_binary('\0', true);
  for(size_t i = 0; i < nontext_chars.size(); ++i)
    {
      set_char_is_binary(nontext_chars[i], true);
    }
}

bool guess_binary(string const & s)
{
  if (did_char_is_binary_init == false)
    {
      init_char_is_binary();
      did_char_is_binary_init = true;
    }

  for (size_t i = 0; i < s.size(); ++i)
    {
      if (char_is_binary[ static_cast<uint8_t>(s[i]) ])
        return true;
    }
  return false;
}

static fs::path
mkdir(any_path const & p)
{
  return fs::path(p.as_external(), fs::native);
}

void
mkdir_p(any_path const & p)
{
  try
    {
      fs::create_directories(mkdir(p));
    }
  catch (FS_ERROR & err)
    {
      // check for this case first, because in this case, the next line will
      // print "could not create directory: Success".  Which is unhelpful.
      E(get_path_status(p) != path::file,
        F("could not create directory '%s'\nit is a file") % p);
      E(false,
        F("could not create directory '%s'\n%s")
        % err.path1().native_directory_string() % os_strerror(err.FS_ERROR_SYSTEM()));
    }
  require_path_is_directory(p,
                            F("could not create directory '%s'") % p,
                            F("could not create directory '%s'\nit is a file") % p);
}

void
make_dir_for(any_path const & p)
{
  fs::path tmp(p.as_external(), fs::native);
  if (tmp.has_branch_path())
    {
      fs::path dir = tmp.branch_path();
      fs::create_directories(dir);
      N(fs::exists(dir) && fs::is_directory(dir),
        F("failed to create directory '%s' for '%s'") % dir.string() % p);
    }
}

static void
do_shallow_deletion_with_sane_error_message(any_path const & p)
{
  fs::path fp = mkdir(p);
  try
    {
      fs::remove(fp);
    }
  catch (FS_ERROR & err)
    {
      E(false, F("could not remove '%s'\n%s")
        % err.path1().native_directory_string()
        % os_strerror(err.FS_ERROR_SYSTEM()));
    }
}

void
delete_file(any_path const & p)
{
  require_path_is_file(p,
                       F("file to delete '%s' does not exist") % p,
                       F("file to delete, '%s', is not a file but a directory") % p);
  do_shallow_deletion_with_sane_error_message(p);
}

void
delete_dir_shallow(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete '%s' does not exist") % p,
                            F("directory to delete, '%s', is not a directory but a file") % p);
  do_shallow_deletion_with_sane_error_message(p);
}

void
delete_file_or_dir_shallow(any_path const & p)
{
  N(path_exists(p), F("object to delete, '%s', does not exist") % p);
  do_shallow_deletion_with_sane_error_message(p);
}

void
delete_dir_recursive(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete, '%s', does not exist") % p,
                            F("directory to delete, '%s', is a file") % p);
  fs::remove_all(mkdir(p));
}

void
move_file(any_path const & old_path,
          any_path const & new_path)
{
  require_path_is_file(old_path,
                       F("rename source file '%s' does not exist") % old_path,
                       F("rename source file '%s' is a directory "
                         "-- bug in monotone?") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists") % new_path);
  fs::rename(mkdir(old_path), mkdir(new_path));
}

void
move_dir(any_path const & old_path,
         any_path const & new_path)
{
  require_path_is_directory(old_path,
                            F("rename source dir '%s' does not exist") % old_path,
                            F("rename source dir '%s' is a file "
                              "-- bug in monotone?") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists") % new_path);
  fs::rename(mkdir(old_path), mkdir(new_path));
}

void
move_path(any_path const & old_path,
          any_path const & new_path)
{
  switch (get_path_status(old_path))
    {
    case path::nonexistent:
      N(false, F("rename source path '%s' does not exist") % old_path);
      break;
    case path::file:
      move_file(old_path, new_path);
      break;
    case path::directory:
      move_dir(old_path, new_path);
      break;
    }
}

void
read_data(any_path const & p, data & dat)
{
  require_path_is_file(p,
                       F("file %s does not exist") % p,
                       F("file %s cannot be read as data; it is a directory") % p);

  ifstream file(p.as_external().c_str(),
                ios_base::in | ios_base::binary);
  N(file, F("cannot open file %s for reading") % p);
  Botan::Pipe pipe;
  pipe.start_msg();
  file >> pipe;
  pipe.end_msg();
  dat = data(pipe.read_all_as_string());
}

void read_directory(any_path const & path,
                    vector<utf8> & files,
                    vector<utf8> & dirs)
{
  files.clear();
  dirs.clear();
  fs::directory_iterator ei;
  fs::path native_path = fs::path(system_path(path).as_external(), fs::native);
  for (fs::directory_iterator di(native_path);
       di != ei; ++di)
    {
      fs::path entry = *di;
      if (!fs::exists(entry)
          || entry.string() == "."
          || entry.string() == "..")
        continue;

      // FIXME: BUG: this screws up charsets (assumes blindly that the fs is
      // utf8)
      if (fs::is_directory(entry))
        dirs.push_back(utf8(entry.leaf()));
      else
        files.push_back(utf8(entry.leaf()));
    }
}


// This function can only be called once per run.
void
read_data_stdin(data & dat)
{
  static bool have_consumed_stdin = false;
  N(!have_consumed_stdin, F("Cannot read standard input multiple times"));
  have_consumed_stdin = true;
  Botan::Pipe pipe;
  pipe.start_msg();
  cin >> pipe;
  pipe.end_msg();
  dat = data(pipe.read_all_as_string());
}

void
read_data_for_command_line(utf8 const & path, data & dat)
{
  if (path() == "-")
    read_data_stdin(dat);
  else
    read_data(system_path(path), dat);
}


// FIXME: this is probably not enough brains to actually manage "atomic
// filesystem writes". at some point you have to draw the line with even
// trying, and I'm not sure it's really a strict requirement of this tool,
// but you might want to make this code a bit tighter.

static void
write_data_impl(any_path const & p,
                data const & dat,
                any_path const & tmp,
                bool user_private)
{
  N(!directory_exists(p),
    F("file '%s' cannot be overwritten as data; it is a directory") % p);

  make_dir_for(p);

  write_data_worker(p.as_external(), dat(), tmp.as_external(), user_private);
}

void
write_data(file_path const & path, data const & dat)
{
  // use the bookkeeping root as the temporary directory.
  assert_path_is_directory(bookkeeping_root);
  write_data_impl(path, dat, bookkeeping_root, false);
}

void
write_data(bookkeeping_path const & path, data const & dat)
{
  // use the bookkeeping root as the temporary directory.
  assert_path_is_directory(bookkeeping_root);
  write_data_impl(path, dat, bookkeeping_root, false);
}

void
write_data(system_path const & path,
           data const & data,
           system_path const & tmpdir)
{
  write_data_impl(path, data, tmpdir, false);
}

void
write_data_userprivate(system_path const & path,
                       data const & data,
                       system_path const & tmpdir)
{
  write_data_impl(path, data, tmpdir, true);
}

tree_walker::~tree_walker() {}

static inline bool
try_file_pathize(fs::path const & p, file_path & fp)
{
  try
    {
      // FIXME BUG: This has broken charset handling
      fp = file_path_internal(p.string());
      return true;
    }
  catch (runtime_error const & c)
    {
      // This arguably has broken charset handling too...
      W(F("caught runtime error %s constructing file path for %s")
        % c.what() % p.string());
      return false;
    }
}

static void
walk_tree_recursive(fs::path const & absolute,
                    fs::path const & relative,
                    tree_walker & walker)
{
  system_path root(absolute.string());
  vector<utf8> files, dirs;

  read_directory(root, files, dirs);

  // At this point, the directory iterator has gone out of scope, and its
  // memory released.  This is important, because it can allocate rather a
  // bit of memory (especially on ReiserFS, see [1]; opendir uses the
  // filesystem's blocksize as a clue how much memory to allocate).  We used
  // to recurse into subdirectories directly in the loop below; this left
  // the memory describing _this_ directory pinned on the heap.  Then our
  // recursive call itself made another recursive call, etc., causing a huge
  // spike in peak memory.  By splitting the loop in half, we avoid this
  // problem. By using read_directory instead of a directory_iterator above
  // we hopefully make this all a bit more clear.
  // 
  // [1] http://lkml.org/lkml/2006/2/24/215

  for (vector<utf8>::const_iterator i = files.begin(); i != files.end(); ++i)
    {
      // the fs::native is necessary here, or it will bomb out on any paths
      // that look at it funny.  (E.g., rcs files with "," in the name.)
      fs::path rel_entry = relative / fs::path((*i)(), fs::native);
      rel_entry.normalize();

      file_path p;
      if (!try_file_pathize(rel_entry, p))
        continue;
      walker.visit_file(p);
    }

  for (vector<utf8>::const_iterator i = dirs.begin(); i != dirs.end(); ++i)
    {
      // the fs::native is necessary here, or it will bomb out on any paths
      // that look at it funny.  (E.g., rcs files with "," in the name.)
      fs::path entry = absolute / fs::path((*i)(), fs::native);
      fs::path rel_entry = relative / fs::path((*i)(), fs::native);
      entry.normalize();
      rel_entry.normalize();

      // FIXME BUG: this utf8() cast is a total lie
      if (bookkeeping_path::internal_string_is_bookkeeping_path(utf8(rel_entry.string())))
        {
          L(FL("ignoring book keeping entry %s") % rel_entry.string());
          continue;
        }

      file_path p;
      if (!try_file_pathize(rel_entry, p))
        continue;
      if (walker.visit_dir(p))
        walk_tree_recursive(entry, rel_entry, walker);
    }
}

bool
tree_walker::visit_dir(file_path const & path)
{
  return true;
}


// from some (safe) sub-entry of cwd
void
walk_tree(file_path const & path,
          tree_walker & walker)
{
  if (path.empty())
    {
      walk_tree_recursive(fs::current_path(), fs::path(), walker);
      return;
    }

  switch (get_path_status(path))
    {
    case path::nonexistent:
      N(false, F("no such file or directory: '%s'") % path);
      break;
    case path::file:
      walker.visit_file(path);
      break;
    case path::directory:
      if (walker.visit_dir(path))
        walk_tree_recursive(system_path(path).as_external(),
                            path.as_external(),
                            walker);
      break;
    }
}

bool
ident_existing_file(file_path const & p, file_id & ident)
{
  switch (get_path_status(p))
    {
    case path::nonexistent:
      return false;
    case path::file:
      break;
    case path::directory:
      W(F("expected file '%s', but it is a directory.") % p);
      return false;
    }

  hexenc<id> id;
  calculate_ident(p, id);
  ident = file_id(id);

  return true;
}

void
calculate_ident(file_path const & file,
                hexenc<id> & ident)
{
  // no conversions necessary, use streaming form
  // Best to be safe and check it isn't a dir.
  assert_path_is_file(file);
  Botan::Pipe p(new Botan::Hash_Filter("SHA-160"), new Botan::Hex_Encoder());
  Botan::DataSource_Stream infile(file.as_external(), true);
  p.process_msg(infile);

  ident = hexenc<id>(lowercase(p.read_all_as_string()));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
