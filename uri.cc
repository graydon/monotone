// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "uri.hh"

using std::string;
typedef string::size_type stringpos;

static void
parse_authority(string const & in, uri & u)
{
  L(FL("matched URI authority: '%s'") % in);

  stringpos p = 0;

  // First, there might be a user: one or more non-@ characters followed
  // by an @.
  stringpos user_end = in.find('@', p);
  if (user_end != 0 && user_end < in.size())
    {
      u.user.assign(in, 0, user_end);
      p = user_end + 1;
      L(FL("matched URI user: '%s'") % u.user);
    }

  // The next thing must either be an ipv6 address, which has the form
  // \[ [0-9A-Za-z:]+ \] and we discard the square brackets, or some other
  // sort of hostname, [^:]+.  (A host-part can be terminated by /, ?, or #
  // as well as :, but our caller has taken care of that.)
  if (p < in.size() && in.at(p) == '[')
    {
      p++;
      stringpos ipv6_end = in.find(']', p);
      N(ipv6_end != string::npos,
        F("IPv6 address in URI has no closing ']'"));

      u.host.assign(in, p, ipv6_end - p);
      p = ipv6_end + 1;
      L(FL("matched URI host (IPv6 address): '%s'") % u.host);
    }
  else
    {
      stringpos host_end = in.find(':', p);
      u.host.assign(in, p, host_end - p);
      p = host_end;
      L(FL("matched URI host: '%s'") % u.host);
    }

  // Finally, if the host-part was ended by a colon, there is a port number
  // following, which must consist entirely of digits.
  if (p < in.size() && in.at(p) == ':')
    {
      p++;
      N(p < in.size(),
        F("explicit port-number specification in URI has no digits"));

      N(in.find_first_not_of("0123456789", p) == string::npos,
        F("explicit port-number specification in URI contains nondigits"));

      u.port.assign(in, p, string::npos);
      L(FL("matched URI port: '%s'") % u.port);
    }
}

void
parse_uri(string const & in, uri & u)
{
  u.scheme.clear();
  u.user.clear();
  u.host.clear();
  u.port.clear();
  u.path.clear();
  u.query.clear();
  u.fragment.clear();

  stringpos p = 0;

  // This is a simplified URI grammar. It does the basics.

  // First there may be a scheme: one or more characters which are not
  // ":/?#", followed by a colon.
  stringpos scheme_end = in.find_first_of(":/?#", p);

  if (scheme_end != 0 && scheme_end < in.size() && in.at(scheme_end) == ':')
    {
      u.scheme.assign(in, p, scheme_end - p);
      p = scheme_end + 1;
      L(FL("matched URI scheme: '%s'") % u.scheme);
    }

  // Next, there may be an authority: "//" followed by zero or more
  // characters which are not "/?#".

  if (p + 1 < in.size() && in.at(p) == '/' && in.at(p+1) == '/')
    {
      p += 2;
      stringpos authority_end = in.find_first_of("/?#", p);
      if (authority_end != p)
        {
          parse_authority(string(in, p, authority_end - p), u);
          p = authority_end;
        }
      if (p >= in.size())
        return;
    }

  // Next, a path: zero or more characters which are not "?#".
  {
    stringpos path_end = in.find_first_of("?#", p);
    u.path.assign(in, p, path_end - p);
    p = path_end;
    L(FL("matched URI path: '%s'") % u.path);
    if (p >= in.size())
      return;
  }

  // Next, perhaps a query: "?" followed by zero or more characters
  // which are not "#".
  if (in.at(p) == '?')
    {
      p++;
      stringpos query_end = in.find('#', p);
      u.query.assign(in, p, query_end - p);
      p = query_end;
      L(FL("matched URI query: '%s'") % u.query);
      if (p >= in.size())
        return;
    }

  // Finally, if there is a '#', then whatever comes after it in the string
  // is a fragment identifier.
  if (in.at(p) == '#')
    {
      u.fragment.assign(in, p + 1, string::npos);
      L(FL("matched URI fragment: '%s'") % u.fragment);
    }
}

