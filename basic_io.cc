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


void basic_io::input_source::err(std::string const & s)
{
  L(F("error in %s:%d:%d:E: %s") % name % line % col % s);
  throw informative_failure((F("error in %s:%d:%d:E: %s") 
                             % name % line % col % s).str());
}


void basic_io::tokenizer::err(std::string const & s)
{
  in.err(s);
}

std::string basic_io::escape(std::string const & s)
{
  std::string escaped;
  escaped.reserve(s.size() + 8);

  escaped += "\"";

  for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
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

  escaped += "\"";

  return escaped;
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

  entries.push_back(std::make_pair(k, escape(v)));
  if (k.size() > indent)
    indent = k.size();
}

void basic_io::stanza::push_file_pair(std::string const & k, file_path const & v)
{
  push_str_pair(k, v.as_internal());
}

void basic_io::stanza::push_str_multi(std::string const & k,
                                      std::vector<std::string> const & v)
{
  for (std::string::const_iterator i = k.begin(); i != k.end(); ++i)
    I(std::isalnum(*i) || *i == '_');

  std::string val;
  bool first = true;
  for (std::vector<std::string>::const_iterator i = v.begin();
       i != v.end(); ++i)
    {
      if (!first)
        val += " ";
      val += escape(*i);
      first = false;
    }
  entries.push_back(std::make_pair(k, val));
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


