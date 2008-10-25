// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "globish.hh"
#include "option.hh" // for arg_type
#include "numeric_vocab.hh"

#include <iterator>
#include <ostream>

using std::string;
using std::vector;
using std::back_inserter;
using std::back_insert_iterator;

// The algorithm here is originally from pdksh 5.  That implementation uses
// the high bit of unsigned chars as a quotation flag.  We can't do that,
// because we need to be utf8 clean.  Instead, we copy the string and
// replace "live" metacharacters with single bytes from the
// control-character range.  This is why bytes <= 0x1f are not allowed in the
// pattern.

enum metachar {
  META_STAR = 1,   // *
  META_QUES,       // ?
  META_CC_BRA,     // [
  META_CC_INV_BRA, // [^ or [!
  META_CC_KET,     // ] (matches either of the above two)
  META_ALT_BRA,    // {
  META_ALT_OR,     // , (when found inside unquoted { ... })
  META_ALT_KET,    // }
};

// Compile a character class.

static string::const_iterator
compile_charclass(string const & pat, string::const_iterator p,
                  back_insert_iterator<string> & to)
{
  string in_class;
  char bra = (char)META_CC_BRA;

  p++;
  N(p != pat.end(),
    F("invalid pattern '%s': unmatched '['") % pat);

  if (*p == '!' || *p == '^')
    {
      bra = (char)META_CC_INV_BRA;
      p++;
      N(p != pat.end(),
        F("invalid pattern '%s': unmatched '['") % pat);
    }

  while (p != pat.end() && *p != ']')
    {
      if (*p == '\\')
        {
          p++;
          if (p == pat.end())
            break;
        }
      // A dash at the beginning or end of the pattern is literal.
      else if (*p == '-'
               && !in_class.empty()
               && p+1 != pat.end()
               && p[1] != ']')
        {
          p++;
          if (*p == '\\')
            p++;
          if (p == pat.end())
            break;

          // the cast is needed because boost::format will not obey the %x
          // if given a 'char'.
          N((widen<unsigned int, char>(*p)) >= ' ',
            F("invalid pattern '%s': control character 0x%02x is not allowed")
            % pat % (widen<unsigned int, char>(*p)));

          unsigned int start = widen<unsigned int, char>(in_class.end()[-1]);
          unsigned int stop = widen<unsigned int, char>(*p);

          N(start != stop,
            F("invalid pattern '%s': "
              "one-element character ranges are not allowed") % pat);
          N(start < stop,
            F("invalid pattern '%s': "
              "endpoints of a character range must be in "
              "ascending numeric order") % pat);
          N(start < 0x80 && stop < 0x80,
            F("invalid pattern '%s': cannot use non-ASCII characters "
              "in classes") % pat);

          L(FL("expanding range from %X (%c) to %X (%c)")
            % (start+1) % (char)(start+1) % stop % (char)stop);
          
          for (unsigned int r = start + 1; r < stop; r++)
            in_class.push_back((char)r);
        }
      else
        N(*p != '[', F("syntax error in '%s': "
                       "character classes may not be nested") % pat);

      N((widen<unsigned int, char>(*p)) >= ' ',
        F("invalid pattern '%s': control character 0x%02x is not allowed")
        % pat % (widen<unsigned int, char>(*p)));

      N((widen<unsigned int, char>(*p)) < 0x80,
        F("invalid pattern '%s': cannot use non-ASCII characters in classes")
        % pat);

      in_class.push_back(*p);
      p++;
    }

  N(p != pat.end(),
    F("invalid pattern '%s': unmatched '['") % pat);

  N(!in_class.empty(),
    F("invalid pattern '%s': empty character class") % pat);

  // minor optimization: one-element non-inverted character class becomes
  // the character.
  if (bra == (char)META_CC_BRA && in_class.size() == 1)
    *to++ = in_class[0];
  else
    {
      *to++ = bra;
      std::sort(in_class.begin(), in_class.end());
      std::copy(in_class.begin(), in_class.end(), to);
      *to++ = (char)META_CC_KET;
    }
  return p;
}

// Compile one fragment of a glob pattern.

