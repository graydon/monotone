// this is a collection of small grammars and functions to assemble /
// disassemble string and structure forms of the terms therein.

#include <string>

#ifdef BUILD_UNIT_TESTS
// #define BOOST_SPIRIT_DEBUG
#endif

#include <boost/spirit.hpp>
#include <boost/spirit/attribute.hpp>
#include <boost/spirit/utility/regex.hpp>
#include <boost/spirit/phoenix/binders.hpp>

#include <sanity.hh>
#include <vocab.hh>

using namespace std;
using namespace boost::spirit;
using namespace phoenix;

template < typename ResultT >
struct result_closure : boost::spirit::closure<result_closure <ResultT>, ResultT> {
  typedef boost::spirit::closure<result_closure<ResultT>, ResultT> base_t;
  typename base_t::member1 val;
};

struct IDNA_LABEL : public grammar<IDNA_LABEL, result_closure<string>::context_t>
{
  template <typename ScannerT> struct definition 
  {
    // vaguely derived from guidelines in RFC3490,
    // "internationalized domain names for applications"
    rule<ScannerT> main;
    rule<ScannerT> const & start() const { return main; }
    definition(IDNA_LABEL const & self)
    {
      main = 
	regex_p("([xX][nN]--)?[a-zA-Z]([a-zA-Z0-9-]*[a-zA-Z0-9])?")
	[ self.val = construct_<string>(arg1, arg2) ];
    }
  };
} idna_label_g;


struct HOST : public grammar<HOST, result_closure<string>::context_t>
{
  template <typename ScannerT> struct definition 
  {
    // vaguely derived from guidelines in STD3
    rule<ScannerT> main;
    rule<ScannerT> const & start() const { return main; }
    definition(HOST const & self)
    {
      subrule<0> submain;
      subrule<1> ipv4_address;
      main = 
	( 
	 submain = 
	 (
	  (ipv4_address | list_p(idna_label_g, ch_p('.')))
	  [ self.val = construct_<string>(arg1, arg2) ]
	  ),	 
	 ipv4_address = regex_p("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}")
	 ) ;
    }
  };
} host_g;


struct MAIL_LOCAL_NAME : public grammar<MAIL_LOCAL_NAME, result_closure<string>::context_t>
{
  template <typename ScannerT> struct definition 
  {
    // subset of email "local names", vaguely derived from RFC 821 "simple
    // mail transfer protocol"
    rule<ScannerT> main;
    rule<ScannerT> const & start() const { return main; }
    definition(MAIL_LOCAL_NAME const & self)
    {
      subrule<0> submain;
      subrule<1> special;
      subrule<2> control;
      subrule<3> bad;
      subrule<4> c;      
      main =
	(
	 submain = (list_p(+c, ch_p('.')))
	 [ self.val = construct_<string>(arg1, arg2) ],

	 special = chset_p("<>()[]\\.,;:@\""),
	 control = (range_p(0x0, 0x1f) | chset_p("\xf7")),
	 bad = (special | control | ch_p(' ')),
	 c = (range_p(0x0, 0xff) - bad)
	 );
    }  
  };
} mailname_g;
  

struct uri
{
  string proto;
  string user;
  string host;
  string path;
  string group;
  unsigned long port;
  uri() : port(0) {}
};

ostream & operator<<(ostream & ost, uri const & u)
{
  ost << "[uri: '" << u.proto << "' '" << u.user << "' '" << u.host << "' '" 
      << u.path <<  "' '" << u.group <<  "' '" << u.port <<  "']" << endl; 
  return ost;
}

