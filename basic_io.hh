#ifndef __BASIC_IO_HH__
#define __BASIC_IO_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file provides parsing and printing primitives used by the higher
// level parser and printer routines for the two datatypes change_set and
// revision_set. every revision_set contains a number of change_sets, so
// their i/o routines are somewhat related.

#include <iosfwd>
#include <string>
#include <vector>
#include <map>

#include "paths.hh"
#include "sanity.hh"

namespace basic_io
{

  inline bool is_xdigit(char x) 
  { 
    return ((x >= '0' && x <= '9')
	    || (x >= 'a' && x <= 'f')
	    || (x >= 'A' && x <= 'F'));
  }

  inline bool is_alpha(char x)
  {
    return ((x >= 'a' && x <= 'z')
	    || (x >= 'A' && x <= 'Z'));
  }

  inline bool is_alnum(char x)
  {
    return ((x >= '0' && x <= '9')
	    || (x >= 'a' && x <= 'z')
	    || (x >= 'A' && x <= 'Z'));
  }

  inline bool is_space(char x)
  {
    return (x == ' ') 
      || (x == '\n')
      || (x == '\t')
      || (x == '\r')
      || (x == '\v')
      || (x == '\f');
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
      : line(1), col(1), in(in), curr(in.begin()), name(nm), lookahead(0), c('\0')
    {}

    inline void peek() 
    { 
      if (LIKELY(curr != in.end()))
	lookahead = *curr; 
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
                                 || static_cast<char>(in.lookahead) == '\\')))
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
                          if (UNLIKELY(!(static_cast<char>(in.lookahead) == '"' 
                                         || static_cast<char>(in.lookahead) == '\\')))
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
    std::vector<std::pair<std::string, std::string> > entries;
    void push_hex_pair(std::string const & k, std::string const & v);
    void push_hex_triple(std::string const & k, std::string const & n, std::string const & v);
    void push_str_pair(std::string const & k, std::string const & v);
    void push_str_triple(std::string const & k, std::string const & n, std::string const & v);
    void push_file_pair(std::string const & k, file_path const & v);
    void push_str_multi(std::string const & k,
                        std::vector<std::string> const & v);
  };

  struct 
  printer
  {
    static std::string buf;
    printer();
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
    inline bool symp(std::string const & val) 
    {
      return ttype == basic_io::TOK_SYMBOL && token == val;
    }
    inline void esym(std::string const & val)
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
  };

}

#endif // __BASIC_IO_HH__