static void
compile_frag(string const & pat, back_insert_iterator<string> & to)
{
  unsigned int brace_depth = 0;

  for (string::const_iterator p = pat.begin(); p != pat.end(); p++)
    switch (*p)
      {
      default:
        N((widen<unsigned int, char>(*p)) >= ' ',
          F("invalid pattern '%s': control character 0x%02x is not allowed")
          % pat % (widen<unsigned int, char>(*p)));
        
        *to++ = *p;
        break;

      case '*':
        // optimization: * followed by any sequence of ?s and *s is
        // equivalent to the number of ?s that appeared in the sequence,
        // followed by a single star.  the latter can be matched without
        // nearly as much backtracking.

        for (p++; p != pat.end(); p++)
          {
            if (*p == '?')
              *to++ = META_QUES;
            else if (*p != '*')
              break;
          }

        p--;
        *to++ = META_STAR;
        break;

      case '?':
        *to++ = META_QUES;
        break;
        
      case '\\':
        p++;
        N(p != pat.end(),
          F("invalid pattern '%s': un-escaped \\ at end") % pat);

        N((widen<unsigned int, char>(*p)) >= ' ',
          F("invalid pattern '%s': control character 0x%02x is not allowed")
          % pat % (widen<unsigned int, char>(*p)));

        *to++ = *p;
        break;

      case '[':
        p = compile_charclass(pat, p, to);
        break;

      case ']':
        N(false, F("invalid pattern '%s': unmatched ']'") % pat);

      case '{':
        // There's quite a bit of optimization we could be doing on
        // alternatives, but it's hairy, especially if you get into
        // nested alternatives; so we're not doing any of it now.
        // (Look at emacs's regexp-opt.el for inspiration.)
        brace_depth++;
        N(brace_depth < 6,
          F("invalid pattern '%s': braces nested too deeply") % pat);
        *to++ = META_ALT_BRA;
        break;

      case ',':
        if (brace_depth > 0)
          *to++ = META_ALT_OR;
        else
          *to++ = ',';
        break;

      case '}':
        N(brace_depth > 0,
          F("invalid pattern '%s': unmatched '}'") % pat);
        brace_depth--;
        *to++ = META_ALT_KET;
        break;
      }

  N(brace_depth == 0,
    F("invalid pattern '%s': unmatched '{'") % pat);
}

// common code used by the constructors.

static inline string
compile(string const & pat)
{
  string s;
  back_insert_iterator<string> to = back_inserter(s);
  compile_frag(pat, to);
  return s;
}

static inline string
compile(vector<arg_type>::const_iterator const & beg,
        vector<arg_type>::const_iterator const & end)
{
  if (end - beg == 0)
    return "";
  if (end - beg == 1)
    return compile((*beg)());

  string s;
  back_insert_iterator<string> to = back_inserter(s);

  *to++ = META_ALT_BRA;
  vector<arg_type>::const_iterator i = beg;
  for (;;)
    {
      compile_frag((*i)(), to);
      i++;
      if (i == end)
        break;
      *to++ = META_ALT_OR;
    }
  *to++ = META_ALT_KET;
  return s;
}

globish::globish(string const & p) : compiled_pattern(compile(p)) {}
globish::globish(char const * p) : compiled_pattern(compile(p)) {}

globish::globish(vector<arg_type> const & p)
  : compiled_pattern(compile(p.begin(), p.end())) {}
globish::globish(vector<arg_type>::const_iterator const & beg,
                 vector<arg_type>::const_iterator const & end)
  : compiled_pattern(compile(beg, end)) {}

// Debugging.

static string
decode(string::const_iterator p, string::const_iterator end)
{
  string s;
  for (; p != end; p++)
    switch (*p)
      {
      case META_STAR:       s.push_back('*'); break;
      case META_QUES:       s.push_back('?'); break;
      case META_CC_BRA:     s.push_back('['); break;
      case META_CC_KET:     s.push_back(']'); break;
      case META_CC_INV_BRA: s.push_back('[');
                            s.push_back('!'); break;

      case META_ALT_BRA:    s.push_back('{'); break;
      case META_ALT_KET:    s.push_back('}'); break;
      case META_ALT_OR:     s.push_back(','); break;

        // Some of these are only special in certain contexts,
        // but it does no harm to escape them always.
      case '[': case ']': case '-': case '!': case '^':
      case '{': case '}': case ',':
      case '*': case '?': case '\\':
        s.push_back('\\');
        // fall through
      default:
        s.push_back(*p);
      }
  return s;
}

string
globish::operator()() const
{
  return decode(compiled_pattern.begin(), compiled_pattern.end());
}

