// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdio.h>             // for rename(2)

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "cryptopp/filters.h"
#include "cryptopp/files.h"

#include "transforms.hh"
#include "file_io.hh"
#include "sanity.hh"

// this file deals with talking to the filesystem, loading and
// saving files.

using namespace std;

string const book_keeping_dir("MT");

bool book_keeping_file(local_path const & p)
{
  if (p() == book_keeping_dir) return true;
  if (*(fs::path(p()).begin()) == book_keeping_dir) return true;
  return false;
}

bool directory_exists(local_path const & p) 
{ 
  return fs::exists(fs::path(p())) &&
    fs::is_directory(fs::path(p())); 
}
bool file_exists(file_path const & p) { return fs::exists(fs::path(p())); }
bool file_exists(local_path const & p) { return fs::exists(fs::path(p())); }

void delete_file(local_path const & p) { fs::remove(fs::path(p())); }
void delete_file(file_path const & p) { fs::remove(fs::path(p())); }

void move_file(file_path const & old_path,
	       file_path const & new_path) 
{ 
  fs::rename(fs::path(old_path()), 
	     fs::path(new_path()));
}

void mkdir_p(local_path const & p) { fs::create_directories(fs::path(p())); }
void mkdir_p(file_path const & p) { fs::create_directories(fs::path(p())); }


static void read_data_impl(fs::path const & p,
			   data & dat)
{
  if (!fs::exists(p))
    throw oops("file '" + p.string() + "' does not exist");
  
  if (fs::is_directory(p))
    throw oops("file '" + p.string() + "' cannot be read as data; it is a directory");
  
  ifstream file(p.string().c_str());
  string in;
  if (!file)
    throw oops(string("cannot open file ") + p.string() + " for reading");
  CryptoPP::FileSource f(file, true, new CryptoPP::StringSink(in));
  dat = in;
}

void read_data(local_path const & path, data & dat)
{ read_data_impl(fs::path(path()), dat); }

void read_data(file_path const & path, data & dat)
{ read_data_impl(fs::path(path()), dat); }

void read_data(local_path const & path,
	       base64< gzip<data> > & dat)
{
  data data_plain;
  read_data_impl(fs::path(path()), data_plain);
  gzip<data> data_compressed;
  base64< gzip<data> > data_encoded;  
  encode_gzip(data_plain, data_compressed);
  encode_base64(data_compressed, dat);
}

void read_data(file_path const & path, base64< gzip<data> > & dat)
{ read_data(local_path(path()), dat); }

// FIXME: this is probably not enough brains to actually manage "atomic
// filesystem writes". at some point you have to draw the line with even
// trying, and I'm not sure it's really a strict requirement of this tool,
// but you might want to make this code a bit tighter.


static void write_data_impl(fs::path const & p,
			    data const & dat)
{  
  if (fs::exists(p) && fs::is_directory(p))
    throw oops("file '" + p.string() + "' cannot be over-written as data; it is a directory");

  fs::create_directories(p.branch_path().string());
  
  // we write, non-atomically, to MT/data.tmp.
  // nb: no mucking around with multiple-writer conditions. we're a
  // single-user single-threaded program. you get what you paid for.
  fs::path mtdir(book_keeping_dir);
  fs::create_directories(mtdir);
  fs::path tmp = mtdir / "data.tmp"; 

  {
    // data.tmp opens
    ofstream file(tmp.string().c_str());
    if (!file)
      throw oops(string("cannot open file ") + tmp.string() + " for writing");    
    CryptoPP::StringSource s(dat(), true, new CryptoPP::FileSink(file));
    // data.tmp closes
  }

  // god forgive my portability sins
  rename(tmp.string().c_str(), p.string().c_str());
}

void write_data(local_path const & path, data const & dat)
{ write_data_impl(fs::path(path()), dat); }

void write_data(file_path const & path, data const & dat)
{ write_data_impl(fs::path(path()), dat); }


void write_data(local_path const & path,
		base64< gzip<data> > const & dat)
{
  gzip<data> data_decoded;
  data data_decompressed;      
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);      
  write_data_impl(fs::path(path()), data_decompressed);
}

void write_data(file_path const & path,
		base64< gzip<data> > const & dat)
{ write_data(local_path(path()), dat); }


tree_walker::~tree_walker() {}

static void walk_tree_recursive(fs::path const & absolute,
				fs::path const & relative,
				tree_walker & walker)
{
  fs::directory_iterator ei;
  for(fs::directory_iterator di(absolute);
      di != ei; ++di)
    {
      fs::path entry = *di;
      fs::path rel_entry = relative / fs::path(entry.leaf());
      if (fs::is_directory(entry))
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
	      continue;
	    }	  
	  walker.visit_file(p);
	}
    }
}

// from some (safe) sub-entry of cwd
void walk_tree(file_path const & path,
	       tree_walker & walker)
{
  if (! fs::is_directory(fs::path(path())))
    walker.visit_file(path);
  else
    {
      fs::path root(fs::current_path());
      fs::path rel(path());
      walk_tree_recursive(root / rel, rel, walker);
    }
}

// from cwd (nb: we can't describe cwd as a file_path)
void walk_tree(tree_walker & walker)
{
  walk_tree_recursive(fs::current_path(), fs::path(), walker);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void test_book_keeping_file()
{
  // positive tests
  BOOST_CHECK(book_keeping_file(local_path("MT")));
  BOOST_CHECK(book_keeping_file(local_path("MT/foo")));
  BOOST_CHECK(book_keeping_file(local_path("MT/foo/bar/baz")));
  // negative tests
  BOOST_CHECK( ! book_keeping_file(local_path("safe")));
  BOOST_CHECK( ! book_keeping_file(local_path("safe/path")));
  BOOST_CHECK( ! book_keeping_file(local_path("safe/path/MT")));
  BOOST_CHECK( ! book_keeping_file(local_path("MTT")));
}

void add_file_io_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_book_keeping_file));
}

#endif // BUILD_UNIT_TESTS
