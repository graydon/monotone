// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "cryptopp/filters.h"
#include "cryptopp/files.h"

#include "file_io.hh"
#include "lua.hh"
#include "sanity.hh"
#include "transforms.hh"

// this file deals with talking to the filesystem, loading and
// saving files.

using namespace std;

string const book_keeping_dir("MT");


#include <stdlib.h>
#include <unistd.h>
#ifndef WIN32
 #include <pwd.h>
#endif
#include <sys/types.h>

void 
save_initial_path()
{
  fs::initial_path();
  L(F("initial path is %s\n") % fs::initial_path().string());
}

bool
find_working_copy(fs::path const & search_root,
                  fs::path & working_copy_root, 
                  fs::path & working_copy_restriction) 
{
  fs::path bookdir = mkpath(book_keeping_dir);
  fs::path current = fs::initial_path();
  fs::path removed;
  fs::path check = current / bookdir;

  L(F("searching for '%s' directory with root '%s'\n") 
    % bookdir.string()
    % search_root.string());

  // nb: boost 1.32.0 has added operations::equivalent(path1, path2)
  // and ==, !=, ... on paths which are probably better than
  // native_directory_string comparisons used here temporarily

  while ( current.native_directory_string() 
          != search_root.native_directory_string() &&
          current.has_branch_path() && 
          current.has_leaf() && 
          !fs::exists(check))
    {
      L(F("'%s' not found in '%s' with '%s' removed\n")
        % bookdir.string() % current.string() % removed.string());
      removed = mkpath(current.leaf()) / removed;
      current = current.branch_path();
      check = current / bookdir;
    }

  L(F("search for '%s' ended at '%s' with '%s' removed\n") 
    % book_keeping_dir % current.string() % removed.string());

  if (!fs::exists(check))
    {
      L(F("'%s' does not exist\n") % check.string());
      return false;
    }

  if (!fs::is_directory(check))
    {
      L(F("'%s' is not a directory\n") % check.string());
      return false;
    }

  // check for MT/. and MT/.. to see if mt dir is readable
  if (!fs::exists(check / ".") || !fs::exists(check / ".."))
    {
      L(F("problems with '%s' (missing '.' or '..')\n") % check.string());
      return false;
    }

  working_copy_root = current;
  working_copy_restriction = removed;

  return true;
}

fs::path 
mkpath(string const & s)
{
  fs::path p(s, fs::native);
  return p;
}

string 
get_homedir()
{
#ifdef WIN32
  // Windows is fun!
  // See thread on monotone-devel:
  //   Message-Id: <20050221.182951.104117563.dalcolmo@vh-s.de>
  //   URL: http://lists.gnu.org/archive/html/monotone-devel/2005-02/msg00241.html
  char * home;
  L(F("Searching for home directory\n"));
  // First try MONOTONE_HOME, to give people a way out in case the cruft below
  // doesn't work for them.
  home = getenv("MONOTONE_HOME");
  if (home != NULL)
    {
      L(F("Home directory from MONOTONE_HOME\n"));
      return string(home);
    }
  // If running under cygwin or mingw, try HOME next:
  home = getenv("HOME");
  char * ostype = getenv("OSTYPE");
  if (home != NULL
      && ostype != NULL
      && (string(ostype) == "cygwin" || string(ostype) == "msys"))
    {
      L(F("Home directory from HOME\n"));
      return string(home);
    }
  // Otherwise, try USERPROFILE:
  home = getenv("USERPROFILE");
  if (home != NULL)
    {
      L(F("Home directory from USERPROFILE\n"));
      return string(home);
    }
  // Finally, if even that doesn't work (old version of Windows, I think?),
  // try the HOMEDRIVE/HOMEPATH combo:
  char * homedrive = getenv("HOMEDRIVE");
  char * homepath = getenv("HOMEPATH");
  if (homedrive != NULL && homepath != NULL)
    {
      L(F("Home directory from HOMEDRIVE+HOMEPATH\n"));
      return string(homedrive) + string(homepath);
    }
  // And if things _still_ didn't work, give up.
  N(false, F("could not find home directory (tried MONOTONE_HOME, HOME (if "
              "cygwin/mingw), USERPROFILE, HOMEDRIVE/HOMEPATH"));
#else
  char * home = getenv("HOME");
  if (home != NULL)
    return string(home);

  struct passwd * pw = getpwuid(getuid());  
  N(pw != NULL, F("could not find home directory for uid %d") % getuid());
  return string(pw->pw_dir);
#endif
}