template <> void dump(globish const & g, string & s)
{
  s = g();
}

std::ostream & operator<<(std::ostream & o, globish const & g)
{
  return o << g();
}

// Matching.

static string::const_iterator
find_next_subpattern(string::const_iterator p,
                       string::const_iterator pe,
                       bool want_alternatives)
{
  L(FL("Finding subpattern in '%s'") % decode(p, pe));
  unsigned int depth = 1;
  for (; p != pe; p++)
    switch (*p)
      {
      default:
        break;

      case META_ALT_BRA:
        depth++;
        break;

      case META_ALT_KET:
        depth--;
        if (depth == 0)
          return p+1;
        break;

      case META_ALT_OR:
        if (depth == 1 && want_alternatives)
          return p+1;
        break;
      }

  I(false);
}
                   

static bool
do_match(string::const_iterator sb, string::const_iterator se,
         string::const_iterator p, string::const_iterator pe)
{
  unsigned int sc, pc;
  string::const_iterator s(sb);

  L(FL("subpattern: '%s' against '%s'") % string(s,se) % decode(p,pe));

  while (p < pe)
    {
      pc = widen<unsigned int, char>(*p++);
      if(s < se) {
        sc = widen<unsigned int, char>(*s);
        s++;
      } else {
        sc = 0;
      }
      switch (pc)
        {
        default:           // literal
          if (sc != pc)
            return false;
          break;

        case META_QUES: // any single character
          if (sc == 0)
            return false;
          break;

        case META_CC_BRA:  // any of these characters
          {
            bool matched = false;
            I(p < pe);
            I(*p != META_CC_KET);
            do
              {
                if (widen<unsigned int, char>(*p) == sc)
                  matched = true;
                p++;
                I(p < pe);
              }
            while (*p != META_CC_KET);
            if (!matched)
              return false;
          }
          p++;
          break;

        case META_CC_INV_BRA:  // any but these characters
          I(p < pe);
          I(*p != META_CC_KET);
          do
            {
              if (widen<unsigned int, char>(*p) == sc)
                return false;
              p++;
              I(p < pe);
            }
          while (*p != META_CC_KET);
          p++;
          break;

        case META_STAR:    // zero or more arbitrary characters
          if (p == pe)
            return true; // star at end always matches, if we get that far

          pc = widen<unsigned int, char>(*p);
          // If the next character in p is not magic, we can only match
          // starting from places in s where that character appears.
          if (pc >= ' ')
            {
              L(FL("after *: looking for '%c' in '%c%s'")
                % (char)pc % (char)sc % string(s, se));
              p++;
              for (;;)
                {
                  if (sc == pc && do_match(s, se, p, pe))
                    return true;
                  if (s >= se)
                    break;
                  sc = widen<unsigned int, char>(*s++);
                }
            }
          else
            {
              L(FL("metacharacter after *: doing it the slow way"));
              s--;
              do
                {
                  if (do_match(s, se, p, pe))
                    return true;
                  s++;
                }
              while (s < se);
            }
          return false;

        case META_ALT_BRA:
          {
            string::const_iterator prest, psub, pnext;
            string::const_iterator srest;

            prest = find_next_subpattern(p, pe, false);
            psub = p;
            if(s > sb) {
              s--;
            }
            do
              {
                pnext = find_next_subpattern(psub, pe, true);
                srest = (prest == pe ? se : s);
                for (; srest < se; srest++)
                  {
                    if (do_match(s, srest, psub, pnext - 1)
                        && do_match(srest, se, prest, pe))
                      return true;
                  }
                // try the empty target too
                if (do_match(s, srest, psub, pnext - 1)
                    && do_match(srest, se, prest, pe))
                  return true;
                
                psub = pnext;
              }
            while (pnext < prest);
            return false;
          }
        }
    }
  return s == se;
}

