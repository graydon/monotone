// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

#include <boost/lexical_cast.hpp>

#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"

std::string revision_file_name("revision");

typedef enum
  {
    TOK_SYMBOL,
    TOK_OPEN_BRACE,
    TOK_CLOSE_BRACE,
    TOK_COLON,
    TOK_STRING,
    TOK_HEX,
    TOK_NONE,
  } token_type;


struct 
revision_input_source
{
  size_t line, col;
  std::istream & in;

  int lookahead;
  char c;

  revision_input_source(std::istream & i)
    : line(1), col(1), in(i), lookahead(0), c('\0')
  {    
  }

  void peek()
  {
    lookahead = in.peek();    
  }

  void eat()
  {
    in.get(c);
    ++col;
    if (c == '\n')
      {
	col = 1;
	++line;
      }
  }

  void advance()
  {
    eat();
    peek();
  }

  void err(std::string const & s)
  {
    throw informative_failure((F("revision:%d:%d:E: %s") 
			      % line % col % s).str());
  }
};

struct
revision_tokenizer
{

  revision_input_source & in;
  revision_tokenizer(revision_input_source & i) : in(i)
  {}

  token_type
  get_token(std::string & val)
  {
    val.clear();
    in.peek();

    while (true)
      {
	if (in.lookahead == EOF)
	  return TOK_NONE;
	if (!std::isspace(in.lookahead))
	  break;
	in.advance();
      }

    switch (in.lookahead)
      {
      case ':':
	in.eat();
	return TOK_COLON;
      case '{':
	in.eat();
	return TOK_OPEN_BRACE;
      case '}':
	in.eat();
	return TOK_CLOSE_BRACE;
      case '[':
	{
	  token_type tt = TOK_NONE;
	  in.advance();
	  switch (static_cast<char>(in.lookahead))
	    {
	    case 's':
	      tt = TOK_STRING;
	      break;
	    case 'x':
	      tt = TOK_HEX;
	      break;
	    }
	  if (tt == TOK_NONE)
	    in.err("unknown string type specifier");
	
	  in.advance();
	
	  std::string ss;
	  while(std::isdigit(in.lookahead))
	    {
	      ss += static_cast<char>(in.lookahead);
	      in.advance();
	    }
	  if (static_cast<char>(in.lookahead) != ':')
	    in.err("string lacks ':' after digits");

	  in.advance();
	  for (size_t len = boost::lexical_cast<size_t>(ss); len > 0; --len)
	    {
	      if (in.lookahead == EOF)
		in.err("input stream ended in string");
	      if (tt == TOK_HEX && !isxdigit(in.lookahead))
		in.err("non-hex character in hex string");
	      in.advance();
	      val += in.c;
	    }
	  if (static_cast<char>(in.lookahead) != ']')
	    in.err("string did not end with ']'");
	  in.eat();
	  return tt;
	}

      default:
	if (std::isalpha(in.lookahead))
	  {
	    while (std::isalnum(in.lookahead) || in.lookahead == '_')
	      {
		in.advance();
		val += in.c;
	      }
	    return TOK_SYMBOL;
	  }
      }
    return TOK_NONE;
  }
  void err(std::string const & s)
  {
    in.err(s);
  }
};


struct 
revision_printer
{
  size_t offset;
  size_t const step;
  std::ostream & out;
  revision_printer(std::ostream & ost) 
    : offset(0), step(2), out(ost)
  {}

  void print_indent()
  {
    for (size_t i = 0; i < offset; ++i) 
      out.put(' ');
  }

  void print_bra()
  {
    print_indent(); 
    out.put('{'); 
    out.put('\n');
    offset += step;
  }

  void print_ket()
  {
    print_indent(); 
    out.put('}'); 
    out.put('\n');
    I(offset >= step);
    offset -= step;
  }

  struct scope
  {
    revision_printer & cp;
    scope(revision_printer & c) 
      : cp(c)
    {
      cp.print_bra();
    }
    ~scope()
    {
      cp.print_ket();
    }
  };

  void print_val(char c, std::string const & s)
  {
    out.put('[');
    out.put(c);
    out << s.size();
    out.put(':');
    out << s;
    out.put(']');
    out.put('\n');
  }

  void print_hex(std::string const & s)
  {
    print_val('x', s);
  }

  void print_str(std::string const & s)
  {
    print_val('s', s);
  }
  
  void print_key(std::string const & s, bool eol = false)
  {
    print_indent();
    out.write(s.data(), s.size());
    out.put(':');
    out.put(eol ? '\n' : ' ');
  }

  void print_add(addition_entry const & add)
  {
    print_key("add", true);
    {
      scope sc(*this);
      print_key("path"); print_str(addition_path(add)());
      print_key("data"); print_hex(addition_id(add).inner()());      
    }
  }

  void print_delete(file_path const & pth)
  {
    print_key("delete"); print_str(pth());
  }

  void print_rename(rename_entry const & r)
  {
    print_key("rename", true);
    {
      scope sc(*this);
      print_key("src"); print_str(rename_src(r)());
      print_key("dst"); print_str(rename_dst(r)());
    }
  }

  void print_delta(delta_entry const & d)
  {
    print_key("delta", true);
    {
      scope sc(*this);
      print_key("path"); print_str(delta_path(d)());
      print_key("src");  print_hex(delta_src_id(d).inner()());
      print_key("dst");  print_hex(delta_dst_id(d).inner()());
    }
  }

  void print_edge(edge_entry const & e)
  {
    print_key("edge", true);
    {
      scope sc(*this);
      print_key("old_revision"); print_hex(edge_old_revision(e).inner()());
      print_key("old_manifest"); print_hex(edge_old_manifest(e).inner()());

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
      scope sc(*this);
      print_key("new_manifest"); print_str(rev.new_manifest.inner()());
      for (edge_map::const_iterator edge = rev.edges.begin();
	   edge != rev.edges.end(); ++edge)
	print_edge(*edge);
    }    
  }
};

