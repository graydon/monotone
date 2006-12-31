// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>

#include "pcrewrap.hh"
#include "sanity.hh"
#include "uri.hh"

using std::string;

static pcre::regex uri_rx(
  "^"
  "(?:([^:/?#]+):)?"            // scheme
  "(?://([^/?#]*))?"            // authority
  "([^?#]*)"                    // path
  "(?:\\?([^#]*))?"             // query
  "(?:#(.*))?"                  // fragment
  "$");

static pcre::regex auth_rx(
  "(?:([^@]+)@)?"               // user
  "(?:"
  "\\[([^\\]]+)]\\]"            // ipv6 host
  "|"
  "([^:/]+)"                    // normal host
  ")"
  "(?::([[:digit:]]+))?");      // port

bool
parse_uri(string const & in, uri & out)
{
  uri u;
  pcre::matches uri_matches;
  
  if (uri_rx.match(in, uri_matches))
    {
      u.scheme = uri_matches[1].str();

      // The "authority" fragment gets a bit more post-processing.
      L(FL("matched URI scheme: '%s'") % u.scheme);

      if (uri_matches[2].matched())
	{
	  string authority = uri_matches[2].str();
	  L(FL("matched URI authority: '%s'") % authority);

          pcre::matches auth_matches;
	  
          N(auth_rx.match(authority, auth_matches),
            F("Invalid URI authority '%s'.\n"
              "Maybe you used an URI in scp-style?") % authority);
      
	  u.user = auth_matches[1].str();
	  u.port = auth_matches[4].str();
	  if (auth_matches[2].matched())	
	    u.host = auth_matches[2].str();
	  else
	    {
	      I(auth_matches[3].matched());
	      u.host = auth_matches[3].str();
	    }
	  L(FL("matched URI user: '%s'") % u.user);
	  L(FL("matched URI host: '%s'") % u.host);
	  L(FL("matched URI port: '%s'") % u.port);

	}

      u.path = uri_matches[3].str();
      u.query = uri_matches[4].str();
      u.fragment = uri_matches[5].str();
      L(FL("matched URI path: '%s'") % u.path);
      L(FL("matched URI query: '%s'") % u.query);
      L(FL("matched URI fragment: '%s'") % u.fragment);
      out = u;
      return true;
    }
  else
    return false;
}



#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"

static void
test_one_uri(string scheme,
	     string user,
	     string ipv6_host,
	     string normal_host,
	     string port,
	     string path,
	     string query,
	     string fragment)
{
  string built;

  if (!scheme.empty())
    built += scheme + ':';

  string host;

  if (! ipv6_host.empty())
    {
      I(normal_host.empty());
      host += '[';
      host += (ipv6_host + ']');
    }
  else
    host = normal_host;

  if (! (user.empty()
	 && host.empty()
	 && port.empty()))
    {
      built += "//";

      if (! user.empty())
	built += (user + '@');

      if (! host.empty())
	built += host;

      if (! port.empty())
	{
	  built += ':';
	  built += port;
	}
    }

  if (! path.empty())
    {
      I(path[0] == '/');
      built += path;
    }

  if (! query.empty())
    {
      built += '?';
      built += query;
    }

  if (! fragment.empty())
    {
      built += '#';
      built += fragment;
    }

  L(FL("testing parse of URI '%s'") % built);
  uri u;
  BOOST_CHECK(parse_uri(built, u));
  BOOST_CHECK(u.scheme == scheme);
  BOOST_CHECK(u.user == user);
  BOOST_CHECK(u.host == host);
  BOOST_CHECK(u.port == port);
  BOOST_CHECK(u.path == path);
  BOOST_CHECK(u.query == query);
  BOOST_CHECK(u.fragment == fragment);
}

UNIT_TEST(uri, uri)
{
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("file", "",       "", "",          "",   "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("http", "graydon", "", "venge.net", "8080", "/foo.cgi", "branch=foo", "tip");
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