string
urldecode(string const & in)
{
  string out;
  
  for (string::const_iterator i = in.begin(); i != in.end(); ++i)
    {
      if (*i != '%')
        out += *i;
      else
        {
          char d1, d2;
          ++i;
          E(i != in.end(), F("Bad URLencoded string '%s'") % in);
          d1 = *i;
          ++i;
          E(i != in.end(), F("Bad URLencoded string '%s'") % in);
          d2 = *i;
          
          char c = 0;
          switch(d1)
            {
            case '0': c += 0; break;
            case '1': c += 1; break;
            case '2': c += 2; break;
            case '3': c += 3; break;
            case '4': c += 4; break;
            case '5': c += 5; break;
            case '6': c += 6; break;
            case '7': c += 7; break;
            case '8': c += 8; break;
            case '9': c += 9; break;
            case 'a': case 'A': c += 10; break;
            case 'b': case 'B': c += 11; break;
            case 'c': case 'C': c += 12; break;
            case 'd': case 'D': c += 13; break;
            case 'e': case 'E': c += 14; break;
            case 'f': case 'F': c += 15; break;
            default: E(false, F("Bad URLencoded string '%s'") % in);
            }
          c *= 16;
          switch(d2)
            {
            case '0': c += 0; break;
            case '1': c += 1; break;
            case '2': c += 2; break;
            case '3': c += 3; break;
            case '4': c += 4; break;
            case '5': c += 5; break;
            case '6': c += 6; break;
            case '7': c += 7; break;
            case '8': c += 8; break;
            case '9': c += 9; break;
            case 'a': case 'A': c += 10; break;
            case 'b': case 'B': c += 11; break;
            case 'c': case 'C': c += 12; break;
            case 'd': case 'D': c += 13; break;
            case 'e': case 'E': c += 14; break;
            case 'f': case 'F': c += 15; break;
            default: E(false, F("Bad URLencoded string '%s'") % in);
            }
          out += c;
        }
    }
  
  return out;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

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
  UNIT_TEST_CHECK_NOT_THROW(parse_uri(built, u), informative_failure);
  UNIT_TEST_CHECK(u.scheme == scheme);
  UNIT_TEST_CHECK(u.user == user);
  if (!normal_host.empty())
    UNIT_TEST_CHECK(u.host == normal_host);
  else
    UNIT_TEST_CHECK(u.host == ipv6_host);
  UNIT_TEST_CHECK(u.port == port);
  UNIT_TEST_CHECK(u.path == path);
  UNIT_TEST_CHECK(u.query == query);
  UNIT_TEST_CHECK(u.fragment == fragment);
}

UNIT_TEST(uri, basic)
{
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "fe:00:01::04:21", "", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("file", "",       "", "",          "",   "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("http", "graydon", "", "venge.net", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http", "graydon", "", "192.168.0.104", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http", "graydon", "fe:00:01::04:21", "", "8080", "/foo.cgi", "branch=foo", "tip");
}

UNIT_TEST(uri, bizarre)
{
  test_one_uri("", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/graydon@venge.net:22/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "", "", "", "", "/graydon@venge.net:22/tmp/foo.mtn", "", "");
}

UNIT_TEST(uri, invalid)
{
  uri u;

  UNIT_TEST_CHECK_THROW(parse_uri("http://[f3:03:21/foo/bar", u), informative_failure);
  UNIT_TEST_CHECK_THROW(parse_uri("http://example.com:/foo/bar", u), informative_failure);
  UNIT_TEST_CHECK_THROW(parse_uri("http://example.com:1a4/foo/bar", u), informative_failure);
}

UNIT_TEST(uri, urldecode)
{
  UNIT_TEST_CHECK(urldecode("foo%20bar") == "foo bar");
  UNIT_TEST_CHECK(urldecode("%61") == "a");
  UNIT_TEST_CHECK_THROW(urldecode("%xx"), informative_failure);
  UNIT_TEST_CHECK_THROW(urldecode("%"), informative_failure);
  UNIT_TEST_CHECK_THROW(urldecode("%5"), informative_failure);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
