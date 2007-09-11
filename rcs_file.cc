// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include <fstream>
#include "vector.hh"

#ifdef WIN32
#include <windows.h>
#endif

#ifdef HAVE_MMAP
#ifdef sun
#define _XPG4_2
#endif
#include <sys/mman.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "rcs_file.hh"
#include "sanity.hh"

using std::ifstream;
using std::ios_base;
using std::istream;
using std::string;

#ifdef HAVE_MMAP
struct
file_handle
{
  string const & filename;
  off_t length;
  int fd;
  file_handle(string const & fn) :
    filename(fn),
    length(0),
    fd(-1)
    {
      struct stat st;
      if (stat(fn.c_str(), &st) == -1)
        throw oops("stat of " + filename + " failed");
      length = st.st_size;
      fd = open(filename.c_str(), O_RDONLY);
      if (fd == -1)
        throw oops("open of " + filename + " failed");
    }
  ~file_handle()
    {
      if (close(fd) == -1)
        throw oops("close of " + filename + " failed");
    }
};
struct file_source
{
  string const & filename;
  int fd;
  off_t length;
  off_t pos;
  void * mapping;
  bool good()
  {
    return pos < length;
  }
  int peek()
  {
    if (pos >= length)
      return EOF;
    else
      return reinterpret_cast<char const *>(mapping)[pos];
  }
  bool get(char & c)
  {
    c = peek();
    if (good())
      ++pos;
    return good();
  }
  file_source(string const & fn,
              int f,
              off_t len) :
    filename(fn),
    fd(f),
    length(len),
    pos(0),
    mapping(NULL)
  {
    mapping = mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED)
      throw oops("mmap of " + filename + " failed");
  }
  ~file_source()
  {
    if (munmap(mapping, length) == -1)
      throw oops("munmapping " + filename + " failed, after reading RCS file");
  }
};
#elif defined(WIN32)
struct
file_handle
{
  string const & filename;
  off_t length;
  HANDLE fd;
  file_handle(string const & fn) :
    filename(fn),
    length(0),
    fd(NULL)
    {
      struct stat st;
      if (stat(fn.c_str(), &st) == -1)
        throw oops("stat of " + filename + " failed");
      length = st.st_size;
      fd = CreateFile(fn.c_str(),
                      GENERIC_READ,
                      FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING, 0, NULL);
      if (fd == NULL)
        throw oops("open of " + filename + " failed");
    }
  ~file_handle()
    {
      if (CloseHandle(fd)==0)
        throw oops("close of " + filename + " failed");
    }
};

struct
file_source
{
  string const & filename;
  HANDLE fd,map;
  off_t length;
  off_t pos;
  void * mapping;
  bool good()
  {
    return pos < length;
  }
  int peek()
  {
    if (pos >= length)
      return EOF;
    else
      return reinterpret_cast<char const *>(mapping)[pos];
  }
  bool get(char & c)
  {
    c = peek();
    if (good())
      ++pos;
    return good();
  }
  file_source(string const & fn,
              HANDLE f,
              off_t len) :
    filename(fn),
    fd(f),
    length(len),
    pos(0),
    mapping(NULL)
  {
    map = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL);
    if (map==NULL)
      throw oops("CreateFileMapping of " + filename + " failed");
    mapping = MapViewOfFile(map, FILE_MAP_READ, 0, 0, len);
    if (mapping==NULL)
      throw oops("MapViewOfFile of " + filename + " failed");
  }
  ~file_source()
  {
    if (UnmapViewOfFile(mapping)==0)
      throw oops("UnmapViewOfFile of " + filename + " failed");
    if (CloseHandle(map)==0)
      throw oops("CloseHandle of " + filename + " failed");
  }
};
#else
// no mmap at all
typedef istream file_source;
#endif

typedef enum
  {
    TOK_STRING,
    TOK_SYMBOL,
    TOK_NUM,
    TOK_SEMI,
    TOK_COLON,
    TOK_NONE
  }
token_type;

static inline void
adv(char i, size_t & line, size_t & col)
{
  if (i == '\n')
    {
      col = 0;
      ++line;
    }
  else
    ++col;
}

static token_type
get_token(file_source & ist,
          string & str,
	  size_t & line,
	  size_t & col)
{
  bool saw_idchar = false;
  int i = ist.peek();
  char c;
  str.clear();

  // eat leading whitespace
  while (true)
    {
      if (i == EOF)
        return TOK_NONE;
      adv(i, line, col);
      if (!isspace(i))
        break;
      ist.get(c);
      i = ist.peek();
    }

  switch (i)
    {
    case ';':
      ist.get(c);
      ++col;
      return TOK_SEMI;
      break;

    case ':':
      ist.get(c);
      ++col;
      return TOK_COLON;
      break;

    case '@':
      ist.get(c);
      ++col;
      while (ist.get(c))
        {
          if (c == '@')
            {
              if (ist.peek() == '@')
                { ist.get(c); str += c; ++col; }
              else
                break;
            }
          else
	    {
	      adv(i, line, col);
	      str += c;
	    }
        }
      return TOK_STRING;
      break;

    default:
      while (ist.good()
             && i != ';'
             && i != ':'
             && !isspace(i))
        {
          ist.get(c);
	  ++col;
          if (! isdigit(c) && c != '.')
            saw_idchar = true;
          str += c;
          i = ist.peek();
        }
      break;
    }

  if (str.empty())
    return TOK_NONE;
  else if (saw_idchar)
    return TOK_SYMBOL;
  else
    return TOK_NUM;
}

