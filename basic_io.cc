#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdlib>

#include <boost/lexical_cast.hpp>

#include "basic_io.hh"
#include "sanity.hh"
#include "vocab.hh"

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file provides parsing and printing primitives used by the higher
// level parser and printer routines for the two datatypes change_set and
// revision_set. every revision_set contains a number of change_sets, so
// their i/o routines are somewhat related.

basic_io::input_source::input_source(std::istream & i)
  : line(1), col(1), in(i), lookahead(0), c('\0')
{    
}

void basic_io::input_source::peek()
{
  lookahead = in.peek();    
}

void basic_io::input_source::eat()
{
  in.get(c);
  ++col;
  if (c == '\n')
    {
      col = 1;
      ++line;
    }
}

void basic_io::input_source::advance()
{
  eat();
  peek();
}

void basic_io::input_source::err(std::string const & s)
{
  throw informative_failure((F("revision:%d:%d:E: %s") 
			     % line % col % s).str());
}


basic_io::tokenizer::tokenizer(input_source & i) 
  : in(i)
{
}

basic_io::token_type
basic_io::tokenizer::get_token(std::string & val)
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
      return basic_io::TOK_COLON;
    case '{':
      in.eat();
      return basic_io::TOK_OPEN_BRACE;
    case '}':
      in.eat();
      return basic_io::TOK_CLOSE_BRACE;
    case '[':
      {
	token_type tt = basic_io::TOK_NONE;
	in.advance();
	switch (static_cast<char>(in.lookahead))
	  {
	  case 's':
	    tt = basic_io::TOK_STRING;
	    break;
	  case 'x':
	    tt = basic_io::TOK_HEX;
	    break;
	  }
	if (tt == basic_io::TOK_NONE)
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
	    if (tt == basic_io::TOK_HEX && !std::isxdigit(in.lookahead))
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
	  return basic_io::TOK_SYMBOL;
	}
    }
  return basic_io::TOK_NONE;
}

void basic_io::tokenizer::err(std::string const & s)
{
  in.err(s);
}

basic_io::printer::printer(std::ostream & ost) 
  : offset(0), step(2), out(ost)
{}

void basic_io::printer::print_indent()
{
  for (size_t i = 0; i < offset; ++i) 
    out.put(' ');
}

void basic_io::printer::print_bra()
{
  print_indent(); 
  out.put('{'); 
  out.put('\n');
  offset += step;
}

void basic_io::printer::print_ket()
{
  I(offset >= step);
  offset -= step;
  print_indent(); 
  out.put('}'); 
  out.put('\n');
}

void basic_io::printer::print_val(char c, std::string const & s)
{
  out.put('[');
  out.put(c);
  out << s.size();
  out.put(':');
  out << s;
  out.put(']');
  out.put('\n');
}

void basic_io::printer::print_hex(std::string const & s)
{
  print_val('x', s);
}

void basic_io::printer::print_str(std::string const & s)
{
  print_val('s', s);
}
  
void basic_io::printer::print_key(std::string const & s, bool eol)
{
  print_indent();
  out.write(s.data(), s.size());
  out.put(':');
  out.put(eol ? '\n' : ' ');
}

void basic_io::printer::print_sym(std::string const & s, bool eol)
{
  out.write(s.data(), s.size());
  out.put(eol ? '\n' : ' ');
}

basic_io::scope::scope(basic_io::printer & p) 
  : cp(p)
{
  cp.print_bra();
}

basic_io::scope::~scope()
{
  cp.print_ket();
}



basic_io::parser::parser(tokenizer & t) 
  : tok(t) 
{
  advance();
}

void basic_io::parser::advance()
{
  ttype = tok.get_token(token);
}

void basic_io::parser::err(std::string const & s)
{
  tok.err(s);
}

std::string basic_io::parser::tt2str(token_type tt)
{
  switch (tt)
    {
    case basic_io::TOK_STRING:
      return "TOK_STRING";
    case basic_io::TOK_SYMBOL:
      return "TOK_SYMBOL";
    case basic_io::TOK_HEX:
      return "TOK_HEX";
    case basic_io::TOK_OPEN_BRACE:
      return "TOK_OPEN_BRACE";
    case basic_io::TOK_CLOSE_BRACE:
      return "TOK_CLOSE_BRACE";
    case basic_io::TOK_COLON:
      return "TOK_COLON";
    case basic_io::TOK_NONE:
      return "TOK_NONE";
    }
  return "TOK_UNKNOWN";
}

void basic_io::parser::eat(token_type want)
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

void basic_io::parser::str() { eat(basic_io::TOK_STRING); }
void basic_io::parser::sym() { eat(basic_io::TOK_SYMBOL); }
void basic_io::parser::hex() { eat(basic_io::TOK_HEX); }
void basic_io::parser::colon() { eat(basic_io::TOK_COLON); }
void basic_io::parser::bra() { eat(basic_io::TOK_OPEN_BRACE); } 
void basic_io::parser::ket() { eat(basic_io::TOK_CLOSE_BRACE); } 

void basic_io::parser::str(std::string & v) { v = token; str(); }
void basic_io::parser::sym(std::string & v) { v = token; sym(); }
void basic_io::parser::hex(std::string & v) { v = token; hex(); }
bool basic_io::parser::symp() { return ttype == basic_io::TOK_SYMBOL; }
bool basic_io::parser::symp(std::string const & val)
{
  return ttype == basic_io::TOK_SYMBOL && token == val;
}

void basic_io::parser::key(std::string const & val)
{
  std::string s;
  sym(s);
  if (s != val)
    err("expected symbol " + val + ", got " + s);
  colon();
}
