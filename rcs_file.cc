// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/lexical_cast.hpp>
#include <boost/spirit.hpp>
#include <boost/spirit/attribute.hpp>
#include <boost/spirit/phoenix/binders.hpp>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rcs_file.hh"
#include "sanity.hh"

#include "config.h"
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

using namespace std;
using namespace boost::spirit;
using boost::lexical_cast;
using namespace phoenix;

struct 
rcs_admin_closure : 
boost::spirit::closure<rcs_admin_closure, rcs_admin>
{ member1 val; };

struct 
rcs_delta_closure : 
boost::spirit::closure<rcs_delta_closure, rcs_delta>
{ member1 val; };

struct 
rcs_symbol_closure : 
boost::spirit::closure<rcs_symbol_closure, rcs_symbol>
{ member1 val; };

struct 
rcs_desc_closure : 
boost::spirit::closure<rcs_desc_closure, string>
{ member1 val; };

struct 
rcs_deltatext_closure : 
boost::spirit::closure<rcs_deltatext_closure, rcs_deltatext>
{ member1 val; };

struct 
rcs_file_closure : 
boost::spirit::closure<rcs_file_closure, rcs_file>
{ member1 val; };

struct 
string_closure : 
boost::spirit::closure<string_closure, string>
{ member1 val; };


string 
unescape_string(char const * ch, char const * end) 
{
  static string str;
  str.reserve(static_cast<int>(end - ch));
  str.clear();
  if (*ch != '@')
    throw oops("parser reported string without leading @");
  while(++ch < end)
    {
      if (*ch == '@') 
	{
	  if (ch + 1 == end)
	    break;
	  if (*(++ch) != '@')
	    throw oops("parser reported a single @ before end of string");
	}
      str += *ch;
    }
  return string(str);
}