struct
revision_parser
{
  revision_tokenizer & tok;
  revision_parser(revision_tokenizer & t) 
    : tok(t) 
  {}

  std::string token;
  token_type ttype;

  void advance()
  {
    ttype = tok.get_token(token);
  }
  void err(std::string const & s)
  {
    tok.err(s);
  }
  std::string tt2str(token_type tt)
  {
    switch (tt)
      {
      case TOK_STRING:
	return "TOK_STRING";
      case TOK_SYMBOL:
	return "TOK_SYMBOL";
      case TOK_HEX:
	return "TOK_HEX";
      case TOK_OPEN_BRACE:
	return "TOK_OPEN_BRACE";
      case TOK_CLOSE_BRACE:
	return "TOK_CLOSE_BRACE";
      case TOK_COLON:
	return "TOK_COLON";
      case TOK_NONE:
	return "TOK_NONE";
      }
    return "TOK_UNKNOWN";
  }

  void eat(token_type want)
  {
    if (ttype != want)
      err("wanted " 
	  + tt2str(want)
	  + ", got "
	  + tt2str(ttype)
	  + (token.empty() 
	     ? std::string("") 
	     : (std::string(" with value ") + token)));
    advance();
  }

  void str() { eat(TOK_STRING); }
  void sym() { eat(TOK_SYMBOL); }
  void hex() { eat(TOK_HEX); }
  void colon() { eat(TOK_COLON); }
  void bra() { eat(TOK_OPEN_BRACE); } 
  void ket() { eat(TOK_CLOSE_BRACE); } 

  void str(std::string & v) { v = token; str(); }
  void sym(std::string & v) { v = token; sym(); }
  void hex(std::string & v) { v = token; hex(); }
  bool symp() { return ttype == TOK_SYMBOL; }
  bool symp(std::string const & val)
  {
    return ttype == TOK_SYMBOL && token == val;
  }

  void key(std::string const & val)
  {
    std::string s;
    sym(s);
    if (s != val)
      err("expected symbol " + val + ", got " + s);
  }

  void parse_add(std::string & path,
		 std::string & hash)
  {
    bra();
    key("path"); colon(); str(path);
    key("data"); colon(); hex(hash);
    ket();
  }

  void parse_rename(std::string & src,
		    std::string & dst)
  {
    bra();
    key("src"); colon(); str(src);
    key("dst"); colon(); str(dst);
    ket();
  }

  void parse_delta(std::string & path,
		   std::string & src,
		   std::string & dst)
  {
    bra();
    key("path"); colon(); str(src);
    key("src"); colon(); hex(src);
    key("dst"); colon(); hex(dst);
    ket();
  }

  void parse_edge(revision_id & rid, 
		  manifest_id & mid,
		  change_set & cs)
  {
    std::string path, hash, del, src, dst;
    bra();

    key("old_revision");
    colon();
    hex(hash);
    rid = revision_id(hash);

    key("old_manifest");
    colon();
    hex(hash);
    mid = manifest_id(hash);

    while (symp())
      {
	if (symp("add")) 
	  { 
	    sym(); 
	    colon(); 
	    parse_add(path, hash); 
	    cs.adds.insert(addition_entry(file_path(path),
					  file_id(hash)));
	  }
	else if (symp("delete")) 
	  { 
	    sym(); 
	    colon(); 
	    str(del); 
	    cs.dels.insert(deletion_entry(del));
	  }
	else if (symp("rename")) 
	  { 
	    sym(); 
	    colon(); 
	    parse_rename(src, dst); 
	    cs.renames.insert(rename_entry(file_path(src),
					   file_path(dst)));
	  }
	else if (symp("delta")) 
	  { 
	    sym(); 
	    colon(); 
	    parse_delta(path, src, dst); 
	    cs.deltas.insert(delta_entry(file_path(path),
					 std::make_pair(file_id(src), 
							file_id(dst))));
	  }
      }
    ket();
  }


  void parse_edges(edge_map & es)
  {
    while(symp("edge"))
      { 
	revision_id rid;
	manifest_id mid;
	change_set cs;
	sym(); 
	colon(); 
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
    advance();
    std::string man;
    key("revision"); 
    colon();
    bra();
    key("new_manifest"); 
    colon(); 
    hex(man);
    rev.new_manifest = manifest_id(man);
    parse_edges(rev.edges);
    ket();
  }
};

void 
read_revision_set(data const & dat,
		 revision_set & cs)
{
  std::istringstream iss(dat());
  revision_input_source src(iss);
  revision_tokenizer tok(src);
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
  revision_printer prn(oss);
  prn.print_revision(cs);
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

bool operator==(const change_set & a, 
		const change_set & b)
{
  return
    (a.adds == b.adds)
    && (a.dels == b.dels)
    && (a.renames == b.renames)
    && (a.deltas == b.deltas);
}

bool operator<(const change_set & a, 
	       const change_set & b)
{
  return    
    ((a.adds < b.adds))
    || ((a.adds == b.adds) && (a.dels < b.dels))
    || ((a.adds == b.adds) && (a.dels == b.dels) && (a.renames < b.renames))
    || ((a.adds == b.adds) && (a.dels == b.dels) && (a.renames == b.renames) && (a.deltas < b.deltas));
}

void 
extract_added_paths(change_set const & c,
		    std::set<file_path> & paths)
{
  paths.clear();
  for (addition_map::const_iterator i = c.adds.begin();
       i != c.adds.end(); ++i)
    paths.insert(addition_path(i));
}

void 
extract_added_idents(change_set const & c,
		     std::set<file_id> & idents)
{
  idents.clear();
  for (addition_map::const_iterator i = c.adds.begin();
       i != c.adds.end(); ++i)
    idents.insert(addition_id(i));
}

void 
extract_renames_src_to_dst(change_set const & c,
			   std::map<file_path, file_path> & renames)
{
  renames.clear();
  renames = c.renames;
}

void
extract_renames_dst_from_src(change_set const & c,
			     std::map<file_path, file_path> & renames)
{
  renames.clear();
  for (std::set<rename_entry>::const_iterator i = c.renames.begin();
       i != c.renames.end(); ++i)
    {
      I(renames.find(rename_dst(i)) == renames.end());
      renames.insert(std::make_pair(rename_dst(i), rename_src(i)));
    }
}



/*

a change_set (logically) has 3 maps and 1 set of changes represented in it:

  - deletes (a set: a bunch of names)
  - renames (a map: each src name maps to at most 1 dst)
  - adds    (a map: each name has 1 new content value)
  - deltas  (a map: each name has 1 delta applied)

these changes are *ordered*. that is to say that deletes are performed
before renames, adds and deltas. this is important: it is legal to delete a
file and add it back with different content, whereas if adds were
interpreted before deletes this would be a no-op (the delete would
annaihilate the add). 

we can therefore think of the action of "applying a changeset" as having 4
discrete, instantaneous "phases". each phase operates on the manifest
produced by the proceeding phase. thus "renames" apply to the manifest as
it appears after having some entries deleted. additions apply to a manifest
as it appears after having some entries deleted and some of the surviving
entries renamed. deltas apply to a manifest as it appears after having some
entries deleted, some of the surviving entries renamed, and some new
entries added. we will refer to these states by number:

    (state 0)
  - deletes
    (state 1)
  - renames
    (state 2)
  - deltas
    (state 3)
  - adds
    (state 4)


applicability and sanity checking
---------------------------------

this ordering allows us to reason about the applicability of a changeset to
a particular pre-state (state 0) of a collection of files. in particular,
for a particular state 0, if:

  - every delete exists in state 0
  - every rename source exists in state 1
  - every rename destination does not exist in state 1
  - every delta preimage exists in state 2
  - every add does not exist in state 3

then the changeset is said to be "applicable" to that state 0. furthermore
this ordering allows us to do sanity checking on the changeset as a whole,
to exclude "nonsense" changesets. if we name the following path sets:

  - D = {the set of filenames deleted}
  - S = {the set of rename sources}
  - T = {the set of rename targets}
  - E = {the set of filenames subject to edits}
  - A = {the set of filenames added}
  - K = {the set of "killed" names} = ((D union S) - (A union T))

then the following is the "sanity condition" for a changeset:

  - D intersect S = 0
  - D intersect A = 0
  - T intersect A = 0
  - E intersect A = 0
  - E intersect K = 0

  additionally:

  - every destination in T occurs in exactly one rename's destination, 
    in the rename map

edges
-----

a triple of [source revision id, source manifest id, changeset] is called
an "edge". an edge's invariant is that the changeset is applicable to the
source manifest identified by it (assuming this manifest can be found).

operations
----------

there are two significant operations to do with change_sets.


sequential concatenation (+)
----------------------------

C <- A+B involves applying every change in B "after" every change in A. it
is significantly different than merging; the concatenation is ordered.
the result of the concatenation must be sane, otherwise the concatenation
is viewed as illegal. the algorithm is somewhat subtle and unintuitive:

  - begin by copying C <- A

  - for each del D1 in B, pretend that D1 was appended to the 4 sets of
    changes described in C. now push on the bottom: migrate D backwards
    over all the deltas, adds, and renames in C, and union it with the
    deletes in C. 
     - if D1 encounters a delta with the same name as it, annaihilate
       the delta and preserve D1
     - if D1 encounters an add with the same name as it, annaihilate
       the add and preserve D1
     - if D1 encounters a rename with *destination* name equal to
       D1, rename D1 to the source name and annaihilate the rename.

  - for each rename R1 in B migrate R1 backwards over the deltas and adds in
    C, coming to rest in the renames of C.
     - if R1 encounters a delta (in C) applying to R1's source name, rename
       the delta to apply to R1's destination name
     - if R1 encounters an add in C with the same name as R1's source, 
       rename the add to have the same name as R's destination and annaihilate
       R1
     - if R1 encounters a rename in C with the same destination as R1's source,
       replace the two renames with their concatenation

  - for each delta E1 in B migrate E1 backwards over the adds in C, coming
    to rest in the deltas of C
    - if E1 encounters an add with the same path name and image equal to
      E1's pre-image, change the add to contain E1's post-image and annaihilate
      E1.
    - if E1 encounters a delta on the same path name, with post-image equal
      to its pre-image, replace the two deltas with their concatenation.

  - for each add A1 in B, ensure that it does not conflict with any adds in
    C and otherwise union it into the adds of C.


parallel merger (|)
-------------------

A|B involves applying every change in B "simultaneously" with every
change in A. it is a different (not necessarily stronger or weaker)
condition to say that A can be merged with B. like concatenation, the
result must be sane:

 - deletes are merged by union.

 - renames are merged by removing all deleted names from the renames,
   then union. the pre-union sets must have disjoint sources and
   disjoint destinations.

 - deltas are merged by applying all the deletes and renames in A
   to the deltas in B and vice-versa, then doing union. if there is
   a conflicting delta on a file, it is 3-way merged at the line level.

 - additions are merged by union. the pre-union sets must have disjoint
   paths (or agree on their content).

 */


change_set::change_set() 
{
}

change_set::change_set(change_set const & other) 
  : dels(other.dels), renames(other.renames),
    deltas(other.deltas), adds(other.adds) 
{
  I(is_sane());
}

change_set const & 
change_set::operator=(change_set const & other)
{
  I(other.is_sane());
  this->dels = other.dels;
  this->renames = other.renames;
  this->deltas = other.deltas;
  this->adds = other.adds;
  I(this->is_sane());
  return *this;
}

change_set const & 
change_set::operator+(change_set const & other) const
{
  return *this;
}

change_set const & 
change_set::operator|(change_set const & other) const
{
  return *this;
}

bool 
change_set::is_applicable(manifest_map const &m) const
{
  manifest_map tmp(m);

  for (deletion_set::const_iterator i = dels.begin();
       i != dels.end(); ++i)
    {
      if (tmp.find(*i) == tmp.end())
	return false;
      else
	tmp.erase(*i);
    }
  
  for (rename_map::const_iterator i = renames.begin();
       i != renames.end(); ++i)
    {
      if (tmp.find(rename_src(i)) == tmp.end())
	return false;
      else if (tmp.find(rename_dst(i)) != tmp.end())
	return false;
      else
	{
	  file_id tid = tmp[rename_src(i)];
	  tmp.erase(rename_src(i));
	  tmp.insert(std::make_pair(rename_dst(i), tid));
	}
    }

  for (delta_map::const_iterator i = deltas.begin();
       i != deltas.end(); ++i)
    {
      manifest_map::const_iterator e = tmp.find(delta_path(i));
      if (e == tmp.end())
	return false;
      else if (! (manifest_entry_id(e) == delta_src_id(i)))
	return false;
    }


  for (addition_map::const_iterator i = adds.begin(); 
       i != adds.end(); ++i)
    {
      if (tmp.find(addition_path(i)) != tmp.end())
	return false;
      else
	tmp.insert(std::make_pair(addition_path(i),
				  addition_id(i)));
    }
    
  return true;
}

bool 
change_set::is_sane() const
{
  return true;
}