string 
absolutify(string const & path)
{
  fs::path tmp = mkpath(path);
  if (! tmp.has_root_path())
    tmp = fs::current_path() / tmp;
  I(tmp.has_root_path());
  tmp = tmp.normalize();
  return tmp.string();
}

string
absolutify_for_command_line(string const & path)
{
  if (path == "-")
    return path;
  else
    return absolutify(path);
}

string 
tilde_expand(string const & path)
{
  fs::path tmp = mkpath(path);
  fs::path::iterator i = tmp.begin();
  if (i != tmp.end())
    {
      fs::path res;
      if (*i == "~")
        {
          res /= mkpath(get_homedir());
          ++i;
        }
      else if (i->size() > 1 && i->at(0) == '~')
        {
#ifdef WIN32
          res /= mkpath(get_homedir());
#else
          struct passwd * pw;
          pw = getpwnam(i->substr(1).c_str());
          N(pw != NULL, F("could not find home directory for user %s") % i->substr(1));
          res /= mkpath(string(pw->pw_dir));
#endif
          ++i;
        }
      while (i != tmp.end())
        res /= mkpath(*i++);
      return res.string();
    }

  return tmp.string();
}

static bool 
book_keeping_file(fs::path const & p)
{
  return *(p.begin()) == book_keeping_dir;
}

bool 
book_keeping_file(local_path const & p)
{
  if (p() == book_keeping_dir) return true;
  if (*(mkpath(p()).begin()) == book_keeping_dir) return true;
  return false;
}

bool 
directory_exists(local_path const & p) 
{ 
  return fs::exists(localized(p)) &&
    fs::is_directory(localized(p)); 
}

bool 
directory_exists(file_path const & p) 
{ 
  return fs::exists(localized(p)) &&
    fs::is_directory(localized(p)); 
}

bool 
file_exists(file_path const & p) 
{ 
  return fs::exists(localized(p)); 
}

bool 
file_exists(local_path const & p) 
{ 
  return fs::exists(localized(p)); 
}

void 
delete_file(local_path const & p) 
{ 
  N(file_exists(p), 
    F("file to delete '%s' does not exist") % p);
  fs::remove(localized(p)); 
}

void 
delete_file(file_path const & p) 
{ 
  N(file_exists(p), 
    F("file to delete '%s' does not exist") % p);
  fs::remove(localized(p)); 
}

void 
delete_dir_recursive(file_path const & p) 
{ 
  N(directory_exists(p), 
    F("directory to delete '%s' does not exist") % p);
  fs::remove_all(localized(p)); 
}

void 
delete_dir_recursive(local_path const & p) 
{ 
  N(directory_exists(p), 
    F("directory to delete '%s' does not exist") % p);
  fs::remove_all(localized(p)); 
}

void 
move_file(file_path const & old_path,
          file_path const & new_path) 
{ 
  N(file_exists(old_path), 
    F("rename source file '%s' does not exist") % old_path);
  N(! file_exists(new_path), 
    F("rename target file '%s' already exists") % new_path);
  fs::rename(localized(old_path), 
             localized(new_path));
}

void 
move_file(local_path const & old_path,
          local_path const & new_path) 
{ 
  N(file_exists(old_path), 
    F("rename source file '%s' does not exist") % old_path);
  N(! file_exists(new_path), 
    F("rename target file '%s' already exists") % new_path);
  fs::rename(localized(old_path), 
             localized(new_path));
}

void 
move_dir(file_path const & old_path,
         file_path const & new_path) 
{ 
  N(directory_exists(old_path), 
    F("rename source dir '%s' does not exist") % old_path);
  N(!directory_exists(new_path), 
    F("rename target dir '%s' already exists") % new_path);
  fs::rename(localized(old_path), 
             localized(new_path));
}