struct 
rcsfile_grammar : 
  public grammar<rcsfile_grammar, rcs_file_closure::context_t>
{
  template <typename ScannerT>
  struct definition
  {
    
    // phrases with closures
    subrule<0, rcs_file_closure      ::context_t> rcstext;
    subrule<1, rcs_admin_closure     ::context_t> admin;    
    subrule<2, rcs_symbol_closure    ::context_t> symbol;
    subrule<3, rcs_desc_closure      ::context_t> desc;
    subrule<4, rcs_delta_closure     ::context_t> delta;
    subrule<5, rcs_deltatext_closure ::context_t> deltatext;

    // tokens we want to record in strings
    subrule<6,  string_closure        ::context_t> str;
    subrule<7,  string_closure        ::context_t> sym; 
    subrule<8,  string_closure        ::context_t> num;
    subrule<9,  string_closure        ::context_t> id; 
    subrule<10, string_closure        ::context_t> word;

    // non-recorded tokens
    subrule<11> semi;
    subrule<12> col;
    subrule<13> newphrase;

    // lexemes
    subrule<14> idchar;
    subrule<15> numchs;

    // the root node
    rule<ScannerT> top;    
    rule<ScannerT> const & 
    start() const { return top; }

    definition(rcsfile_grammar const & self) 
    {

      top = 
	(
	 rcstext = 
	 (admin[ bind(&rcs_file::admin)(self.val) = arg1 ] 
	  >> (* (delta[bind(&rcs_file::push_delta)(self.val,arg1)]) 
	      | (*newphrase >> 
		 *(delta[bind(&rcs_file::push_delta)(self.val,arg1)]) ))
	  >> ( desc  | (*newphrase >>  desc  ))
	  >> *(deltatext[bind(&rcs_file::push_deltatext)(self.val,arg1)])
	  >> lexeme_d[*ch_p('\n')]),
	 	 
	 admin = 
	 str_p("head")         >>  num [bind(&rcs_admin::head   )(admin.val) = arg1]  >> semi  >>
	 !(str_p("branch")     >>  num [bind(&rcs_admin::branch )(admin.val) = arg1]  >> semi) >>
	 str_p("access")       >> *(id [bind(&rcs_admin::access )(admin.val) = arg1]) >> semi  >>
	 str_p("symbols")      >> *(symbol [bind(&rcs_admin::push_symbol)(admin.val,arg1)]) >> semi  >>
	 str_p("locks")        >> *(id  >> col >> num) >> semi  >> 
	                            !(str_p("strict")  >> semi) >>
	 !(str_p("comment")    >> str [bind(&rcs_admin::comment )(admin.val) = arg1] >> semi) >>
	 !(str_p("expand")     >> str [bind(&rcs_admin::expand  )(admin.val) = arg1] >> semi),

	 symbol = sym[bind(&rcs_symbol::name   )(symbol.val) = arg1] >> col
	       >> num[bind(&rcs_symbol::version)(symbol.val) = arg1],
	 
	 delta = 
	 num                       [bind(&rcs_delta::num)   (delta.val) = arg1]       >>
	 str_p("date")     >> num  [bind(&rcs_delta::date)  (delta.val) = arg1]       >> semi >>
	 str_p("author")   >> id   [bind(&rcs_delta::author)(delta.val) = arg1]       >> semi >>
	 str_p("state")    >> !(id [bind(&rcs_delta::state) (delta.val) = arg1])      >> semi >>
	 str_p("branches") >> *(num[bind(&rcs_delta::push_branch)(delta.val,arg1)])   >> semi >>
	 str_p("next")     >> !(num[bind(&rcs_delta::next)  (delta.val) = arg1])      >> semi,
	 
	 desc =  str_p("desc") >> str[desc.val = arg1 ],
	 
	 deltatext =
	 num                  [bind(&rcs_deltatext::num)  (deltatext.val)  = arg1 ]   >>
	 str_p("log")  >> str [bind(&rcs_deltatext::log)  (deltatext.val)  = arg1 ]   >>
	 (str_p("text") | (*newphrase >> str_p("text"))) >> 
	 str                  [bind(&rcs_deltatext::text) (deltatext.val)  = arg1 ]
	 ,
	 
	 // phrase-level phrase rule definitions:
	 word      = id | num | str | semi,
	 newphrase = id >> *word >> semi,
	 	 
	 // these are to be called as character-level rules (in lexeme_d[...] blocks)
	 idchar  = graph_p - chset<>("@,.:;@"),
	 numchs  = +(digit_p | ch_p('.')),
	 
	 // these are lexical units which can be called from outside lexeme_d[...] blocks
	 semi    = ch_p(';'),
	 col     = ch_p(':'),

	 sym     = lexeme_d[  *digit_p >> idchar >> *(idchar | digit_p)  ]
	                   [  sym.val = construct_<string>(arg1,arg2)    ],

	 num     = lexeme_d[  numchs                                     ]
	                   [  num.val = construct_<string>(arg1,arg2)    ],

	 id      = lexeme_d[  !numchs >> idchar >> *(idchar | numchs)    ]
	                   [  id.val = construct_<string>(arg1,arg2)     ],

 	 str     = lexeme_d[  ch_p('@') 
  			      >> 
  			      *((chset<>(anychar_p) - chset<>('@')) 
  				| (ch_p('@') >> ch_p('@'))) 
  			      >> 
  			      ch_p('@')                                  ]
	                   [ str.val = bind(&unescape_string)(arg1,arg2) ]
	  );
    }
  };
};

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

struct 
mmapped_handle
{
  string const & filename;
  int fd;
  off_t length;
  void * mapping;
  mmapped_handle(string const & fn, 
		int f, 
		off_t len) : 
    filename(fn),
    fd(f),
    length(len),
    mapping(NULL)
    {
      mapping = mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0);
      if (mapping == MAP_FAILED) 
	throw oops("mmap of " + filename + " failed");
    }
  ~mmapped_handle()
    {
      if (munmap(mapping, length) == -1)
	throw oops("munmapping " + filename + " failed, after reading RCS file");
    }
};

void 
parse_rcs_file(string const & filename, rcs_file & r)
{

  file_handle file(filename);
  mmapped_handle mm(filename, file.fd, file.length);  

  char const * begin = reinterpret_cast<char const *>(mm.mapping);
  char const * end = begin + file.length;

  rcsfile_grammar gram;
  rcs_file rcs;
  parse_info<char const *> info = 
    parse(begin, end,
	  gram[var(rcs) = arg1],
	  space_p);

  if (info.hit && info.full)
    r = rcs;
  else
    throw oops( "parse of RCS file " + filename + " failed"
		+ ", info.hit = " + lexical_cast<string>(info.hit)
		+ ", info.full = " + lexical_cast<string>(info.full)
		+ ", stopped at = " + lexical_cast<string>(info.stop - begin));
}

