// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>
#include <cctype>
#include <cstdlib>

#include "basic_io.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"

using std::logic_error;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

// This file provides parsing and printing primitives used by the
// higher level parser and printer routines for the datatypes cset,
// roster/marking_map and revision.

void basic_io::input_source::err(string const & s)
{
  E(false,
    F("parsing a %s at %d:%d:E: %s") % name % line % col % s);
}


void basic_io::tokenizer::err(string const & s)
{
  in.err(s);
}

string
basic_io::escape(string const & s)
{
  string escaped;
  escaped.reserve(s.size() + 8);

  escaped += "\"";

  for (string::const_iterator i = s.begin(); i != s.end(); ++i)
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

void basic_io::stanza::push_binary_pair(symbol const & k, id const & v)
{
  push_hex_pair(k, hexenc<id>(encode_hexenc(v())));
}

void basic_io::stanza::push_hex_pair(symbol const & k, hexenc<id> const & v)
{
  entries.push_back(make_pair(k, ""));
  string const & s(v());
  entries.back().second.reserve(s.size()+2);
  entries.back().second.push_back('[');
  entries.back().second.append(s);
  entries.back().second.push_back(']');
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_binary_triple(symbol const & k,
                                          string const & n,
                                          id const & v)
{
  string const & s(encode_hexenc(v()));
  entries.push_back(make_pair(k, escape(n) + " " + "[" + s + "]"));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_str_pair(symbol const & k, string const & v)
{
  entries.push_back(make_pair(k, escape(v)));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_file_pair(symbol const & k, file_path const & v)
{
  push_str_pair(k, v.as_internal());
}

void basic_io::stanza::push_str_multi(symbol const & k,
                                      vector<string> const & v)
{
  string val;
  bool first = true;
  for (vector<string>::const_iterator i = v.begin();
       i != v.end(); ++i)
    {
      if (!first)
        val += " ";
      val += escape(*i);
      first = false;
    }
  entries.push_back(make_pair(k, val));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_str_triple(symbol const & k,
                                       string const & n,
                                       string const & v)
{
  entries.push_back(make_pair(k, escape(n) + " " + escape(v)));
  if (k().size() > indent)
    indent = k().size();
}


string basic_io::printer::buf;
int basic_io::printer::count;

basic_io::printer::printer()
{
  I(count == 0);
  count++;
  buf.clear();
}

basic_io::printer::~printer()
{
  count--;
}

void basic_io::printer::print_stanza(stanza const & st)
{
  if (LIKELY(!buf.empty()))
    buf += '\n';

  for (vector<pair<symbol, string> >::const_iterator i = st.entries.begin();
       i != st.entries.end(); ++i)
    {
      for (size_t k = i->first().size(); k < st.indent; ++k)
        buf += ' ';
      buf.append(i->first());
      buf += ' ';
      buf.append(i->second);
      buf += '\n';
    }
}

void basic_io::parser::err(string const & s)
{
  tok.err(s);
}

string basic_io::parser::tt2str(token_type tt)
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

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(basic_io, binary_transparency)
{
  std::string testpattern;
  for (unsigned i=0; i<256; ++i) testpattern+=char(i);

  static symbol test("test");

  basic_io::printer printer;
  basic_io::stanza st;
  st.push_str_pair(test, testpattern);
  printer.print_stanza(st);

  basic_io::input_source source(printer.buf, "unit test string");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);
  std::string t1;
  parser.esym(test);
  parser.str(t1);
  I(testpattern==t1);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