bool globish::matches(string const & target) const
{
  bool result;
  
  // The empty pattern matches nothing.
  if (compiled_pattern.empty())
    result = false;
  else
    result = do_match (target.begin(), target.end(),
                       compiled_pattern.begin(), compiled_pattern.end());

  L(FL("matching '%s' against '%s': %s")
    % target % (*this)() % (result ? "matches" : "does not match"));
  return result;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(globish, syntax)
{
  struct tcase
  {
    char const * in;
    char const * out;
  };
  tcase const good[] = {
    { "a",   "a" },
    { "\\a", "a" },
    { "[a]", "a" },
    { "[!a]", "[!a]" },
    { "[^a]", "[!a]" },
    { "[\\!a]", "[\\!a]" },
    { "[\\^a]", "[\\^a]" },
    { "[ab]", "[ab]" },
    { "[a-b]", "[ab]" },
    { "[a-c]", "[abc]" },
    { "[ac-]", "[\\-ac]" },
    { "[-ac]", "[\\-ac]" },
    { "[+-/]", "[+\\,\\-./]" },

    { "\xC2\xA1", "\xC2\xA1" }, // U+00A1 in UTF8
    
    { "*",   "*" },
    { "\\*", "\\*" },
    { "[*]", "\\*" },
    { "?",   "?" },
    { "\\?", "\\?" },
    { "[?]", "\\?" },
    { ",",   "\\," },
    { "\\,", "\\," },
    { "[,]", "\\," },
    { "\\{", "\\{" },
    { "[{]", "\\{" },
    { "[}]", "\\}" },
    { "\\[", "\\[" },
    { "\\]", "\\]" },
    { "\\\\", "\\\\" },

    { "**",      "*" },
    { "*?",      "?*" },
    { "*???*?*", "????*" },
    { "*a?*?b*", "*a??*b*" },

    { "{a,b,c}d", "{a,b,c}d" },
    { "foo{a,{b,c},?*}d", "foo{a,{b,c},?*}d" },
    { "\\a\\b\\|\\{\\*", "ab|\\{\\*" },
    { ".+$^{}", ".+$\\^{}" },
    { "\\.\\+\\$\\^\\(\\)", ".+$\\^()" },
    { 0, 0 }
  };

  char const * const bad[] = {
    "[",
    "[!",
    "[\\",
    "[\\]",
    "[foo",
    "[!foo",
    "foo]",
    "[\003]",
    "[a-a]",
    "[f-a]",
    "[]",
    "[\xC2\xA1]",
    "[\xC2\xA1\xC2\xA2]",
    "[\xC2\xA1-\xC2\xA2]",
    "[-\xC2\xA1]",
    "[[]",
    "[]",

    "\003",
    "foo\\",
    "{foo",
    "{foo,bar{baz,quux}",
    "foo}",
    "foo,bar{baz,quux}}",
    "{{{{{{{{{{a,b},c},d},e},f},g},h},i},j},k}",
    0
  };
  char const dummy[] = "";

  for (tcase const * p = good; p->in; p++)
    {
      globish g(p->in);
      string s;
      dump(g, s);
      L(FL("globish syntax: %s -> %s [expect %s]") % p->in % s % p->out);
      UNIT_TEST_CHECK(s == p->out);
    }

  for (char const * const * p = bad; *p; p++)
    {
      L(FL("globish syntax: invalid %s") % *p);
      UNIT_TEST_CHECK_THROW(I(globish(*p).matches(dummy)), informative_failure);
    }
}

UNIT_TEST(globish, from_vector)
{
  vector<arg_type> v;
  v.push_back(arg_type("a"));
  v.push_back(arg_type("b"));
  v.push_back(arg_type("c"));
  globish combined(v);
  string s;
  dump(combined, s);
  UNIT_TEST_CHECK(s == "{a,b,c}");
}

UNIT_TEST(globish, simple_matches)
{
  UNIT_TEST_CHECK(globish("abc").matches("abc"));
  UNIT_TEST_CHECK(!globish("abc").matches("aac"));

  UNIT_TEST_CHECK(globish("a[bc]d").matches("abd"));
  UNIT_TEST_CHECK(globish("a[bc]d").matches("acd"));
  UNIT_TEST_CHECK(!globish("a[bc]d").matches("and"));
  UNIT_TEST_CHECK(!globish("a[bc]d").matches("ad"));
  UNIT_TEST_CHECK(!globish("a[bc]d").matches("abbd"));

  UNIT_TEST_CHECK(globish("a[!bc]d").matches("and"));
  UNIT_TEST_CHECK(globish("a[!bc]d").matches("a#d"));
  UNIT_TEST_CHECK(!globish("a[!bc]d").matches("abd"));
  UNIT_TEST_CHECK(!globish("a[!bc]d").matches("acd"));
  UNIT_TEST_CHECK(!globish("a[!bc]d").matches("ad"));
  UNIT_TEST_CHECK(!globish("a[!bc]d").matches("abbd"));

  UNIT_TEST_CHECK(globish("a?c").matches("abc"));
  UNIT_TEST_CHECK(globish("a?c").matches("aac"));
  UNIT_TEST_CHECK(globish("a?c").matches("a%c"));
  UNIT_TEST_CHECK(!globish("a?c").matches("a%d"));
  UNIT_TEST_CHECK(!globish("a?c").matches("d%d"));
  UNIT_TEST_CHECK(!globish("a?c").matches("d%c"));
  UNIT_TEST_CHECK(!globish("a?c").matches("a%%d"));

  UNIT_TEST_CHECK(globish("a*c").matches("ac"));
  UNIT_TEST_CHECK(globish("a*c").matches("abc"));
  UNIT_TEST_CHECK(globish("a*c").matches("abac"));
  UNIT_TEST_CHECK(globish("a*c").matches("abbcc"));
  UNIT_TEST_CHECK(globish("a*c").matches("abcbbc"));
  UNIT_TEST_CHECK(!globish("a*c").matches("abcbb"));
  UNIT_TEST_CHECK(!globish("a*c").matches("abcb"));
  UNIT_TEST_CHECK(!globish("a*c").matches("aba"));
  UNIT_TEST_CHECK(!globish("a*c").matches("ab"));

  UNIT_TEST_CHECK(globish("*.bak").matches(".bak"));
  UNIT_TEST_CHECK(globish("*.bak").matches("a.bak"));
  UNIT_TEST_CHECK(globish("*.bak").matches("foo.bak"));
  UNIT_TEST_CHECK(globish("*.bak").matches(".bak.bak"));
  UNIT_TEST_CHECK(globish("*.bak").matches("fwibble.bak.bak"));

  UNIT_TEST_CHECK(globish("a*b*[cd]").matches("abc"));
  UNIT_TEST_CHECK(globish("a*b*[cd]").matches("abcd"));
  UNIT_TEST_CHECK(globish("a*b*[cd]").matches("aabrd"));
  UNIT_TEST_CHECK(globish("a*b*[cd]").matches("abbbbbbbccd"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]").matches("ab"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]").matches("abde"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]").matches("aaaaaaab"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]").matches("axxxxd"));
  UNIT_TEST_CHECK(!globish("a*b*[cd]").matches("adb"));
}