struct URI : public grammar<URI, result_closure<uri>::context_t>
{
  template <typename ScannerT> struct definition 
  {
    // vaguely derived from guidelines in RFC2396,
    // "uniform resource identifiers"
    typedef rule<ScannerT> rule_t;
    rule_t main;
    rule_t const & start() const { return main; }
    definition(URI const & self)
    {
      subrule<0> submain;
      subrule<2> unreserved;
      subrule<3> mark;
      subrule<4> escaped;

      subrule<5> path_char;
      subrule<6> path_segment;
      subrule<7> path_segments;
      subrule<8> path;

      subrule<9> http;
      subrule<10> nntp;
      subrule<11> mailto;

      subrule<12> mailname;
      subrule<13> hostport;
      subrule<14> host;
      subrule<15> port;
      subrule<16> group;

      main = 
	(	 
	 submain = 
	 (  
	  (   http   >> str_p("://") >> hostport >> path)
	  | ( nntp   >> str_p("://") >> hostport >> ch_p('/') >> group)
	  | ( mailto >> str_p(":")   >> mailname >> ch_p('@') >> hostport)	  
	 ),

	 http = str_p("http")     [ bind(&uri::proto)(self.val) = construct_<string>(arg1,arg2) ],
	 nntp = str_p("nntp")     [ bind(&uri::proto)(self.val) = construct_<string>(arg1,arg2) ],
	 mailto = str_p("mailto") [ bind(&uri::proto)(self.val) = construct_<string>(arg1,arg2) ],

	 hostport = (host >> !(ch_p(':') >> port)),
	 host = host_g [ bind(&uri::host)(self.val) = arg1 ],
	 port = uint_p [ bind(&uri::port)(self.val) = arg1 ],

	 mark = chset_p("-_.!~*'()"),
 	 unreserved = (chset_p("a-zA-Z0-9") | mark),
 	 escaped = regex_p("%[[:xdigit:]]{2}"),	 
 	 path_char = unreserved | escaped | chset_p(":@&=+$,"),
 	 path_segment = (+path_char),
 	 path_segments = (list_p(path_segment, ch_p('/'))),
 	 path = (ch_p('/') >> path_segments) 
	 [ bind(&uri::path)(self.val) = construct_<string>(arg1,arg2) ],

 	 group = (list_p(idna_label_g, ch_p('.'))) 
 	 [ bind(&uri::group)(self.val) = construct_<string>(arg1,arg2) ],

	 mailname = mailname_g [bind(&uri::user)(self.val) = arg1]
	 );
    }
  };
} uri_g;



bool parse_url(url const & u,
	       string & proto,
	       string & user,
	       string & host,	       
	       string & path,
	       string & group,
	       unsigned long & port)
{
  // http://host:port/path.cgi/group
  // nntp://host:port/group
  // mailto:user@host:port


  uri ustruct;  
  bool parsed_ok = parse(u().c_str(), uri_g[var(ustruct) = arg1]).full;
    
  if (parsed_ok)
    {
      proto = ustruct.proto;
      user = ustruct.user;
      host = ustruct.host;
      path = ustruct.path;
      group = ustruct.group;
      port = ustruct.port;

      if (proto == "http")
	{
	  string::size_type gpos = path.rfind('/');
	  if (gpos == string::npos || gpos == path.size() - 1 || gpos == 0)
	    return false;
	  group = path.substr(gpos+1);
	  path = path.substr(0,gpos);
	}
      
      if (proto == "http" && port == 0)
	port = 80;
      else if (proto == "nntp" && port == 0)
	port = 119;
      else if (proto == "mailto" && port == 0)
	port= 25;
    }
  
  L(F("parsed URL: proto '%s', user '%s', host '%s', port '%d', path '%s', group '%s'\n")
    % proto % user % host % port % path % group);

  return parsed_ok;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static bool url_parses(string u, 
		       string xproto,
		       string xuser,
		       string xhost,
		       string xpath,
		       string xgroup,
		       unsigned long xport)
{
  url uu(u);
  string proto, user, host, path, group;
  unsigned long port = 0;

  L(F("trying to parse %s\n") % u);

  parse_url(uu, proto, user, host, path, group, port);

#define CHECK(z) if (z != (x ## z)) { \
  cerr << "parsed url '" << u << "' wrong: " \
  "got " << (#z) << " = '" << z << "', expected '" << (x ## z) << "'" \
  << endl; return false; \
}
  CHECK(proto);
  CHECK(user);
  CHECK(host);
  CHECK(path);
  CHECK(group);
  CHECK(port);
#undef CHECK

  return true;
}

static void test_legal_urls()
{
  // positive tests
  BOOST_CHECK(url_parses("http://www.gurgle.com/depot.cgi/foo.foo", 
	      "http", "", "www.gurgle.com", "/depot.cgi", "foo.foo", 80));

  BOOST_CHECK(url_parses("nntp://news.isp.com/my.group.is.good", 
	      "nntp", "", "news.isp.com", "", "my.group.is.good", 119));

  BOOST_CHECK(url_parses("mailto:super-list@mail.yoohoo.com", 
	      "mailto", "super-list", "mail.yoohoo.com", "", "", 25));

  BOOST_CHECK(url_parses("http://www.gurgle.com:1234/~someone/depot.cgi/foo.bleh", 
	      "http", "", "www.gurgle.com", "/~someone/depot.cgi", "foo.bleh", 1234));

  BOOST_CHECK(url_parses("nntp://news.isp.com:1221/my.group.is.good", 
	      "nntp", "", "news.isp.com", "", "my.group.is.good", 1221));

  BOOST_CHECK(url_parses("mailto:super-list@mail.yoohoo.com:3345", 
	      "mailto", "super-list", "mail.yoohoo.com", "", "", 3345));

}

static void test_illegal_urls()
{
}

void add_url_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_legal_urls));
  suite->add(BOOST_TEST_CASE(&test_illegal_urls));
}


#endif // BUILD_UNIT_TESTS
