// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/regex.hpp>
#include <sstream>

#include "app_state.hh"
#include "work.hh"
#include "file_io.hh"
#include "sanity.hh"

// working copy / book-keeping file code

string const work_file_name("work");

using namespace boost;


class addition_builder : public tree_walker
{
  app_state & app;
  work_set & work;
  manifest_map & man;
  bool & rewrite_work;
public:
  addition_builder(app_state & a, 
		   work_set & w, 
		   manifest_map & m, 
		   bool & rw) : 
    app(a), work(w), man(m), rewrite_work(rw)
  {};
  virtual void visit_file(file_path const & path);
};

void addition_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    {
      P("skipping book-keeping file %s\n", path().c_str());
      return;
    }

  if (app.lua.hook_ignore_file(path))
    {
      P("skipping ignorable file %s\n", path().c_str());
      return;
    }

  if (work.adds.find(path) != work.adds.end())
    {
      P("skipping %s, already present in working copy add set\n", path().c_str());
      return;
    }
  
  if (work.dels.find(path) != work.dels.end())
    {
      P("removing %s from working copy delete set\n", path().c_str());
      work.dels.erase(path);
      rewrite_work = true;
    }
  else if (man.find(path) != man.end())
    {
      P("skipping %s, already present in manifest\n", path().c_str());
      return;
    }
  else
    {
      P("adding %s to working copy add set\n", path().c_str());
      work.adds.insert(path);
      rewrite_work = true;
    }
}

void build_addition(file_path const & path,
		    app_state & app,
		    work_set & work,
		    manifest_map & man,
 		    bool & rewrite_work)
{
  addition_builder build(app, work, man, rewrite_work);
  walk_tree(path, build);
}



class deletion_builder : public tree_walker
{
  app_state & app;
  work_set & work;
  manifest_map & man;
  bool & rewrite_work;
public:
  deletion_builder(app_state & a, 
		   work_set & w, 
		   manifest_map & m, 
		   bool & rw) : 
    app(a), work(w), man(m), rewrite_work(rw)
  {};
  virtual void visit_file(file_path const & path);
};

void deletion_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    {
      P("skipping book-keeping file %s\n", path().c_str());
      return;
    }

  if (app.lua.hook_ignore_file(path))
    {
      P("skipping ignorable file %s\n", path().c_str());
      return;
    }

  if (work.dels.find(path) != work.dels.end())
    {
      P("skipping %s, already present in working copy delete set\n", path().c_str());
      return;
    }
  
  if (work.adds.find(path) != work.adds.end())
    {
      P("removing %s from working copy add set\n", path().c_str());
      work.adds.erase(path);
      rewrite_work = true;
    }
  else if (man.find(path) == man.end())
    {
      P("skipping %s, does not exist in manifest\n", path().c_str());
      return;
    }
  else
    {
      P("adding %s to working copy delete set\n", path().c_str());
      work.dels.insert(path);
      rewrite_work = true;
    }
}

void build_deletion(file_path const & path,
		    app_state & app,
		    work_set & work,
		    manifest_map & man,
 		    bool & rewrite_work)
{
  deletion_builder build(app, work, man, rewrite_work);
  walk_tree(path, build);
}


struct add_to_work_set
{    
  work_set & work;
  explicit add_to_work_set(work_set & w) : work(w) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    std::string adddel(res[1].first, res[1].second);
    std::string path(res[2].first, res[2].second);
    if (!book_keeping_file(path))
      {
	if (adddel == "-")
	  work.dels.insert(path);
	else if (adddel == "+")
	  work.adds.insert(path);
	else throw oops("unknown add/del character in work set: " + adddel);
      } else {
	throw oops("book-keeping filename: " + path);
      }
    return true;
  }
};

void read_work_set(data const & dat,
		   work_set & work)
{
  regex expr("^[[:space:]]*([\\+\\-])[[:space:]]+([^[:space:]]+)");
  regex_grep(add_to_work_set(work), dat(), expr, match_not_dot_newline);    
}

void write_work_set(data & dat,
		    work_set const & work)
{
  ostringstream tmp;
  for (path_set::const_iterator i = work.dels.begin();
       i != work.dels.end(); ++i)
    tmp << "- " << (*i)() << endl;
  for (path_set::const_iterator i = work.adds.begin();
       i != work.adds.end(); ++i)
    tmp << "+ " << (*i)() << endl;
  dat = tmp.str();
}

void extract_path_set(manifest_map const & man,
		      path_set & paths)
{
  paths.clear();
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    paths.insert(path_id_pair(*i).path());
}

void apply_work_set(work_set const & work,
		    path_set & paths)
{
  for (path_set::const_iterator i = work.dels.begin();
       i != work.dels.end(); ++i)
    paths.erase(*i);
  for (path_set::const_iterator i = work.adds.begin();
       i != work.adds.end(); ++i)
    paths.insert(*i);
}

// read options file

string const options_file_name("options");

struct add_to_options_map
{
  options_map & options;
  explicit add_to_options_map(options_map & m): options(m) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    std::string key(res[1].first, res[1].second);
    std::string value(res[2].first, res[2].second);
    options[key] = value;
    return true;
  }
};

void get_options_path(local_path & o_path)
{
  o_path = (fs::path(book_keeping_dir) / fs::path(options_file_name)).string();
  L("options path is %s\n", o_path().c_str());
}

void read_options_map(data const & dat, options_map & options)
{
  regex expr("^([^[:space:]]+)[[:blank:]]+([^[:space:]]+)$");
  regex_grep(add_to_options_map(options), dat(), expr, match_not_dot_newline);
}

void write_options_map(data & dat, options_map const & options)
{
  ostringstream tmp;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    tmp << i->first << " " << i->second << endl;
  dat = tmp.str();
}
