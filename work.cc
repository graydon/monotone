// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/regex.hpp>
#include <sstream>

#include "app_state.hh"
#include "basic_io.hh"
#include "change_set.hh"
#include "file_io.hh"
#include "sanity.hh"
#include "transforms.hh"
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
  change_set::path_rearrangement & pr;
  path_set ps;
public:
  addition_builder(app_state & a, 
                   change_set::path_rearrangement & pr,
                   path_set & p)
    : app(a), pr(pr), ps(p)
  {}
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

  if (ps.find(path) != ps.end())
    {
      P(F("skipping %s, already accounted for in working copy\n") % path);
      return;
    }

  P(F("adding %s to working copy add set\n") % path);
  ps.insert(path);
  pr.added_files.insert(path);
}

void 
build_addition(file_path const & path,
               manifest_map const & man,
               app_state & app,
               change_set::path_rearrangement & pr)
{
  N(directory_exists(path) || file_exists(path),
    F("path %s does not exist\n") % path);

  change_set::path_rearrangement pr_new, pr_concatenated;
  change_set cs_new;

  path_set ps;
  extract_path_set(man, ps);
  apply_path_rearrangement(pr, ps);    

  addition_builder build(app, pr_new, ps);
  walk_tree(path, build);

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
}

static bool
known_preimage_path(file_path const & p,
                    manifest_map const & m,
                    change_set::path_rearrangement const & pr,
                    bool & path_is_directory)
{
  path_set ps;
  extract_path_set(m, ps);
  apply_path_rearrangement(pr, ps);    

  std::string path_as_dir = p() + "/";
  for (path_set::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      if (*i == p) 
        {
          path_is_directory = false;
          return true;
        }
      else if ((*i)().find(path_as_dir) == 0)
        {
          path_is_directory = true;
          return true;
        }
    }
  return false;
}

void 
build_deletion(file_path const & path,
               manifest_map const & man,
               change_set::path_rearrangement & pr)
{
  change_set::path_rearrangement pr_new, pr_concatenated;

  bool dir_p = false;
  
  if (! known_preimage_path(path, man, pr, dir_p))
    {
      P(F("skipping %s, not currently tracked\n") % path);
      return;
    }

  P(F("adding %s to working copy delete set\n") % path);

  if (dir_p) 
    pr_new.deleted_dirs.insert(path);
  else 
    pr_new.deleted_files.insert(path);
  
  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
}

void 
build_rename(file_path const & src,
             file_path const & dst,
             manifest_map const & man,
             change_set::path_rearrangement & pr)
{
  change_set::path_rearrangement pr_new, pr_concatenated;

  bool dir_p = false;

  if (! known_preimage_path(src, man, pr, dir_p))
    {
      P(F("skipping %s, not currently tracked\n") % src);
      return;
    }

  P(F("adding %s -> %s to working copy rename set\n") % src % dst);
  if (dir_p)
    pr_new.renamed_dirs.insert(std::make_pair(src, dst));
  else 
    pr_new.renamed_files.insert(std::make_pair(src, dst));

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
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


void 
get_options_path(local_path & o_path)
{
  o_path = (mkpath(book_keeping_dir) / mkpath(options_file_name)).string();
  L(F("options path is %s\n") % o_path);
}

void 
read_options_map(data const & dat, options_map & options)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "MT/options");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  // don't clear the options which will have settings from the command line
  // options.clear(); 

  std::string opt, val;
  while (parser.symp())
    {
      parser.sym(opt);
      parser.str(val);
      // options[opt] = val;      
      // use non-replacing insert verses replacing with options[opt] = val;
      options.insert(make_pair(opt, val)); 
    }
}

void 
write_options_map(data & dat, options_map const & options)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);

  basic_io::stanza st;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    st.push_str_pair(i->first, i->second());

  pr.print_stanza(st);
  dat = oss.str();
}


// attribute map file

string const attr_file_name(".mt-attrs");

void 
get_attr_path(file_path & a_path)
{
  a_path = (mkpath(attr_file_name)).string();
  L(F("attribute map path is %s\n") % a_path);
}

namespace
{
  namespace syms
  {
    std::string const file("file");
  }
}

void 
read_attr_map(data const & dat, attr_map & attr)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, ".mt-attrs");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  std::string file, name, value;

  attr.clear();

  while (parser.symp(syms::file))
    {
      parser.sym();
      parser.str(file);
      file_path fp(file);

      while (parser.symp() && 
	     !parser.symp(syms::file)) 
	{
	  parser.sym(name);
	  parser.str(value);
	  attr[fp][name] = value;
	}
    }
}

void 
write_attr_map(data & dat, attr_map const & attr)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::file, i->first());

      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
	   j != i->second.end(); ++j)
	  st.push_str_pair(j->first, j->second);	  

      pr.print_stanza(st);
    }

  dat = oss.str();
}


void 
apply_attributes(app_state & app, attr_map const & attr)
{
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
	   j != i->second.end(); ++j)
	app.lua.hook_apply_attribute (j->first,
				      i->first, 
				      j->second);
}

string const encoding_attribute("encoding");
string const binary_encoding("binary");
string const default_encoding("default");

static bool find_in_attr_map(attr_map const & attr,
			     file_path const & file,
			     std::string const & attr_key,
			     std::string & attr_val)
{
  attr_map::const_iterator f = attr.find(file);
  if (f == attr.end())
    return false;

  std::map<std::string, std::string>::const_iterator a = f->second.find(attr_key);
  if (a == f->second.end())
    return false;

  attr_val = a->second;
  return true;
}

bool get_attribute_from_db(file_path const & file,
			   std::string const & attr_key,
			   manifest_map const & man,
			   std::string & attr_val,
			   app_state & app)
{
  file_path fp;
  get_attr_path(fp);
  manifest_map::const_iterator i = man.find(fp);
  if (i == man.end())
    return false;

  file_id fid = manifest_entry_id(i);
  if (!app.db.file_version_exists(fid))
    return false;

  file_data attr_data;
  app.db.get_file_version(fid, attr_data);

  attr_map attr;
  read_attr_map(data(attr_data.inner()()), attr);

  return find_in_attr_map(attr, file, attr_key, attr_val);
}

bool get_attribute_from_working_copy(file_path const & file,
				     std::string const & attr_key,
				     std::string & attr_val)
{
  file_path fp;
  get_attr_path(fp);
  if (!file_exists(fp))
    return false;
  
  data attr_data;
  read_data(fp, attr_data);

  attr_map attr;
  read_attr_map(attr_data, attr);

  return find_in_attr_map(attr, file, attr_key, attr_val);
}
