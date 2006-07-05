// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "sanity.hh"
#include "globish.hh"

using std::string;
using std::vector;

using boost::regex_match;

// this converts a globish pattern to a regex.  The regex should be usable by
// the Boost regex library operating in default mode, i.e., it should be a
// valid ECMAscript regex.
//
// Pattern tranformation:
//
// - As a special case, the empty pattern is translated to "$.^", which cannot
//   match any string.
//
// - Any character except those described below are copied as they are.
// - The backslash (\) escapes the following character.  The escaping
//   backslash is copied to the regex along with the following character.
// - * is transformed to .* in the regex.
// - ? is transformed to . in the regex.
// - { is transformed to ( in the regex
// - } is transformed to ) in the regex
// - , is transformed to | in the regex, if within { and }
// - ^ is escaped unless it comes directly after an unescaped [.
// - ! is transformed to ^ in the regex if it comes directly after an
//   unescaped [.
// - ] directly following an unescaped [ is escaped.
static void
maybe_quote(char c, string & re)
{
  if (!(isalnum(c) || c == '_'))
    {
      re += '\\';
    }
  re += c;
}

static void
checked_globish_to_regex(string const & glob, string & regex)
{
  int in_braces = 0;            // counter for levels if {}

  regex.clear();
  regex.reserve(glob.size() * 2);

  L(FL("checked_globish_to_regex: input = '%s'") % glob);

  if (glob == "")
    {
      regex = "$.^";
      // and the below loop will do nothing
    }
  for (string::const_iterator i = glob.begin(); i != glob.end(); ++i)
    {
      char c = *i;

      N(in_braces < 5, F("braces nested too deep in pattern '%s'") % glob);

      switch(c)
        {
        case '*':
          regex += ".*";
          break;
        case '?':
          regex += '.';
          break;
        case '{':
          in_braces++;
          regex += '(';
          break;
        case '}':
          N(in_braces != 0,
            F("trying to end a brace expression in a glob when none is started"));
          regex += ')';
          in_braces--;
          break;
        case ',':
          if (in_braces > 0)
            regex += '|';
          else
            maybe_quote(c, regex);
          break;
        case '\\':
          N(++i != glob.end(), F("pattern '%s' ends with backslash") % glob);
          maybe_quote(*i, regex);
          break;
        default:
          maybe_quote(c, regex);
          break;
        }
    }

  N(in_braces == 0,
    F("run-away brace expression in pattern '%s'") % glob);

  L(FL("checked_globish_to_regex: output = '%s'") % regex);
}

void
combine_and_check_globish(vector<utf8> const & patterns, utf8 & pattern)
{
  string p;
  if (patterns.size() > 1)
    p += '{';
  bool first = true;
  for (vector<utf8>::const_iterator i = patterns.begin(); i != patterns.end(); ++i)
    {
      string tmp;
      // run for the checking it does
      checked_globish_to_regex((*i)(), tmp);
      if (!first)
        p += ',';
      first = false;
      p += (*i)();
    }
  if (patterns.size() > 1)
    p += '}';
  pattern = utf8(p);
}

globish_matcher::globish_matcher(utf8 const & include_pat, utf8 const & exclude_pat)
{
  string re;
  checked_globish_to_regex(include_pat(), re);
  r_inc = re;
  checked_globish_to_regex(exclude_pat(), re);
  r_exc = re;
}

bool
globish_matcher::operator()(string const & s)
{
  // regex_match may throw a runtime_error, if the regex turns out to be
  // really pathological
  bool inc_match = regex_match(s, r_inc);
  bool exc_match = regex_match(s, r_exc);
  bool result = inc_match && !exc_match;
  L(FL("matching '%s' against '%s' excluding '%s': %s, %s: %s")
    % s % r_inc % r_exc
    % (inc_match ? "included" : "not included")
    % (exc_match ? "excluded" : "not excluded")
    % (result ? "matches" : "does not match"));
  return result;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void
checked_globish_to_regex_test()
{
  string pat;

  checked_globish_to_regex("*", pat);
  BOOST_CHECK(pat == ".*");
  checked_globish_to_regex("?", pat);
  BOOST_CHECK(pat == ".");
  checked_globish_to_regex("{a,b,c}d", pat);
  BOOST_CHECK(pat == "(a|b|c)d");
  checked_globish_to_regex("foo{a,{b,c},?*}d", pat);
  BOOST_CHECK(pat == "foo(a|(b|c)|..*)d");
  checked_globish_to_regex("\\a\\b\\|\\{\\*", pat);
  BOOST_CHECK(pat == "ab\\|\\{\\*");
  checked_globish_to_regex(".+$^{}", pat);
  BOOST_CHECK(pat == "\\.\\+\\$\\^()");
  checked_globish_to_regex(",", pat);
  // we're very conservative about metacharacters, and quote all
  // non-alphanumerics, hence the backslash
  BOOST_CHECK(pat == "\\,");
  checked_globish_to_regex("\\.\\+\\$\\^\\(\\)", pat);
  BOOST_CHECK(pat == "\\.\\+\\$\\^\\(\\)");

  BOOST_CHECK_THROW(checked_globish_to_regex("foo\\", pat), informative_failure);
  BOOST_CHECK_THROW(checked_globish_to_regex("{foo", pat), informative_failure);
  BOOST_CHECK_THROW(checked_globish_to_regex("{foo,bar{baz,quux}", pat), informative_failure);
  BOOST_CHECK_THROW(checked_globish_to_regex("foo}", pat), informative_failure);
  BOOST_CHECK_THROW(checked_globish_to_regex("foo,bar{baz,quux}}", pat), informative_failure);
  BOOST_CHECK_THROW(checked_globish_to_regex("{{{{{{{{{{a,b},c},d},e},f},g},h},i},j},k}", pat), informative_failure);
}

static void
combine_and_check_globish_test()
{
  vector<utf8> s;
  s.push_back(utf8("a"));
  s.push_back(utf8("b"));
  s.push_back(utf8("c"));
  utf8 combined;
  combine_and_check_globish(s, combined);
  BOOST_CHECK(combined() == "{a,b,c}");
}

static void
globish_matcher_test()
{
  {
    globish_matcher m(utf8("{a,b}?*\\*|"), utf8("*c*"));
    BOOST_CHECK(m("aq*|"));
    BOOST_CHECK(m("bq*|"));
    BOOST_CHECK(!m("bc*|"));
    BOOST_CHECK(!m("bq|"));
    BOOST_CHECK(!m("b*|"));
    BOOST_CHECK(!m(""));
  }
  {
    globish_matcher m(utf8("{a,\\\\,b*}"), utf8("*c*"));
    BOOST_CHECK(m("a"));
    BOOST_CHECK(!m("ab"));
    BOOST_CHECK(m("\\"));
    BOOST_CHECK(!m("\\\\"));
    BOOST_CHECK(m("b"));
    BOOST_CHECK(m("bfoobar"));
    BOOST_CHECK(!m("bfoobarcfoobar"));
  }
  {
    globish_matcher m(utf8("*"), utf8(""));
    BOOST_CHECK(m("foo"));
    BOOST_CHECK(m(""));
  }
  {
    globish_matcher m(utf8("{foo}"), utf8(""));
    BOOST_CHECK(m("foo"));
    BOOST_CHECK(!m("bar"));
  }
}


void add_globish_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&checked_globish_to_regex_test));
  suite->add(BOOST_TEST_CASE(&combine_and_check_globish_test));
  suite->add(BOOST_TEST_CASE(&globish_matcher_test));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