void 
move_dir(local_path const & old_path,
         local_path const & new_path) 
{ 
  N(directory_exists(old_path), 
    F("rename source dir '%s' does not exist") % old_path);
  N(!directory_exists(new_path), 
    F("rename target dir '%s' already exists") % new_path);
  fs::rename(localized(old_path), 
             localized(new_path));
}

void 
mkdir_p(local_path const & p) 
{ 
  fs::create_directories(localized(p)); 
}

void 
mkdir_p(file_path const & p) 
{ 
  fs::create_directories(localized(p)); 
}

void 
make_dir_for(file_path const & p) 
{ 
  fs::path tmp = localized(p);
  if (tmp.has_branch_path())
    {
      fs::create_directories(tmp.branch_path()); 
    }
}

static void 
read_data_impl(fs::path const & p,
               data & dat)
{
  if (!fs::exists(p))
    throw oops("file '" + p.string() + "' does not exist");
  
  if (fs::is_directory(p))
    throw oops("file '" + p.string() + "' cannot be read as data; it is a directory");
  
  ifstream file(p.string().c_str(),
                ios_base::in | ios_base::binary);
  string in;
  if (!file)
    throw oops(string("cannot open file ") + p.string() + " for reading");
  CryptoPP::FileSource f(file, true, new CryptoPP::StringSink(in));
  dat = in;
}

// This function can only be called once per run.
static void
read_data_stdin(data & dat)
{
  static bool have_consumed_stdin = false;
  N(!have_consumed_stdin, F("Cannot read standard input multiple times"));
  have_consumed_stdin = true;
  string in;
  CryptoPP::FileSource f(cin, true, new CryptoPP::StringSink(in));
  dat = in;
}

void 
read_data(local_path const & path, data & dat)
{ 
  read_data_impl(localized(path), dat); 
}

void 
read_data(file_path const & path, data & dat)
{ 
  read_data_impl(localized(path), dat); 
}

void 
read_data(local_path const & path,
          base64< gzip<data> > & dat)
{
  data data_plain;
  read_data_impl(localized(path), data_plain);
  gzip<data> data_compressed;
  base64< gzip<data> > data_encoded;  
  encode_gzip(data_plain, data_compressed);
  encode_base64(data_compressed, dat);
}

void 
read_data(file_path const & path, 
          base64< gzip<data> > & dat)
{ 
  read_data(local_path(path()), dat); 
}

void 
read_localized_data(file_path const & path,
                    base64< gzip<data> > & dat,
                    lua_hooks & lua)
{
  data data_plain;
  read_localized_data(path, data_plain, lua);
  gzip<data> data_compressed;
  base64< gzip<data> > data_encoded;  
  encode_gzip(data_plain, data_compressed);
  encode_base64(data_compressed, dat);
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


void
read_data_for_command_line(utf8 const & path, data & dat)
{
  if (path() == "-")
    read_data_stdin(dat);
  else
    {
      N(fs::exists(localized(path)), F("file '%s' does not exist") % path);
      read_data_impl(localized(path), dat);
    }
}


// FIXME: this is probably not enough brains to actually manage "atomic
// filesystem writes". at some point you have to draw the line with even
// trying, and I'm not sure it's really a strict requirement of this tool,
// but you might want to make this code a bit tighter.


static void 
write_data_impl(fs::path const & p,
                data const & dat)
{  
  if (fs::exists(p) && fs::is_directory(p))
    throw oops("file '" + p.string() + "' cannot be over-written as data; it is a directory");

  fs::create_directories(mkpath(p.branch_path().string()));
  
  // we write, non-atomically, to MT/data.tmp.
  // nb: no mucking around with multiple-writer conditions. we're a
  // single-user single-threaded program. you get what you paid for.
  fs::path mtdir = mkpath(book_keeping_dir);
  fs::create_directories(mtdir);
  fs::path tmp = mtdir / "data.tmp"; 

  {
    // data.tmp opens
    ofstream file(tmp.string().c_str(),
                  ios_base::out | ios_base::trunc | ios_base::binary);
    if (!file)
      throw oops(string("cannot open file ") + tmp.string() + " for writing");    
    CryptoPP::StringSource s(dat(), true, new CryptoPP::FileSink(file));
    // data.tmp closes
  }

  if (fs::exists(p))
    N(fs::remove(p),
      F("removing %s failed") % p.string());
  fs::rename(tmp, p);
}

void 
write_data(local_path const & path, data const & dat)
{ 
  write_data_impl(localized(path), dat); 
}

void 
write_data(file_path const & path, data const & dat)
{ 
  write_data_impl(localized(path), dat); 
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
write_localized_data(file_path const & path,
                     base64< gzip<data> > const & dat,
                     lua_hooks & lua)
{
  gzip<data> data_decoded;
  data data_decompressed;      
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);
  write_localized_data(path, data_decompressed, lua);
}

void 
write_data(local_path const & path,
           base64< gzip<data> > const & dat)
{
  gzip<data> data_decoded;
  data data_decompressed;      
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);      
  write_data_impl(localized(path), data_decompressed);
}

