// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/regex.hpp>
#include <sstream>

#include "app_state.hh"
#include "file_io.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "work.hh"

// working copy / book-keeping file code

using namespace boost;
using namespace std;

string const work_file_name("work");

class 
addition_builder 
  : public tree_walker
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

void 
addition_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    {
      P(F("skipping book-keeping file %s\n") % path);
      return;
    }

  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }

  N(work.renames.find(path) == work.renames.end(),
    F("adding file %s, also scheduled for source of rename") % path);

  for (rename_set::const_iterator i = work.renames.begin();
       i != work.renames.end(); ++i)
    N(!(path == i->second),
      F("adding file %s, also scheduled for target of rename") % path);

  if (work.adds.find(path) != work.adds.end())
    {
      P(F("skipping %s, already present in working copy add set\n") % path);
      return;
    }
  
  if (work.dels.find(path) != work.dels.end())
    {
      P(F("removing %s from working copy delete set\n") % path);
      work.dels.erase(path);
      rewrite_work = true;
    }
  else if (man.find(path) != man.end())
    {
      P(F("skipping %s, already present in manifest\n") % path);
      return;
    }
  else
    {
      P(F("adding %s to working copy add set\n") % path);
      work.adds.insert(path);
      rewrite_work = true;
    }
}

void 
build_addition(file_path const & path,
	       app_state & app,
	       work_set & work,
	       manifest_map & man,
	       bool & rewrite_work)
{
  addition_builder build(app, work, man, rewrite_work);
  walk_tree(path, build);
}



class 
deletion_builder 
  : public tree_walker
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

void 
deletion_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    {
      P(F("skipping book-keeping file %s\n") % path);
      return;
    }

  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }

  N(work.renames.find(path) == work.renames.end(),
    F("deleting file %s, also scheduled for source of rename") % path);

  for (rename_set::const_iterator i = work.renames.begin();
       i != work.renames.end(); ++i)
    N(!(path == i->second),
      F("deleting file %s, also scheduled for target of rename") % path);

  if (work.dels.find(path) != work.dels.end())
    {
      P(F("skipping %s, already present in working copy delete set\n") % path);
      return;
    }
  
  if (work.adds.find(path) != work.adds.end())
    {
      P(F("removing %s from working copy add set\n") % path);
      work.adds.erase(path);
      rewrite_work = true;
    }
  else if (man.find(path) == man.end())
    {
      P(F("skipping %s, does not exist in manifest\n") % path);
      return;
    }
  else
    {
      P(F("adding %s to working copy delete set\n") % path);
      work.dels.insert(path);
      rewrite_work = true;
    }
}

void 
build_deletion(file_path const & path,
	       app_state & app,
	       work_set & work,
	       manifest_map & man,
	       bool & rewrite_work)
{
  deletion_builder build(app, work, man, rewrite_work);
  walk_tree(path, build);
}


class 
rename_builder 
  : public tree_walker
{
  file_path const & src;
  file_path const & dst;
  app_state & app;
  work_set & work;
  manifest_map & man;
  bool & rewrite_work;
public:
  rename_builder(file_path const & s,
		 file_path const & d,
		 app_state & a, 
		 work_set & w, 
		 manifest_map & m, 
		 bool & rw) : 
    src(s), dst(d),
    app(a), work(w), man(m), rewrite_work(rw)
  {}
  file_path pathsub(file_path const & in);
  virtual void visit_file(file_path const & path);
};


file_path 
rename_builder::pathsub(file_path const & in)
{
  fs::path sp = mkpath(src());
  fs::path dp = mkpath(dst());
  fs::path ip = mkpath(in());
  
  fs::path::iterator i = ip.begin();
  for (fs::path::iterator s = sp.begin();
       s != sp.end(); ++s, ++i)
    I(i != ip.end());

  fs::path res = dp;

  while (i != ip.end())
    res /= *i++;
  
  return file_path(res.string());
}


void 
rename_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    {
      P(F("skipping book-keeping file %s\n") % path);
      return;
    }

  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }

  N(work.dels.find(path) == work.dels.end(),
    F("moving file %s, also scheduled for deletion") % path);

  N(work.adds.find(path) == work.adds.end(),
    F("moving file %s, also scheduled for addition") % path);

  for (rename_set::const_iterator i = work.renames.begin();
       i != work.renames.end(); ++i)
    N(!(path == i->second),
      F("renaming file %s, existing target of rename") % path);

  if (work.renames.find(path) != work.renames.end())
    {
      P(F("skipping %s, already present in working copy rename set\n") % path);
      return;
    }

  if (man.find(path) == man.end())
    {
      P(F("skipping %s, does not exist in manifest\n") % path);
      return;
    }

  file_path targ = pathsub(path);
  P(F("adding %s -> %s to working copy rename set\n") % path % targ);
  work.renames.insert(make_pair(path, targ));
  rewrite_work = true;
}


void 
build_rename(file_path const & src,
	     file_path const & dst,
	     app_state & app,
	     work_set & work,
	     manifest_map & man,
	     bool & rewrite_work)
{
  rename_builder build(src, dst, app, work, man, rewrite_work);
  walk_tree(src, build);
}


