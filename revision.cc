// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdlib>

#include <boost/lexical_cast.hpp>

#include "basic_io.hh"
#include "change_set.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"

std::string revision_file_name("revision");


struct revision_printer
{
  basic_io::printer & pr;
  revision_printer(basic_io::printer & p) : pr(p)
  {
  }

  void print_add(addition_entry const & add)
  {
    pr.print_key("add", true);
    {
      basic_io::scope sc(pr);
      pr.print_key("path"); pr.print_str(addition_path(add)());
      pr.print_key("data"); pr.print_hex(addition_id(add).inner()());      
    }
  }

  void print_delete(file_path const & pth)
  {
    print_key("delete"); pr.print_str(pth());
  }

  void print_rename(rename_entry const & r)
  {
    print_key("rename", true);
    {
      basic_io::scope sc(pr);
      pr.print_key("src"); pr.print_str(rename_src(r)());
      pr.print_key("dst"); pr.print_str(rename_dst(r)());
    }
  }

  void print_delta(delta_entry const & d)
  {
    print_key("delta", true);
    {
      basic_io::scope sc(pr);
      print_key("path"); pr.print_str(delta_path(d)());
      print_key("src");  pr.print_hex(delta_src_id(d).inner()());
      print_key("dst");  pr.print_hex(delta_dst_id(d).inner()());
    }
  }

  void print_edge(edge_entry const & e)
  {
    print_key("edge", true);
    {
      basic_io::scope sc(pr);
      pr.print_key("old_revision"); pr.print_hex(edge_old_revision(e).inner()());
      pr.print_key("old_manifest"); pr.print_hex(edge_old_manifest(e).inner()());

      change_set const & cs = edge_changes(e);

      for (deletion_set::const_iterator del = cs.dels.begin();
	   del != cs.dels.end(); ++del)
	print_delete(*del);

      for (rename_map::const_iterator ren = cs.renames.begin();
	   ren != cs.renames.end(); ++ren)
	print_rename(*ren);

      for (delta_map::const_iterator delta = cs.deltas.begin();
	   delta != cs.deltas.end(); ++delta)
	print_delta(*delta);

      for (addition_map::const_iterator add = cs.adds.begin();
	   add != cs.adds.end(); ++add)
	print_add(*add);
    }
  }

  void print_revision(revision_set const & rev)
  {
    print_key("revision", true);
    {
      basic_io::scope sc(pr);
      pr.print_key("new_manifest"); pr.print_str(rev.new_manifest.inner()());
      for (edge_map::const_iterator edge = rev.edges.begin();
	   edge != rev.edges.end(); ++edge)
	print_edge(*edge);
    }    
  }
};

struct revision_parser
{
  basic_io::parser & pa;
  revision_parser(basic_io::parser & p) : pa(p) {}

  void parse_add(std::string & path,
		 std::string & hash)
  {
    pa.bra();
    pa.key("path"); pa.colon(); pa.str(path);
    pa.key("data"); pa.colon(); pa.hex(hash);
    pa.ket();
  }

  void parse_rename(std::string & src,
		    std::string & dst)
  {
    pa.bra();
    pa.key("src"); pa.colon(); pa.str(src);
    pa.key("dst"); pa.colon(); pa.str(dst);
    pa.ket();
  }

  void parse_delta(std::string & path,
		   std::string & src,
		   std::string & dst)
  {
    pa.bra();
    pa.key("path"); pa.colon(); pa.str(src);
    pa.key("src"); pa.colon(); pa.hex(src);
    pa.key("dst"); pa.colon(); pa.hex(dst);
    pa.ket();
  }

  void parse_edge(revision_id & rid, 
		  manifest_id & mid,
		  change_set & cs)
  {
    std::string path, hash, del, src, dst;
    pa.bra();

    pa.key("old_revision");
    pa.colon();
    pa.hex(hash);
    rid = revision_id(hash);

    pa.key("old_manifest");
    pa.colon();
    pa.hex(hash);
    mid = manifest_id(hash);

    while (pa.symp())
      {
	if (pa.symp("add")) 
	  { 
	    pa.sym(); 
	    pa.colon(); 
	    parse_add(path, hash); 
	    cs.adds.insert(addition_entry(file_path(path),
					  file_id(hash)));
	  }
	else if (pa.symp("delete")) 
	  { 
	    pa.sym(); 
	    pa.colon(); 
	    pa.str(del); 
	    cs.dels.insert(deletion_entry(del));
	  }
	else if (pa.symp("rename")) 
	  { 
	    pa.sym(); 
	    pa.colon(); 
	    parse_rename(src, dst); 
	    cs.renames.insert(rename_entry(file_path(src),
					   file_path(dst)));
	  }
	else if (pa.symp("delta")) 
	  { 
	    pa.sym(); 
	    pa.colon(); 
	    parse_delta(path, src, dst); 
	    cs.deltas.insert(delta_entry(file_path(path),
					 std::make_pair(file_id(src), 
							file_id(dst))));
	  }
      }
    pa.ket();
  }


  void parse_edges(edge_map & es)
  {
    while(pa.symp("edge"))
      { 
	revision_id rid;
	manifest_id mid;
	change_set cs;
	pa.sym(); 
	pa.colon(); 
	parse_edge(rid, mid, cs); 
	N(es.find(rid) == es.end(),
	  F("multiple edges from revision id %s") % rid);
	es.insert(edge_entry(rid, std::make_pair(mid, cs)));
      }
  }

  void parse_revision(revision_set & rev)
  {
    rev.new_manifest = manifest_id();
    rev.edges.clear();
    pa.advance();
    std::string man;
    pa.key("revision"); 
    pa.colon();
    pa.bra();
    pa.key("new_manifest"); 
    pa.colon(); 
    pa.hex(man);
    rev.new_manifest = manifest_id(man);
    parse_edges(rev.edges);
    pa.ket();
  }
};

void 
read_revision_set(data const & dat,
		 revision_set & cs)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss);
  basic_io::tokenizer tok(src);
  revision_parser pars(tok);
  pars.parse_revision(cs);
}

void 
read_revision_set(revision_data const & dat,
		 revision_set & cs)
{
  data unpacked;
  unpack(dat.inner(), unpacked);
  read_revision_set(unpacked, cs);
}

void
write_revision_set(revision_set const & cs,
		  data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  revision_printer rpr(pr);
  rpr.print_revision(cs);
  dat = data(oss.str());
}

void
write_revision_set(revision_set const & cs,
		  revision_data & dat)
{
  data d;
  write_revision_set(cs, d);
  base64< gzip<data> > packed;
  pack(d, packed);
  dat = revision_data(packed);
}

