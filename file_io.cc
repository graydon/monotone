// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <fstream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>

#include "botan/botan.h"

#include "file_io.hh"
#include "lua.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "platform.hh"

// this file deals with talking to the filesystem, loading and
// saving files.

using namespace std;

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
ident_existing_file(file_path const & p, file_id & ident, lua_hooks & lua)
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
  calculate_ident(p, id, lua);
  ident = file_id(id);

  return true;
}

static bool did_char_is_binary_init;
static bool char_is_binary[256];

void
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
  set_char_is_binary('\0',true);
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
  catch (fs::filesystem_error & err)
    {
      // check for this case first, because in this case, the next line will
      // print "could not create directory: Success".  Which is unhelpful.
      E(get_path_status(p) != path::file,
        F("could not create directory '%s'\nit is a file") % p);
      E(false,
        F("could not create directory '%s'\n%s")
        % err.path1().native_directory_string() % strerror(err.native_error()));
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
  catch (fs::filesystem_error & err)
    {
      E(false, F("could not remove '%s'\n%s")
        % err.path1().native_directory_string()
        % strerror(err.native_error()));
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
  dat = pipe.read_all_as_string();
}

void 
read_localized_data(file_path const & path, 
                    data & dat, 
                    lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;
  
  bool do_lineconv = (lua.hook_get_linesep_conv(path, db_linesep, ext_linesep) 
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(path, db_charset, ext_charset) 
                      && db_charset != ext_charset);

  data tdat;
  read_data(path, tdat);
  
  string tmp1, tmp2;
  tmp2 = tdat();
  if (do_charconv) {
    tmp1 = tmp2;
    charset_convert(ext_charset, db_charset, tmp1, tmp2);
  }
  if (do_lineconv) {
    tmp1 = tmp2;
    line_end_convert(db_linesep, tmp1, tmp2);
  }
  dat = tmp2;
}

void read_directory(any_path const & path,
                    std::vector<utf8> & files,
                    std::vector<utf8> & dirs)
{
  files.clear();
  dirs.clear();
  fs::directory_iterator ei;
  for (fs::directory_iterator di(system_path(path).as_external());
       di != ei; ++di)
    {
      fs::path entry = *di;
      if (!fs::exists(entry)
          || di->string() == "."
          || di->string() == "..")
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
static void
read_data_stdin(data & dat)
{
  static bool have_consumed_stdin = false;
  N(!have_consumed_stdin, F("Cannot read standard input multiple times"));
  have_consumed_stdin = true;
  Botan::Pipe pipe;
  pipe.start_msg();
  cin >> pipe;
  pipe.end_msg();
  dat = pipe.read_all_as_string();
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
                any_path const & tmp)
{  
  N(!directory_exists(p),
    F("file '%s' cannot be overwritten as data; it is a directory") % p);

  make_dir_for(p);

  {
    // data.tmp opens
    ofstream file(tmp.as_external().c_str(),
                  ios_base::out | ios_base::trunc | ios_base::binary);
    N(file, F("cannot open file %s for writing") % tmp);
    Botan::Pipe pipe(new Botan::DataSink_Stream(file));
    pipe.process_msg(dat());
    // data.tmp closes
  }

  rename_clobberingly(tmp, p);
}

static void
write_data_impl(any_path const & p,
                data const & dat)
{  
  // we write, non-atomically, to MT/data.tmp.
  // nb: no mucking around with multiple-writer conditions. we're a
  // single-user single-threaded program. you get what you paid for.
  assert_path_is_directory(bookkeeping_root);
  bookkeeping_path tmp = bookkeeping_root / (boost::format("data.tmp.%d") %
                                             get_process_id()).str();
  write_data_impl(p, dat, tmp);
}

void 
write_data(file_path const & path, data const & dat)
{ 
  write_data_impl(path, dat); 
}

void 
write_data(bookkeeping_path const & path, data const & dat)
{ 
  write_data_impl(path, dat); 
}

void 
write_localized_data(file_path const & path, 
                     data const & dat, 
                     lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;
  
  bool do_lineconv = (lua.hook_get_linesep_conv(path, db_linesep, ext_linesep) 
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(path, db_charset, ext_charset) 
                      && db_charset != ext_charset);
  
  string tmp1, tmp2;
  tmp2 = dat();
  if (do_lineconv) {
    tmp1 = tmp2;
    line_end_convert(ext_linesep, tmp1, tmp2);
  }
  if (do_charconv) {
    tmp1 = tmp2;
    charset_convert(db_charset, ext_charset, tmp1, tmp2);
  }

  write_data(path, data(tmp2));
}

void
write_data(system_path const & path,
           data const & data,
           system_path const & tmpdir)
{
  write_data_impl(path, data, tmpdir / (boost::format("data.tmp.%d") %
                                             get_process_id()).str());
}

tree_walker::~tree_walker() {}

static void 
walk_tree_recursive(fs::path const & absolute,
                    fs::path const & relative,
                    tree_walker & walker)
{
  fs::directory_iterator ei;
  for(fs::directory_iterator di(absolute);
      di != ei; ++di)
    {
      fs::path entry = *di;
      // the fs::native is necessary here, or it will bomb out on any paths
      // that look at it funny.  (E.g., rcs files with "," in the name.)
      fs::path rel_entry = relative / fs::path(entry.leaf(), fs::native);
      
      if (bookkeeping_path::is_bookkeeping_path(rel_entry.normalize().string()))
        {
          L(FL("ignoring book keeping entry %s\n") % rel_entry.string());
          continue;
        }
      
      if (!fs::exists(entry) 
          || di->string() == "." 
          || di->string() == "..") 
        {
          // ignore
          continue;
        }
      else
        {
          file_path p;
          try 
            {
              // FIXME: BUG: this screws up charsets
              p = file_path_internal(rel_entry.normalize().string());
            }
          catch (std::runtime_error const & c)
            {
              W(F("caught runtime error %s constructing file path for %s\n") 
                % c.what() % rel_entry.string());
              continue;
            }
          if (fs::is_directory(entry))
            {
              walker.visit_dir(p);
              walk_tree_recursive(entry, rel_entry, walker);
            }
          else
            {
              walker.visit_file(p);
            }

        }
    }
}

void
tree_walker::visit_dir(file_path const & path)
{
}


// from some (safe) sub-entry of cwd
void 
walk_tree(file_path const & path,
          tree_walker & walker,
          bool require_existing_path)
{
  if (path.empty())
    {
      walk_tree_recursive(fs::current_path(), fs::path(), walker);
      return;
    }
      
  switch (get_path_status(path))
    {
    case path::nonexistent:
      N(!require_existing_path, F("no such file or directory: '%s'") % path);
      walker.visit_file(path);
      break;
    case path::file:
      walker.visit_file(path);
      break;
    case path::directory:
      walker.visit_dir(path);
      walk_tree_recursive(system_path(path).as_external(),
                          path.as_external(),
                          walker);
      break;
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

void 
add_file_io_tests(test_suite * suite)
{
  I(suite);
  // none, ATM.
}

#endif // BUILD_UNIT_TESTS
