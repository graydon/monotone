#include "base.hh"
#include "simplestring_xform.hh"
#include "sanity.hh"
#include "constants.hh"

#include <set>
#include <algorithm>
#include <sstream>
#include <iterator>

using std::set;
using std::string;
using std::vector;
using std::ostringstream;
using std::ostream_iterator;
using std::transform;

struct
lowerize
{
  char operator()(char const & c) const
  {
    return ::tolower(static_cast<int>(c));
  }
};

string
lowercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), lowerize());
  return n;
}

struct
upperize
{
  char operator()(char const & c) const
  {
    return ::toupper(static_cast<int>(c));
  }
};

string
uppercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), upperize());
  return n;
}

void split_into_lines(string const & in,
                      vector<string> & out,
                      bool diff_compat)
{
  return split_into_lines(in, constants::default_encoding, out, diff_compat);
}

void split_into_lines(string const & in,
                      string const & encoding,
                      vector<string> & out)
{
  return split_into_lines(in, encoding, out, false);
}

void split_into_lines(string const & in,
                      string const & encoding,
                      vector<string> & out,
                      bool diff_compat)
{
  string lc_encoding = lowercase(encoding);
  out.clear();

  // note: this function does not handle ISO-2022-X, Shift-JIS, and
  // probably a good deal of other encodings as well. please expand
  // the logic here if you can work out an easy way of doing line
  // breaking on these encodings. currently it's just designed to
  // work with charsets in which 0x0a / 0x0d are *always* \n and \r
  // respectively.
  //
  // as far as I know, this covers the EUC, ISO-8859-X, GB, Big5, KOI,
  // ASCII, and UTF-8 families of encodings.

  if (lc_encoding == constants::default_encoding
      || lc_encoding.find("ascii") != string::npos
      || lc_encoding.find("8859") != string::npos
      || lc_encoding.find("euc") != string::npos
      || lc_encoding.find("koi") != string::npos
      || lc_encoding.find("gb") != string::npos
      || lc_encoding == "utf-8"
      || lc_encoding == "utf_8"
      || lc_encoding == "utf8")
    {
      string::size_type begin = 0;
      string::size_type end = in.find_first_of("\r\n", begin);

      while (end != string::npos && end >= begin)
        {
          out.push_back(in.substr(begin, end-begin));
          if (in.at(end) == '\r'
              && in.size() > end+1
              && in.at(end+1) == '\n')
            begin = end + 2;
          else
            begin = end + 1;
          if (begin >= in.size())
            break;
          end = in.find_first_of("\r\n", begin);
        }
      if (begin < in.size()) {
        // special case: last line without trailing newline
        string s = in.substr(begin, in.size() - begin);
        if (diff_compat) {
          // special handling: produce diff(1) compatible output
          s += (in.find_first_of("\r") != string::npos ? "\r\n" : "\n");
          s += "\\ No newline at end of file"; 
        }
        out.push_back(s);
      }
    }
  else
    {
      out.push_back(in);
    }
}


void
split_into_lines(string const & in,
                 vector<string> & out)
{
  split_into_lines(in, constants::default_encoding, out);
}

void
join_lines(vector<string> const & in,
           string & out,
           string const & linesep)
{
  ostringstream oss;
  copy(in.begin(), in.end(), ostream_iterator<string>(oss, linesep.c_str()));
  out = oss.str();
}

void
join_lines(vector<string> const & in,
           string & out)
{
  join_lines(in, out, "\n");
}

void
prefix_lines_with(string const & prefix, string const & lines, string & out)
{
  vector<string> msgs;
  split_into_lines(lines, msgs);

  ostringstream oss;
  for (vector<string>::const_iterator i = msgs.begin();
       i != msgs.end();)
    {
      oss << prefix << *i;
      i++;
      if (i != msgs.end())
        oss << '\n';
    }

  out = oss.str();
}

void
append_without_ws(string & appendto, string const & s)
{
  unsigned pos = appendto.size();
  appendto.resize(pos + s.size());
  for (string::const_iterator i = s.begin();
       i != s.end(); ++i)
    {
      switch (*i)
        {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
          break;
        default:
          appendto[pos] = *i;
          ++pos;
          break;
        }
    }
  appendto.resize(pos);
}

string
remove_ws(string const & s)
{
  string tmp;
  append_without_ws(tmp, s);
  return tmp;
}