UNIT_TEST(globish, complex_matches)
{  {
    globish_matcher m(globish("{a,b}?*\\*|"), globish("*c*"));
    UNIT_TEST_CHECK(m("aq*|"));
    UNIT_TEST_CHECK(m("bq*|"));
    UNIT_TEST_CHECK(!m("bc*|"));
    UNIT_TEST_CHECK(!m("bq|"));
    UNIT_TEST_CHECK(!m("b*|"));
    UNIT_TEST_CHECK(!m(""));
  }
  {
    globish_matcher m(globish("{a,\\\\,b*}"), globish("*c*"));
    UNIT_TEST_CHECK(m("a"));
    UNIT_TEST_CHECK(!m("ab"));
    UNIT_TEST_CHECK(m("\\"));
    UNIT_TEST_CHECK(!m("\\\\"));
    UNIT_TEST_CHECK(m("b"));
    UNIT_TEST_CHECK(m("bfoobar"));
    UNIT_TEST_CHECK(!m("bfoobarcfoobar"));
  }
  {
    globish_matcher m(globish("*"), globish(""));
    UNIT_TEST_CHECK(m("foo"));
    UNIT_TEST_CHECK(m(""));
  }
  {
    globish_matcher m(globish("{foo}"), globish(""));
    UNIT_TEST_CHECK(m("foo"));
    UNIT_TEST_CHECK(!m("bar"));
  }
}

UNIT_TEST(globish, nested_matches)
{
  globish g("a.{i.{x,y},j}");
  UNIT_TEST_CHECK(g.matches("a.i.x"));
  UNIT_TEST_CHECK(g.matches("a.i.y"));
  UNIT_TEST_CHECK(g.matches("a.j"));
  UNIT_TEST_CHECK(!g.matches("q"));
  UNIT_TEST_CHECK(!g.matches("a.q"));
  UNIT_TEST_CHECK(!g.matches("a.j.q"));
  UNIT_TEST_CHECK(!g.matches("a.i.q"));
  UNIT_TEST_CHECK(!g.matches("a.i.x.q"));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
