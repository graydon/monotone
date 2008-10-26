#ifndef __BASIC_IO_HH__
#define __BASIC_IO_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "vector.hh"
#include <map>

#include "paths.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "numeric_vocab.hh"
#include "char_classifiers.hh"

// This file provides parsing and printing primitives used by the
// higher level parser and printer routines for the datatypes cset,
// roster/marking_map and revision.

namespace basic_io
{

  namespace
    {
      namespace syms
        {
          // general format symbol
          symbol const format_version("format_version");

          // common symbols
          symbol const dir("dir");
          symbol const file("file");
          symbol const content("content");
          symbol const attr("attr");

          symbol const content_mark("content_mark");
        }
    }

  typedef enum
    {
      TOK_SYMBOL,
      TOK_STRING,
      TOK_HEX,
      TOK_NONE
    } token_type;

  struct
  input_source
  {
    size_t line, col;
    std::string const & in;
    std::string::const_iterator curr;
    std::string name;
    int lookahead;
    char c;
    input_source(std::string const & in, std::string const & nm)
      : line(1), col(1), in(in), curr(in.begin()),
	name(nm), lookahead(0), c('\0')
    {}

    inline void peek()
    {
      if (LIKELY(curr != in.end()))
        // we do want to distinguish between EOF and '\xff',
        // so we translate '\xff' to 255u
	lookahead = widen<unsigned int,char>(*curr);
      else
	lookahead = EOF;
    }

    inline void advance()
    {
      if (LIKELY(curr != in.end()))
        {
          c = *curr;
          ++curr;
          ++col;
          if (c == '\n')
            {
              col = 1;
              ++line;
            }
        }
      peek();
    }
    void err(std::string const & s);
  };

  struct
  tokenizer
  {
    input_source & in;
    std::string::const_iterator begin;
    std::string::const_iterator end;

    tokenizer(input_source & i) : in(i), begin(in.curr), end(in.curr)
    {}

    inline void mark()
    {
      begin = in.curr;
      end = begin;
    }

    inline void advance()
    {
      in.advance();
      end = in.curr;
    }

    inline void store(std::string & val)
    {
      val.assign(begin, end);
    }

    inline token_type get_token(std::string & val)
    {
      in.peek();

      while (true)
        {
          if (UNLIKELY(in.lookahead == EOF))
            return TOK_NONE;
          if (!is_space(in.lookahead))
            break;
          in.advance();
        }

      if (is_alpha(in.lookahead))
	{
	  mark();
	  while (is_alnum(in.lookahead) || in.lookahead == '_')
	    advance();
	  store(val);
	  return basic_io::TOK_SYMBOL;
	}
      else if (in.lookahead == '[')
	{
	  in.advance();
	  mark();
	  while (static_cast<char>(in.lookahead) != ']')
	    {
	      if (UNLIKELY(in.lookahead == EOF))
		in.err("input stream ended in hex string");
              if (UNLIKELY(!is_xdigit(in.lookahead)))
                in.err("non-hex character in hex string");
              advance();
	    }

	  store(val);

	  if (UNLIKELY(static_cast<char>(in.lookahead) != ']'))
	    in.err("hex string did not end with ']'");
	  in.advance();

	  return basic_io::TOK_HEX;
	}
      else if (in.lookahead == '"')
	{
	  in.advance();
	  mark();
	  while (static_cast<char>(in.lookahead) != '"')
	    {
	      if (UNLIKELY(in.lookahead == EOF))
		in.err("input stream ended in string");
	      if (UNLIKELY(static_cast<char>(in.lookahead) == '\\'))
		{
		  // Possible escape: we understand escaped quotes and
		  // escaped backslashes. Nothing else. If we // happen to
		  // hit an escape, we stop doing the mark/store // thing
		  // and switch to copying and appending per-character
		  // until the // end of the token.

                  // So first, store what we have *before* the escape.
                  store(val);

                  // Then skip over the escape backslash.
		  in.advance();

                  // Make sure it's an escape we recognize.
		  if (UNLIKELY(!(static_cast<char>(in.lookahead) == '"'
                                 ||
				 static_cast<char>(in.lookahead) == '\\')))
                    in.err("unrecognized character escape");

                  // Add the escaped character onto the accumulating token.
		  in.advance();
                  val += in.c;

                  // Now enter special slow loop for remainder.
                  while (static_cast<char>(in.lookahead) != '"')
                    {
                      if (UNLIKELY(in.lookahead == EOF))
                        in.err("input stream ended in string");
                      if (UNLIKELY(static_cast<char>(in.lookahead) == '\\'))
                        {
                          // Skip over any further escape marker.
                          in.advance();
                          if (UNLIKELY
			      (!(static_cast<char>(in.lookahead) == '"'
				 ||
				 static_cast<char>(in.lookahead) == '\\')))
                            in.err("unrecognized character escape");
                        }
                      in.advance();
                      val += in.c;
                    }
                  // When slow loop completes, return early.
                  if (static_cast<char>(in.lookahead) != '"')
                    in.err("string did not end with '\"'");
                  in.advance();

                  return basic_io::TOK_STRING;
		}
	      advance();
	    }

	  store(val);

	  if (UNLIKELY(static_cast<char>(in.lookahead) != '"'))
	    in.err("string did not end with '\"'");
	  in.advance();

	  return basic_io::TOK_STRING;
	}
      else
	return basic_io::TOK_NONE;
    }
   void err(std::string const & s);
  };

