// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/regex.hpp>
#include <sstream>

#include "app_state.hh"
#include "change_set.hh"
#include "file_io.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "work.hh"

// working copy / book-keeping file code

using namespace boost;
using namespace std;

std::string const work_file_name("work");

class 
addition_builder 
  : public tree_walker
{
  app_state & app;
  boost::shared_ptr<path_edit_consumer> pc;
public:
  addition_builder(app_state & a, 
		   boost::shared_ptr<path_edit_consumer> pc)
    : app(a), pc(pc)
  {};
  virtual void visit_file(file_path const & path);
};

void 
addition_builder::visit_file(file_path const & path)
{     
  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }
  pc->add_file(path);
}

void 
build_addition(file_path const & path,
	       app_state & app,
	       change_set::path_rearrangement & pr)
{
  boost::shared_ptr<path_edit_consumer> pc = new_rearrangement_builder(pr);

  N(directory_exists(path) || file_exists(path),
    F("path %s does not exist") % path);

  if (directory_exists(path))
    {
      addition_builder build(app, pc);
      walk_tree(path, build);
    }
  else 
    {
      I(file_exists(path));
      pc->add_file(path);
    }
}

void 
build_deletion(file_path const & path,
	       change_set::path_rearrangement & pr)
{
  boost::shared_ptr<path_edit_consumer> pc = new_rearrangement_builder(pr);

  N(directory_exists(path) || file_exists(path),
    F("path %s does not exist") % path);

  if (directory_exists(path))
    pc->delete_dir(path);
  else 
    {
      I(file_exists(path));
      pc->delete_file(path);
    }
}

void 
build_rename(file_path const & src,
	     file_path const & dst,
	     change_set::path_rearrangement & pr)
{
  boost::shared_ptr<path_edit_consumer> pc = new_rearrangement_builder(pr);

  N(directory_exists(src) || file_exists(src),
    F("path %s does not exist") % src);

  if (directory_exists(src))
    pc->rename_dir(src, dst);
  else 
    {
      I(file_exists(src));
      pc->rename_file(src, dst);
    }
}


void 
extract_path_set(manifest_map const & man,
		 path_set & paths)
{
  paths.clear();
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    paths.insert(manifest_entry_path(i));
}

// options map file

string const options_file_name("options");

struct 
add_to_options_map
{
  options_map & options;
  explicit add_to_options_map(options_map & m): options(m) {}
  bool operator()(match_results<std::string::const_iterator> const & res) 
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
  bool operator()(match_results<std::string::const_iterator> const & res) 
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
  regex expr("^([^[:space:]]+) ([^[:space:]]+) ([^[:space:]].*)$");
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