void 
write_data(file_path const & path,
           base64< gzip<data> > const & dat)
{ 
  write_data(local_path(path()), dat); 
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
      fs::path entry = mkpath(di->string());
      fs::path rel_entry = relative / mkpath(entry.leaf());
      
      if (book_keeping_file(rel_entry))
        {
          L(F("ignoring book keeping entry %s\n") % rel_entry.string());
          continue;
        }
      
      if (!fs::exists(entry) 
          || di->string() == "." 
          || di->string() == "..") 
        ;                       // ignore
      else if (fs::is_directory(entry))
        walk_tree_recursive(entry, rel_entry, walker);
      else
        {
          file_path p;
          try 
            {
              p = file_path(rel_entry.string());
            }
          catch (std::runtime_error const & c)
            {
              L(F("caught runtime error %s constructing file path for %s\n") 
                % c.what() % rel_entry.string());
              continue;
            }     
          walker.visit_file(p);
        }
    }
}

// from some (safe) sub-entry of cwd
void 
walk_tree(file_path const & path,
          tree_walker & walker,
          bool require_existing_path)
{
  if (fs::exists(localized(path)))
    {
      if (! fs::is_directory(localized(path)))
        walker.visit_file(path);
      else
        {
          // the current path does not need localization
          fs::path root(fs::current_path());
          fs::path rel(localized(path));
          walk_tree_recursive(root / rel, rel, walker);
        }
    }
  else
    {
      if (require_existing_path)
        {
          N(false,
            F("no such file or directory: %s") % path());
        }
      else
        {
          walker.visit_file(path);
        }
    }
}

// from cwd (nb: we can't describe cwd as a file_path)
void 
walk_tree(tree_walker & walker)
{
  walk_tree_recursive(fs::current_path(), fs::path(), walker);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void 
test_book_keeping_file()
{
  // positive tests

  BOOST_CHECK(book_keeping_file(local_path("MT")));
  BOOST_CHECK(book_keeping_file(local_path("MT/foo")));
  BOOST_CHECK(book_keeping_file(local_path("MT/foo/bar/baz")));

  BOOST_CHECK(book_keeping_file(fs::path("MT")));
  BOOST_CHECK(book_keeping_file(fs::path("MT/foo")));
  BOOST_CHECK(book_keeping_file(fs::path("MT/foo/bar/baz")));

  // negative tests

  BOOST_CHECK( ! book_keeping_file(local_path("safe")));
  BOOST_CHECK( ! book_keeping_file(local_path("safe/path")));
  BOOST_CHECK( ! book_keeping_file(local_path("safe/path/MT")));
  BOOST_CHECK( ! book_keeping_file(local_path("MTT")));

  BOOST_CHECK( ! book_keeping_file(fs::path("safe")));
  BOOST_CHECK( ! book_keeping_file(fs::path("safe/path")));
  BOOST_CHECK( ! book_keeping_file(fs::path("safe/path/MT")));
  BOOST_CHECK( ! book_keeping_file(fs::path("MTT")));
}

void 
add_file_io_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_book_keeping_file));
}

#endif // BUILD_UNIT_TESTS