struct parser
{
  file_source & ist;
  rcs_file & r;
  string token;
  token_type ttype;

  size_t line, col;

  parser(file_source & s,
         rcs_file & r)
    : ist(s), r(r), line(1), col(1)
  {}

  string tt2str(token_type tt)
  {
    switch (tt)
      {
      case TOK_STRING:
        return "TOK_STRING";
      case TOK_SYMBOL:
        return "TOK_SYMBOL";
      case TOK_NUM:
        return "TOK_NUM";
      case TOK_SEMI:
        return "TOK_SEMI";
      case TOK_COLON:
        return "TOK_COLON";
      case TOK_NONE:
        return "TOK_NONE";
      }
    return "TOK_UNKNOWN";
  }

  void advance()
  {
    ttype = get_token(ist, token, line, col);
    // cerr << tt2str(ttype) << ": " << token << '\n';
  }

  bool nump() { return ttype == TOK_NUM; }
  bool strp() { return ttype == TOK_STRING; }
  bool symp() { return ttype == TOK_SYMBOL; }
  bool symp(string const & val)
  {
    return ttype == TOK_SYMBOL && token == val;
  }
  void eat(token_type want)
  {
    if (ttype != want)
      throw oops((F("parse failure %d:%d: expecting %s, got %s with value '%s'")
		  % line % col % tt2str(want) % tt2str(ttype) % token).str());
    advance();
  }

  // basic "expect / extract" functions

  void str(string & v) { v = token; eat(TOK_STRING); }
  void str() { eat(TOK_STRING); }
  void sym(string & v) { v = token; eat(TOK_SYMBOL); }
  void sym() { eat(TOK_SYMBOL); }
  void num(string & v) { v = token; eat(TOK_NUM); }
  void num() { eat(TOK_NUM); }
  void semi() { eat(TOK_SEMI); }
  void colon() { eat(TOK_COLON); }
  void expect(string const & expected)
  {
    string tmp;
    if (!symp(expected))
      throw oops((F("parse failure %d:%d: expecting word '%s'")
		  % line % col % expected).str());
    advance();
  }

  bool wordp()
  {
    return (ttype == TOK_STRING
            || ttype == TOK_SYMBOL
            || ttype == TOK_NUM
            || ttype == TOK_COLON);
  }
  void word()
  {
    if (!wordp())
      throw oops((F("parse failure %d:%d: expecting word")
		  % line % col).str());
    advance();
  }

  void parse_newphrases(string const & terminator)
  {
    while(symp() && !symp(terminator))
      {
        sym();
        while (wordp()) word();
        semi();
      }
  }

  void parse_admin()
  {
    expect("head"); num(r.admin.head); semi();
    if (symp("branch")) { sym(r.admin.branch); if (nump()) num(); semi(); }
    expect("access"); while(symp()) { sym(); } semi();
    expect("symbols");

    // "man rcsfile" lies: there are real files in the wild which use
    // num tokens as the key value in a symbols entry. for example
    // "3.1:1.1.0.2" is a real sym:num specification, despite "3.1"
    // being a num itself, not a sym.

    while(symp() || nump())
      {
        string stmp, ntmp;
	if (symp())
	  {
	    sym(stmp); colon(); num(ntmp);
	  }
	else
	  {
	    num(stmp); colon(); num(ntmp);
	  }
        r.admin.symbols.insert(make_pair(ntmp, stmp));
      }
    semi();
    expect("locks"); while(symp()) { sym(); colon(); num(); } semi();
    if (symp("strict")) { sym(); semi(); }
    if (symp("comment")) { sym(); if (strp()) { str(); } semi(); }
    if (symp("expand")) { sym(); if (strp()) { str(); } semi(); }
    parse_newphrases("");
  }

  void parse_deltas()
  {
    while (nump())
      {
        rcs_delta d;
        num(d.num);
        expect("date"); num(d.date); semi();
        expect("author"); sym(d.author); semi();
        expect("state"); if (symp()) sym(d.state); semi();
        expect("branches");
        while(nump())
          {
            string tmp;
            num(tmp);
            d.branches.push_back(tmp);
          }
        semi();
        expect("next"); if (nump()) num(d.next); semi();
        parse_newphrases("desc");
        r.push_delta(d);
      }
  }

  void parse_desc()
  {
    expect("desc"); str();
  }

  void parse_deltatexts()
  {
    while(nump())
      {
        rcs_deltatext d;
        num(d.num);
        expect("log"); str(d.log);
        parse_newphrases("text");
        expect("text"); str(d.text);
        r.push_deltatext(d);
      }
  }

  void parse_file()
  {
    advance();
    parse_admin();
    parse_deltas();
    parse_desc();
    parse_deltatexts();
    eat(TOK_NONE);
  }
};

void
parse_rcs_file(string const & filename, rcs_file & r)
{
#if defined(HAVE_MMAP) || defined(WIN32)
      file_handle handle(filename);
      file_source ifs(filename, handle.fd, handle.length);
#else
      ifstream ifs(filename.c_str());
      ifs.unsetf(ios_base::skipws);
#endif
      parser p(ifs, r);
      p.parse_file();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
