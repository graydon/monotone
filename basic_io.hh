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

namespace basic_io
{

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
  input_source
  {
    size_t line, col;
    std::istream & in;
    int lookahead;
    char c;
    input_source(std::istream & i);
    void peek();
    void eat();
    void advance();
    void err(std::string const & s);
  };

  struct
  tokenizer
  {  
    input_source & in;
    tokenizer(input_source & i);
    token_type get_token(std::string & val);
    void err(std::string const & s);
  };

  struct 
  printer
  {
    size_t offset;
    size_t const step;
    std::ostream & out;
    printer(std::ostream & ost);
    void print_indent();
    void print_bra();
    void print_ket();
    void print_val(char c, std::string const & s);
    void print_hex(std::string const & s);
    void print_str(std::string const & s);
    void print_key(std::string const & s, bool eol = false);
  };

  struct scope
  {
    printer & cp;
    scope(printer & c);
    ~scope();
  };

  struct
  parser
  {
    tokenizer & tok;
    parser(tokenizer & t);

    std::string token;
    token_type ttype;
    
    void advance();
    void err(std::string const & s);
    std::string tt2str(token_type tt);
    void eat(token_type want);
    
    void str();
    void sym();
    void hex();
    void colon();
    void bra();
    void ket();
    
    void str(std::string & v);
    void sym(std::string & v);
    void hex(std::string & v);
    bool symp();
    bool symp(std::string const & val);

    void key(std::string const & val);
  };

}

#endif // __BASIC_IO_HH__
