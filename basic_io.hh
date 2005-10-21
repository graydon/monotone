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

namespace basic_io
{

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
    std::istream & in;
    std::string name;
    int lookahead;
    char c;
    input_source(std::istream & i, std::string const & nm)
      : line(1), col(1), in(i), name(nm), lookahead(0), c('\0')
    {}
    inline void peek() { lookahead = in.peek(); }
    inline void eat()
    {
      in.get(c);
      ++col;
      if (c == '\n')
        {
          col = 1;
          ++line;
        }
    }
    inline void advance() { eat(); peek(); }
    void err(std::string const & s);
  };

  struct
  tokenizer
  {  
    input_source & in;
    tokenizer(input_source & i) : in(i) {}

    inline token_type get_token(std::string & val)
    {
      val.clear();
      val.reserve(80);
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
    void push_str_pair(std::string const & k, std::string const & v);
    void push_file_pair(std::string const & k, file_path const & v);
    void push_str_multi(std::string const & k,
                        std::vector<std::string> const & v);
  };

  struct 
  printer
  {
    bool empty_output;
    std::ostream & out;
    printer(std::ostream & ost);
    void print_stanza(stanza const & st);
  };

  struct
  parser
  {
    tokenizer & tok;
    parser(tokenizer & t) : tok(t) 
    {
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
