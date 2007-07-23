// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <fstream>

#include "botan/botan.h"

#include "file_io.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "platform-wrapped.hh"
#include "numeric_vocab.hh"

// this file deals with talking to the filesystem, loading and
// saving files.

using std::cin;
using std::ifstream;
using std::ios_base;
using std::ofstream;
using std::logic_error;
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

namespace
{
  struct directory_not_empty_exception {};
  struct directory_empty_helper : public dirent_consumer
  {
    virtual void consume(char const *)
    { throw directory_not_empty_exception(); }
  };
}

bool
directory_empty(any_path const & path)
{
  directory_empty_helper h;
  try {
    do_read_directory(system_path(path).as_external(), h, h, h);
  } catch (directory_not_empty_exception) {
    return false;
  }
  return true;
}

static bool did_char_is_binary_init;
static bool char_is_binary[256];

static void
set_char_is_binary(char c, bool is_binary)
{
    char_is_binary[static_cast<u8>(c)] = is_binary;
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
      if (char_is_binary[ static_cast<u8>(s[i]) ])
        return true;
    }
  return false;
}

void
mkdir_p(any_path const & p)
{
  switch (get_path_status(p))
    {
    case path::directory:
      return;
    case path::file:
      E(false, F("could not create directory '%s': it is a file") % p);
    case path::nonexistent:
      std::string const current = p.as_external();
      any_path const parent = p.dirname();
      if (current != parent.as_external())
        {
          mkdir_p(parent);
        }
      do_mkdir(current);
    }
}

void
make_dir_for(any_path const & p)
{
  mkdir_p(p.dirname());
}

void
delete_file(any_path const & p)
{
  require_path_is_file(p,
                       F("file to delete '%s' does not exist") % p,
                       F("file to delete, '%s', is not a file but a directory") % p);
  do_remove(p.as_external());
}

void
delete_dir_shallow(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete '%s' does not exist") % p,
                            F("directory to delete, '%s', is not a directory but a file") % p);
  do_remove(p.as_external());
}

void
delete_file_or_dir_shallow(any_path const & p)
{
  N(path_exists(p), F("object to delete, '%s', does not exist") % p);
  do_remove(p.as_external());
}

namespace
{
  struct fill_pc_vec : public dirent_consumer
  {
    fill_pc_vec(vector<path_component> & v) : v(v) { v.clear(); }

    // FIXME BUG: this treats 's' as being already utf8, but it is actually
    // in the external character set.  Also, will I() out on invalid
    // pathnames, when it should N() or perhaps W() and skip.
    virtual void consume(char const * s)
    { v.push_back(path_component(s)); }

  private:
    vector<path_component> & v;
  };

  struct file_deleter : public dirent_consumer
  {
    file_deleter(any_path const & p) : parent(p) {}
    virtual void consume(char const * f)
    {
      // FIXME: same bug as above.
      do_remove((parent / path_component(f)).as_external());
    }
  private:
    any_path const & parent;
  };
}

static void
do_remove_recursive(any_path const & p)
{
  // for the reasons described in walk_tree_recursive, we read the entire
  // directory before recursing into any subdirs.  however, it is safe to
  // delete files as we encounter them, and we do so.
  vector<path_component> subdirs;
  fill_pc_vec subdir_fill(subdirs);
  file_deleter delete_files(p);

  do_read_directory(p.as_external(), delete_files, subdir_fill, delete_files);
  for (vector<path_component>::const_iterator i = subdirs.begin();
       i != subdirs.end(); i++)
    do_remove_recursive(p / *i);

  do_remove(p.as_external());
}


void
delete_dir_recursive(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete, '%s', does not exist") % p,
                            F("directory to delete, '%s', is a file") % p);

  do_remove_recursive(p);
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
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
}

void
move_dir(any_path const & old_path,
         any_path const & new_path)
{
  require_path_is_directory(old_path,
                            F("rename source dir '%s' does not exist")
                            % old_path,
                            F("rename source dir '%s' is a file "
                              "-- bug in monotone?") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
}

void
move_path(any_path const & old_path,
          any_path const & new_path)
{
  N(path_exists(old_path),
    F("rename source path '%s' does not exist") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
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
                    vector<path_component> & files,
                    vector<path_component> & dirs)
{
  vector<path_component> special_files;
  fill_pc_vec ff(files), df(dirs), sf(special_files);
  do_read_directory(path.as_external(), ff, df, sf);
  E(special_files.empty(), F("cannot handle special files in dir '%s'") % path);
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

// recursive directory walking

tree_walker::~tree_walker() {}

bool
tree_walker::visit_dir(file_path const & path)
{
  return true;
}

// subroutine of walk_tree_recursive: if the path composition of PATH and PC
// is a valid file_path, write it to ENTRY and return true.  otherwise,
// generate an appropriate diagnostic and return false.  in this context, an
// invalid path is *not* an invariant failure, because it came from a
// directory scan.  ??? arguably belongs as a file_path method.
static bool
safe_compose(file_path const & path, path_component const & pc, bool isdir,
             file_path & entry)
{
  try
    {
      entry = path / pc;
      return true;
    }
  catch (logic_error)
    {
      // do what the above operator/ did, by hand, and then figure out what
      // sort of diagnostic to issue.
      utf8 badpth;
      if (path.empty())
        badpth = utf8(pc());
      else
        badpth = utf8(path.as_internal() + "/" + pc());

      if (!isdir)
        W(F("skipping file '%s' with unsupported name") % badpth);
      else if (bookkeeping_path::internal_string_is_bookkeeping_path(badpth))
        L(FL("ignoring bookkeeping directory '%s'") % badpth);
      else
        W(F("skipping directory '%s' with unsupported name") % badpth);
      return false;
    }
}

static void
walk_tree_recursive(file_path const & path,
                    tree_walker & walker)
{
  // Read the directory up front, so that the directory handle is released
  // before we recurse.  This is important, because it can allocate rather a
  // bit of memory (especially on ReiserFS, see [1]; opendir uses the
  // filesystem's blocksize as a clue how much memory to allocate).  We used
  // to recurse into subdirectories on the fly; this left the memory
  // describing _this_ directory pinned on the heap.  Then our recursive
  // call itself made another recursive call, etc., causing a huge spike in
  // peak memory.  By splitting the loop in half, we avoid this problem.
  // 
  // [1] http://lkml.org/lkml/2006/2/24/215
  vector<path_component> files, dirs;
  read_directory(path, files, dirs);

  for (vector<path_component>::const_iterator i = files.begin();
       i != files.end(); ++i)
    {
      file_path entry;
      if (safe_compose(path, *i, false, entry))
        walker.visit_file(entry);
    }

  for (vector<path_component>::const_iterator i = dirs.begin();
       i != dirs.end(); ++i)
    {
      file_path entry;
      if (safe_compose(path, *i, true, entry))
        if (walker.visit_dir(entry))
          walk_tree_recursive(entry, walker);
    }
}

// from some (safe) sub-entry of cwd
void
walk_tree(file_path const & path, tree_walker & walker)
{
  if (path.empty())
    {
      walk_tree_recursive(path, walker);
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
        walk_tree_recursive(path, walker);
      break;
    }
}

bool
ident_existing_file(file_path const & p, file_id & ident)
{
  return ident_existing_file(p, ident, get_path_status(p));
}

bool
ident_existing_file(file_path const & p, file_id & ident, path::status status)
{
  switch (status)
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