string
trim_ws(string const & s)
{
  string tmp = s;
  string::size_type pos = tmp.find_last_not_of("\n\r\t ");
  if (pos < string::npos)
    tmp.erase(++pos);
  pos = tmp.find_first_not_of("\n\r\t ");
  if (pos < string::npos)
    tmp = tmp.substr(pos);
  return tmp;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "vocab.hh"

UNIT_TEST(simplestring_xform, caseconv)
{
  UNIT_TEST_CHECK(uppercase("hello") == "HELLO");
  UNIT_TEST_CHECK(uppercase("heLlO") == "HELLO");
  UNIT_TEST_CHECK(lowercase("POODLE DAY") == "poodle day");
  UNIT_TEST_CHECK(lowercase("PooDLe DaY") == "poodle day");
  UNIT_TEST_CHECK(uppercase("!@#$%^&*()") == "!@#$%^&*()");
  UNIT_TEST_CHECK(lowercase("!@#$%^&*()") == "!@#$%^&*()");
}

UNIT_TEST(simplestring_xform, join_lines)
{
  vector<string> strs;
  string joined;

  strs.clear();
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "");

  strs.push_back("hi");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\n");

  strs.push_back("there");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\nthere\n");

  strs.push_back("user");
  join_lines(strs, joined);
  UNIT_TEST_CHECK(joined == "hi\nthere\nuser\n");
}

UNIT_TEST(simplestring_xform, join_words)
{
  vector< utf8 > v;
  set< utf8 > s;

  v.clear();
  UNIT_TEST_CHECK(join_words(v)() == "");

  v.clear();
  v.push_back(utf8("a"));
  UNIT_TEST_CHECK(join_words(v)() == "a");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a");

  s.clear();
  s.insert(utf8("a"));
  UNIT_TEST_CHECK(join_words(s)() == "a");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a");

  v.clear();
  v.push_back(utf8("a"));
  v.push_back(utf8("b"));
  UNIT_TEST_CHECK(join_words(v)() == "a b");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a, b");

  s.clear();
  s.insert(utf8("b"));
  s.insert(utf8("a"));
  UNIT_TEST_CHECK(join_words(s)() == "a b");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a, b");

  v.clear();
  v.push_back(utf8("a"));
  v.push_back(utf8("b"));
  v.push_back(utf8("c"));
  UNIT_TEST_CHECK(join_words(v)() == "a b c");
  UNIT_TEST_CHECK(join_words(v, ", ")() == "a, b, c");

  s.clear();
  s.insert(utf8("b"));
  s.insert(utf8("a"));
  s.insert(utf8("c"));
  UNIT_TEST_CHECK(join_words(s)() == "a b c");
  UNIT_TEST_CHECK(join_words(s, ", ")() == "a, b, c");
}

UNIT_TEST(simplestring_xform, split_into_words)
{
  vector< utf8 > words;

  words = split_into_words(utf8(""));
  UNIT_TEST_CHECK(words.size() == 0);

  words = split_into_words(utf8("foo"));
  UNIT_TEST_CHECK(words.size() == 1);
  UNIT_TEST_CHECK(words[0]() == "foo");

  words = split_into_words(utf8("foo bar"));
  UNIT_TEST_CHECK(words.size() == 2);
  UNIT_TEST_CHECK(words[0]() == "foo");
  UNIT_TEST_CHECK(words[1]() == "bar");

  // describe() in commands.cc assumes this behavior.  If it ever changes,
  // remember to modify that function accordingly!
  words = split_into_words(utf8("foo  bar"));
  UNIT_TEST_CHECK(words.size() == 3);
  UNIT_TEST_CHECK(words[0]() == "foo");
  UNIT_TEST_CHECK(words[1]() == "");
  UNIT_TEST_CHECK(words[2]() == "bar");
}

UNIT_TEST(simplestring_xform, strip_ws)
{
  UNIT_TEST_CHECK(trim_ws("\n  leading space") == "leading space");
  UNIT_TEST_CHECK(trim_ws("trailing space  \n") == "trailing space");
  UNIT_TEST_CHECK(trim_ws("\t\n both \r \n\r\n") == "both");
  UNIT_TEST_CHECK(remove_ws("  I like going\tfor walks\n  ")
              == "Ilikegoingforwalks");
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