  std::string escape(std::string const & s);

  struct
  stanza
  {
    stanza();
    size_t indent;
    std::vector<std::pair<symbol, std::string> > entries;
    void push_symbol(symbol const & k);
    void push_hex_pair(symbol const & k, hexenc<id> const & v);
    void push_binary_pair(symbol const & k, id const & v);
    void push_binary_triple(symbol const & k, std::string const & n,
			 id const & v);
    void push_str_pair(symbol const & k, std::string const & v);
    void push_str_pair(symbol const & k, symbol const & v);
    void push_str_triple(symbol const & k, std::string const & n,
			 std::string const & v);
    void push_file_pair(symbol const & k, file_path const & v);
    void push_str_multi(symbol const & k,
                        std::vector<std::string> const & v);
    void push_str_multi(symbol const & k1,
                        symbol const & k2,
                        std::vector<std::string> const & v);
  };


  // Note: printer uses a static buffer; thus only one buffer
  // may be referenced (globally). An invariant will be triggered
  // if more than one basic_io::printer is instantiated.
  struct
  printer
  {
    static std::string buf;
    static int count;
    printer();
    ~printer();
    void print_stanza(stanza const & st);
  };

  struct
  parser
  {
    tokenizer & tok;
    parser(tokenizer & t) : tok(t)
    {
      token.reserve(128);
      advance();
    }

    std::string token;
    token_type ttype;

    void err(std::string const & s);
    std::string tt2str(token_type tt);

    inline void advance()
    {
      ttype = tok.get_token(token);
    }

    inline void eat(token_type want)
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

    inline void str() { eat(basic_io::TOK_STRING); }
    inline void sym() { eat(basic_io::TOK_SYMBOL); }
    inline void hex() { eat(basic_io::TOK_HEX); }

    inline void str(std::string & v) { v = token; str(); }
    inline void sym(std::string & v) { v = token; sym(); }
    inline void hex(std::string & v) { v = token; hex(); }
    inline bool symp() { return ttype == basic_io::TOK_SYMBOL; }
    inline bool symp(symbol const & val)
    {
      return ttype == basic_io::TOK_SYMBOL && token == val();
    }
    inline void esym(symbol const & val)
    {
      if (!(ttype == basic_io::TOK_SYMBOL && token == val()))
        err("wanted symbol '"
            + val() +
            + "', got "
            + tt2str(ttype)
            + (token.empty()
               ? std::string("")
               : (std::string(" with value ") + token)));
      advance();
    }
  };

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __BASIC_IO_HH__
