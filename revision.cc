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

namespace 
{
  namespace syms
  {
    std::string const revision("revision");
    std::string const edge("edge");
    std::string const old_revision("old_revision");
    std::string const new_manifest("new_manifest");
    std::string const old_manifest("old_manifest");
    std::string const change_set("change_set");
  }
}


void 
print_edge(basic_io::printer & printer,
	   edge_entry const & e)
{       
  printer.print_key(syms::edge, true);
  {
    basic_io::scope sc(printer);

    printer.print_key(syms::old_revision); 
    printer.print_hex(edge_old_revision(e).inner()());
    
    printer.print_key(syms::old_manifest); 
    printer.print_hex(edge_old_manifest(e).inner()());
    
    print_change_set(printer, edge_changes(e));
  }
}


void 
print_revision(basic_io::printer & printer,
	       revision_set const & rev)
{
  printer.print_key(syms::revision, true);
  {
    basic_io::scope sc(printer);

    printer.print_key(syms::new_manifest); 
    printer.print_str(rev.new_manifest.inner()());    

    for (edge_map::const_iterator edge = rev.edges.begin();
	 edge != rev.edges.end(); ++edge)
      print_edge(printer, *edge);
  }
}


void 
parse_edge(basic_io::parser & parser,
	   edge_map & es)
{
  change_set cs;
  manifest_id old_man;
  revision_id old_rev;
  std::string tmp;

  parser.key(syms::edge);
  parser.bra();
  {
    parser.key(syms::old_revision);
    parser.hex(tmp);
    old_rev = revision_id(tmp);

    parser.key(syms::old_manifest);
    parser.hex(tmp);
    old_man = manifest_id(tmp);

    parse_change_set(parser, cs);
  }
  parser.ket();

  es.insert(std::make_pair(old_rev, std::make_pair(old_man, cs)));
}


void 
parse_revision(basic_io::parser & parser,
	       revision_set & rev)
{
  rev.edges.clear();

  parser.key(syms::revision);
  parser.bra();
  {
    std::string tmp;
    parser.key(syms::new_manifest);
    parser.hex(tmp);
    rev.new_manifest = manifest_id(tmp);
    while (parser.symp(syms::edge))
      parse_edge(parser, rev.edges);
  }
  parser.ket();
}

void 
read_revision_set(data const & dat,
		  revision_set & rev)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss);
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_revision(pars, rev);
}

void 
read_revision_set(revision_data const & dat,
		  revision_set & rev)
{
  data unpacked;
  unpack(dat.inner(), unpacked);
  read_revision_set(unpacked, rev);
}

void
write_revision_set(revision_set const & rev,
		   data & dat)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  print_revision(pr, rev);
  dat = data(oss.str());
}

void
write_revision_set(revision_set const & rev,
		   revision_data & dat)
{
  data d;
  write_revision_set(rev, d);
  base64< gzip<data> > packed;
  pack(d, packed);
  dat = revision_data(packed);
}

