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

basic_io::input_source::input_source(std::istream & i, std::string const & nm)
  : line(1), col(1), in(i), name(nm), lookahead(0), c('\0')
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
  L(F("error in %s:%d:%d:E: %s") % name % line % col % s);
  throw informative_failure((F("%s:%d:%d:E: %s") 
			     % name % line % col % s).str());
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

    case '"':
      {
	in.advance();
	while (static_cast<char>(in.lookahead) != '"')
	  {
	    if (in.lookahead == EOF)
	      in.err("input stream ended in string");
	    if (static_cast<char>(in.lookahead) == '\\')
	      {
		// possible escape: we understand escaped quotes
		// and escaped backslashes. nothing else.
		in.advance();
		if (!(static_cast<char>(in.lookahead) == '"' 
		      || static_cast<char>(in.lookahead) == '\\'))
		  {
		    in.err("unrecognized character escape");
		  }
	      }
	    in.advance();
	    val += in.c;
	  }

	if (static_cast<char>(in.lookahead) != '"')
	  in.err("string did not end with '\"'");
	in.eat();
	
	return basic_io::TOK_STRING;
      }

    case '[':
      {
	in.advance();
	while (static_cast<char>(in.lookahead) != ']')
	  {
	    if (in.lookahead == EOF)
	      in.err("input stream ended in hex string");
	    if (!std::isxdigit(in.lookahead))
	      in.err("non-hex character in hex string");
	    in.advance();
	    val += in.c;
	  }

	if (static_cast<char>(in.lookahead) != ']')
	  in.err("hex string did not end with ']'");
	in.eat();
	
	return basic_io::TOK_HEX;	
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

basic_io::stanza::stanza() : indent(0)
{}

void basic_io::stanza::push_hex_pair(std::string const & k, std::string const & v)
{
  for (std::string::const_iterator i = k.begin(); i != k.end(); ++i)
    I(std::isalnum(*i) || *i == '_');

  for (std::string::const_iterator i = v.begin(); i != v.end(); ++i)
    I(std::isxdigit(*i));
  
  entries.push_back(std::make_pair(k, "[" + v + "]"));
  if (k.size() > indent)
    indent = k.size();
}

void basic_io::stanza::push_str_pair(std::string const & k, std::string const & v)
{
  for (std::string::const_iterator i = k.begin(); i != k.end(); ++i)
    I(std::isalnum(*i) || *i == '_');

  std::string escaped;

  for (std::string::const_iterator i = v.begin();
       i != v.end(); ++i)
    {
      switch (*i)
	{
	case '\\':
	case '"':
	  escaped += '\\';
	default:
	  escaped += *i;
	}
    }

  
  entries.push_back(std::make_pair(k, "\"" + escaped + "\""));
  if (k.size() > indent)
    indent = k.size();
}


basic_io::printer::printer(std::ostream & ost) 
  : empty_output(true), out(ost)
{}

void basic_io::printer::print_stanza(stanza const & st)
{
  if (empty_output)
    empty_output = false;
  else
    out.put('\n');
  
  for (std::vector<std::pair<std::string, std::string> >::const_iterator i = st.entries.begin();
       i != st.entries.end(); ++i)
    {
      for (size_t k = i->first.size(); k < st.indent; ++k)
	out.put(' ');
      out.write(i->first.data(), i->first.size());
      out.put(' ');
      out.write(i->second.data(), i->second.size());
      out.put('\n');
    }
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

void basic_io::parser::str(std::string & v) { v = token; str(); }
void basic_io::parser::sym(std::string & v) { v = token; sym(); }
void basic_io::parser::hex(std::string & v) { v = token; hex(); }
bool basic_io::parser::symp() { return ttype == basic_io::TOK_SYMBOL; }
bool basic_io::parser::symp(std::string const & val)
{
  return ttype == basic_io::TOK_SYMBOL && token == val;
}
void basic_io::parser::esym(std::string const & val)
{
  if (!(ttype == basic_io::TOK_SYMBOL && token == val))
    err("wanted symbol '" 
	+ val +
	+ "', got "
	+ tt2str(ttype)
	+ (token.empty() 
	   ? std::string("") 
	   : (std::string(" with value ") + token)));
  advance();
}