struct 
add_to_work_set
{    
  work_set & work;
  explicit add_to_work_set(work_set & w) : work(w) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    std::string action(res[1].first, res[1].second);
    I(res.size() == 3 || res.size() == 4);
    file_path path = file_path(std::string(res[2].first, res[2].second));
    I(work.adds.find(path) == work.adds.end());
    I(work.dels.find(path) == work.dels.end());
    I(work.renames.find(path) == work.renames.end());
    
    if (action == "add")
      {
	N(res.size() == 3, F("extra junk on work entry for add of %s") % path);
	work.adds.insert(path);
      }
    else if (action == "drop")
      {
	N(res.size() == 3, F("extra junk on work entry for drop of %s") % path);
	work.dels.insert(path);
      }
    else if (action == "rename")
      {
	N(res.size() == 4, F("missing rename target for %s in work set") % path);
	file_path dst = file_path(std::string(res[3].first, res[3].second));

	for (path_set::const_iterator a = work.adds.begin();
	     a != work.adds.end(); ++a)
	  I(!(*a == dst));

	for (path_set::const_iterator d = work.dels.begin();
	     d != work.dels.end(); ++d)
	  I(!(*d == dst));

	for (rename_set::const_iterator r = work.renames.begin();
	     r != work.renames.end(); ++r)
	  I(!(r->second == dst));
	    
	work.renames.insert(make_pair(path, dst));
      }
    else 
      throw oops("unknown action in work set: " + action);

    return true;
  }
};

void 
read_work_set(data const & dat,
	      work_set & work)
{
  regex expr("^(add|drop)\n ([^[:space:]].+)$");
  regex_grep(add_to_work_set(work), dat(), expr, match_not_dot_newline);    
  regex expr2("^(rename)\n ([^[:space:]].+)\n ([^[:space:]].+)$");
  regex_grep(add_to_work_set(work), dat(), expr2, match_not_dot_newline);
}

void 
write_work_set(data & dat,
	       work_set const & work)
{
  ostringstream tmp;
  for (path_set::const_iterator i = work.dels.begin();
       i != work.dels.end(); ++i)
    tmp << "drop\n " << (*i) << endl;

  for (path_set::const_iterator i = work.adds.begin();
       i != work.adds.end(); ++i)
    tmp << "add\n " << (*i) << endl;

  for (rename_set::const_iterator i = work.renames.begin();
       i != work.renames.end(); ++i)
    tmp << "rename\n " << i->first << "\n " << i->second << endl;

  dat = tmp.str();
}

void 
extract_path_set(manifest_map const & man,
		 path_set & paths)
{
  paths.clear();
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    paths.insert(path_id_pair(*i).path());
}

void 
apply_work_set(work_set const & work,
	       path_set & paths)
{
  for (path_set::const_iterator i = work.dels.begin();
       i != work.dels.end(); ++i)
    {
      I(paths.find(*i) != paths.end());
      paths.erase(*i);
    }

  for (path_set::const_iterator i = work.adds.begin();
       i != work.adds.end(); ++i)
    {
      I(paths.find(*i) == paths.end());
      paths.insert(*i);
    }

  for (rename_set::const_iterator i = work.renames.begin();
       i != work.renames.end(); ++i)
    {
      I(paths.find(i->first) != paths.end());
      I(paths.find(i->second) == paths.end());
      paths.erase(i->first);
      paths.insert(i->second);
    }

}

// options map file

string const options_file_name("options");

struct 
add_to_options_map
{
  options_map & options;
  explicit add_to_options_map(options_map & m): options(m) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    utf8 value;
    std::string key(res[1].first, res[1].second);
    value = std::string(res[2].first, res[2].second);
    options[key] = value;
    return true;
  }
};

void 
get_options_path(local_path & o_path)
{
  o_path = (mkpath(book_keeping_dir) / mkpath(options_file_name)).string();
  L(F("options path is %s\n") % o_path);
}

void 
read_options_map(data const & dat, options_map & options)
{
  regex expr("^([^[:space:]]+)[[:blank:]]+([^[:space:]]+)$");
  regex_grep(add_to_options_map(options), dat(), expr, match_not_dot_newline);
}

void 
write_options_map(data & dat, options_map const & options)
{
  ostringstream tmp;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    tmp << i->first << " " << i->second << endl;
  dat = tmp.str();
}


// attribute map file

string const attr_file_name(".mt-attrs");

struct 
add_to_attr_map
{
  attr_map & attr;
  explicit add_to_attr_map(attr_map & m): attr(m) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    std::string key(res[1].first, res[1].second);
    std::string value(res[2].first, res[2].second);
    std::string file(res[3].first, res[3].second);
    attr[make_pair(file_path(file), key)] = value;
    return true;
  }
};

void 
get_attr_path(file_path & a_path)
{
  a_path = (mkpath(attr_file_name)).string();
  L(F("attribute map path is %s\n") % a_path);
}

void 
read_attr_map(data const & dat, attr_map & attr)
{
  regex expr("^([^[:space:]]+) ([^[:space:]]+) ([^[:space:]].+)$");
  regex_grep(add_to_attr_map(attr), dat(), expr, match_not_dot_newline);
}

void 
write_attr_map(data & dat, attr_map const & attr)
{
  ostringstream tmp;
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
    tmp << i->first.second << " "    // key
	<< i->second << " "          // value
	<< i->first.first << endl;   // file
  dat = tmp.str();
}


void 
apply_attributes(app_state & app, attr_map const & attr)
{
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
    app.lua.hook_apply_attribute (i->first.second, 
				  i->first.first,
				  i->second);
}
