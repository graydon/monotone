// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include <boost/regex.hpp>

#include "app_state.hh"
#include "file_io.hh"
#include "manifest.hh"
#include "transforms.hh"
#include "sanity.hh"

// this file defines the class of manifest_map objects, and various comparison
// and i/o functions on them. a manifest specifies exactly which versions
// of each file reside at which path location in a given tree.

using namespace boost;
using namespace std;

string const manifest_file_name("manifest");

// building manifest_maps

class manifest_map_builder : public tree_walker
{
  app_state & app;
  manifest_map & man;
public:
  manifest_map_builder(app_state & a, manifest_map & m);
  virtual void visit_file(file_path const & path);
};

manifest_map_builder::manifest_map_builder(app_state & a, manifest_map & m) 
  : app(a), man(m) 
{
}

void manifest_map_builder::visit_file(file_path const & path)
{
      
  if (book_keeping_file(path()))
    return;
  if (app.lua.hook_ignore_file(path))
    return;
  hexenc<id> ident;
  L(F("scanning file %s\n") % path);
  calculate_ident(path, ident);
  man.insert(entry(path, file_id(ident)));
}

void build_manifest_map(file_path const & path,
			app_state & app,
			manifest_map & man)
{
  man.clear();
  manifest_map_builder build(app,man);
  walk_tree(path, build);
}

void build_manifest_map(app_state & app,
			manifest_map & man)
{
  man.clear();
  manifest_map_builder build(app,man);
  walk_tree(build);
}


void build_manifest_map(path_set const & paths,
			manifest_map & man)
{
  man.clear();
  for (path_set::const_iterator i = paths.begin();
       i != paths.end(); ++i)
    {
      hexenc<id> ident;
      calculate_ident(*i, ident);
      man.insert(entry(*i, file_id(ident)));
    }
}

void append_manifest_map(manifest_map const & m1,
			 manifest_map & m2)
{
  copy(m1.begin(), m1.end(), inserter(m2, m2.begin()));
}


// reading manifest_maps

struct add_to_manifest_map
{    
  manifest_map & man;
  explicit add_to_manifest_map(manifest_map & m) : man(m) {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) 
  {
    std::string ident(res[1].first, res[1].second);
    std::string path(res[2].first, res[2].second);
    if (!book_keeping_file(path))
      man.insert(entry(path, hexenc<id>(ident)));
    else
      throw oops("unsafe filename: " + path);
    return true;
  }
};

void read_manifest_map(data const & dat,
		       manifest_map & man)
{
  regex expr("^([[:xdigit:]]{40})  ([^[:space:]].+)$");
  regex_grep(add_to_manifest_map(man), dat(), expr, match_not_dot_newline);  
}

void read_manifest_map(manifest_data const & dat,
		       manifest_map & man)
{  
  gzip<data> decoded;
  data decompressed;
  decode_base64(dat.inner(), decoded);
  decode_gzip(decoded, decompressed);
  read_manifest_map(decompressed, man);
}



// writing manifest_maps

std::ostream & operator<<(std::ostream & out, entry const & e)
{
  path_id_pair pip(e);
  return (out << pip.ident().inner()() << "  " << pip.path()() << "\n");
}


void write_manifest_map(manifest_map const & man,
			manifest_data & dat)
{
  ostringstream sstr;
  copy(man.begin(),
       man.end(),
       ostream_iterator<entry>(sstr));

  data raw;
  gzip<data> compressed;
  base64< gzip<data> > encoded;

  raw = sstr.str();
  encode_gzip(raw, compressed);
  encode_base64(compressed, encoded);
  dat = manifest_data(encoded);
}

void write_manifest_map(manifest_map const & man,
			data & dat)
{
  ostringstream sstr;
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    sstr << *i;
  dat = sstr.str();
}


// manifest_maps are set-theoretic enough objects that we can use our
// friendly <algorithm> routines

void calculate_manifest_changes(manifest_map const & a,
				manifest_map const & b,
				manifest_changes & chg)
{  
  chg.adds.clear();
  chg.dels.clear();
  set_difference(a.begin(), a.end(), b.begin(), b.end(), 
		 inserter(chg.dels, chg.dels.begin()));
  set_difference(b.begin(), b.end(), a.begin(), a.end(), 
		 inserter(chg.adds, chg.adds.begin()));
}


void apply_manifest_changes(manifest_map const & a,
			    manifest_changes const & chg,
			    manifest_map & b)
{
  set<entry> tmp, deleted;
  copy(a.begin(), a.end(), inserter(tmp, tmp.begin()));
  set_difference(tmp.begin(), tmp.end(), 
		 chg.dels.begin(), chg.dels.end(), 
		 inserter(deleted, deleted.begin()));
  b.clear();
  set_union(deleted.begin(), deleted.end(), 
	    chg.adds.begin(), chg.adds.end(), 
	    inserter(b, b.begin()));
}

void write_manifest_changes(manifest_changes const & changes, 
			    data & dat)
{
  ostringstream out;
  for (set<entry>::const_iterator i = changes.dels.begin();
       i != changes.dels.end(); ++i)
    out << "- " << *i;
  for (set<entry>::const_iterator i = changes.adds.begin();
       i != changes.adds.end(); ++i)
    out << "+ " << *i;
  dat = out.str();  
}
