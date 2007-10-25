// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "vector.hh"

#include <boost/tokenizer.hpp>

#include "idna/idna.h"
#include "idna/stringprep.h"

#include "charset.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"

using std::string;
using std::vector;
using std::free;

using boost::char_separator;

// General character code conversion routines.

static string
system_charset()
{
  char const * locale_charset_name = stringprep_locale_charset ();
  I(locale_charset_name != NULL);
  string sys_charset(locale_charset_name);
  return sys_charset;
}

void
charset_convert(string const & src_charset,
                string const & dst_charset,
                string const & src,
                string & dst,
                bool best_effort)
{
  if (src_charset == dst_charset)
    dst = src;
  else
    {
      char * converted = stringprep_convert(src.c_str(),
                                            dst_charset.c_str(),
                                            src_charset.c_str(),
                                            best_effort);
      E(converted != NULL,
        F("failed to convert string from %s to %s: '%s'")
         % src_charset % dst_charset % src);
      dst = string(converted);
      free(converted);
    }
}

size_t
display_width(utf8 const & utf)
{
  string const & u = utf();
  size_t sz = 0;
  string::const_iterator i = u.begin();
  while (i != u.end())
    {
      if (UNLIKELY(static_cast<u8>(*i) & static_cast<u8>(0x80)))
        {
          // A UTF-8 escape: consume the full escape.
          ++i;
          ++sz;
          while (i != u.end()
                 && (static_cast<u8>(*i) & static_cast<u8>(0x80))
                 && (!(static_cast<u8>(*i) & static_cast<u8>(0x40))))
            ++i;
        }
      else
        {
          // An ASCII-like character in the range 0..0x7F.
          ++i;
          ++sz;
        }
    }
  return sz;
}

// Lots of gunk to avoid charset conversion as much as possible.  Running
// iconv over every element of every path in a 30,000 file manifest takes
// multiple seconds, which then is a minimum bound on pretty much any
// operation we do...
static inline bool
system_charset_is_utf8_impl()
{
  string lc_encoding = lowercase(system_charset());
  return (lc_encoding == "utf-8"
          || lc_encoding == "utf_8"
          || lc_encoding == "utf8");
}

static inline bool
system_charset_is_utf8()
{
  static bool it_is = system_charset_is_utf8_impl();
  return it_is;
}

static inline bool
system_charset_is_ascii_extension_impl()
{
  if (system_charset_is_utf8())
    return true;
  string lc_encoding = lowercase(system_charset());
  // if your character set is identical to ascii in the lower 7 bits, then add
  // it here for a speed boost.
  return (lc_encoding.find("ascii") != string::npos
          || lc_encoding.find("8859") != string::npos
          || lc_encoding.find("ansi_x3.4") != string::npos
          || lc_encoding == "646" // another name for ascii
          // http://www.cs.mcgill.ca/~aelias4/encodings.html -- "EUC (Extended
          // Unix Code) is a simple and clean encoding, standard on Unix
          // systems.... It is backwards-compatible with ASCII (i.e. valid
          // ASCII implies valid EUC)."
          || lc_encoding.find("euc") != string::npos);
}

static inline bool
system_charset_is_ascii_extension()
{
  static bool it_is = system_charset_is_ascii_extension_impl();
  return it_is;
}

inline static bool
is_all_ascii(string const & utf)
{
  // could speed this up by vectorization -- mask against 0x80808080,
  // process a whole word at at time...
  for (string::const_iterator i = utf.begin(); i != utf.end(); ++i)
    if (0x80 & *i)
      return false;
  return true;
}

// this function must be fast.  do not make it slow.
void
utf8_to_system_strict(utf8 const & utf, string & ext)
{
  if (system_charset_is_utf8())
    ext = utf();
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(utf()))
    ext = utf();
  else
    charset_convert("UTF-8", system_charset(), utf(), ext, false);
}

// this function must be fast.  do not make it slow.
void
utf8_to_system_best_effort(utf8 const & utf, string & ext)
{
  if (system_charset_is_utf8())
    ext = utf();
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(utf()))
    ext = utf();
  else
    charset_convert("UTF-8", system_charset(), utf(), ext, true);
}

void
utf8_to_system_strict(utf8 const & utf, external & ext)
{
  string out;
  utf8_to_system_strict(utf, out);
  ext = external(out);
}

void
utf8_to_system_best_effort(utf8 const & utf, external & ext)
{
  string out;
  utf8_to_system_best_effort(utf, out);
  ext = external(out);
}

void
system_to_utf8(external const & ext, utf8 & utf)
{
  if (system_charset_is_utf8())
    utf = utf8(ext());
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(ext()))
    utf = utf8(ext());
  else
    {
      string out;
      charset_convert(system_charset(), "UTF-8", ext(), out, false);
      utf = utf8(out);
      I(utf8_validate(utf));
    }
}

// utf8_validate and the helper functions is_valid_unicode_char and
// utf8_consume_continuation_char g_utf8_validate and supporting functions
// from the file gutf8.c of the GLib library.

static bool
is_valid_unicode_char(u32 c)
{
  return (c < 0x110000 &&
          ((c & 0xfffff800) != 0xd800) &&
          (c < 0xfdd0 || c > 0xfdef) &&
          (c & 0xfffe) != 0xfffe);
}

static bool
utf8_consume_continuation_char(u8 c, u32 & val)
{
  if ((c & 0xc0) != 0x80)
    return false;
  val <<= 6;
  val |= c & 0x3f;
  return true;
}

bool
utf8_validate(utf8 const & utf)
{
  string::size_type left = utf().size();
  u32 min, val;

  for (string::const_iterator i = utf().begin();
       i != utf().end(); ++i, --left)
  {
    u8 c = *i;
    if (c < 128)
      continue;
    if ((c & 0xe0) == 0xc0)
    {
      if (left < 2)
        return false;
      if ((c & 0x1e) == 0)
        return false;
      ++i; --left; c = *i;
      if ((c & 0xc0) != 0x80)
        return false;
    }
    else
    {
      if ((c & 0xf0) == 0xe0)
      {
        if (left < 3)
          return false;
        min = 1 << 11;
        val = c & 0x0f;
        goto two_remaining;
      }
      else if ((c & 0xf8) == 0xf0)
      {
        if (left < 4)
          return false;
        min = 1 << 16;
        val = c & 0x07;
      }
      else
        return false;
      ++i; --left; c = *i;
      if (!utf8_consume_continuation_char(c, val))
        return false;
two_remaining:
      ++i; --left; c = *i;
      if (!utf8_consume_continuation_char(c, val))
        return false;
      ++i; --left; c = *i;
      if (!utf8_consume_continuation_char(c, val))
        return false;
      if (val < min)
        return false;
      if (!is_valid_unicode_char(val))
        return false;
    }
  }
  return true;
}

static string
decode_idna_error(int err)
{
  switch (static_cast<Idna_rc>(err))
    {
    case IDNA_STRINGPREP_ERROR: return "stringprep error"; break;
    case IDNA_PUNYCODE_ERROR: return "punycode error"; break;
    case IDNA_CONTAINS_NON_LDH: return "non-LDH characters"; break;
    case IDNA_CONTAINS_MINUS: return "leading / trailing hyphen-minus character"; break;
    case IDNA_INVALID_LENGTH: return "invalid length (output must be between 1 and 63 chars)"; break;
    case IDNA_NO_ACE_PREFIX: return "no ace prefix"; break;
    case IDNA_ROUNDTRIP_VERIFY_ERROR: return "roundtrip verify error"; break;
    case IDNA_CONTAINS_ACE_PREFIX: return "contains ACE prefix (\"xn--\")"; break;
    case IDNA_ICONV_ERROR: return "iconv error"; break;
    case IDNA_MALLOC_ERROR: return "malloc error"; break;
    default: return "unknown error"; break;
    }
  return "unknown error";
}

static void
ace_to_utf8(string const & a, utf8 & utf)
{
  char *out = NULL;
  L(FL("converting %d bytes from IDNA ACE to UTF-8") % a.size());
  int res = idna_to_unicode_8z8z(a.c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS || res == IDNA_NO_ACE_PREFIX,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % a.size()
    % decode_idna_error(res));
  utf = utf8(string(out));
  free(out);
}

static void
utf8_to_ace(utf8 const & utf, string & a)
{
  char *out = NULL;
  L(FL("converting %d bytes from UTF-8 to IDNA ACE") % utf().size());
  int res = idna_to_ascii_8z(utf().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % utf().size()
    % decode_idna_error(res));
  a = string(out);
  free(out);
}

void
internalize_cert_name(utf8 const & utf, cert_name & c)
{
  string a;
  utf8_to_ace(utf, a);
  c = cert_name(a);
}

void
internalize_cert_name(external const & ext, cert_name & c)
{
  utf8 utf;
  system_to_utf8(ext, utf);
  internalize_cert_name(utf, c);
}

void
internalize_rsa_keypair_id(utf8 const & utf, rsa_keypair_id & key)
{
  string tmp;
  typedef boost::tokenizer<char_separator<char> >
    tokenizer;
  char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(utf(), sep);
  bool in_domain = false;
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (!in_domain || *i == "." || *i == "@")
        tmp += *i;
      else
        {
          string a;
          utf8_to_ace(utf8(*i), a);
          tmp += a;
        }
      if (*i == "@")
        in_domain = true;
    }
  key = rsa_keypair_id(tmp);
}

void
internalize_rsa_keypair_id(external const & ext, rsa_keypair_id & key)
{
  utf8 utf;
  system_to_utf8(ext, utf);
  internalize_rsa_keypair_id(utf, key);
}

void
externalize_rsa_keypair_id(rsa_keypair_id const & key, utf8 & utf)
{
  string tmp;
  typedef boost::tokenizer<char_separator<char> >
    tokenizer;
  char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(key(), sep);
  bool in_domain = false;
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (!in_domain || *i == "." || *i == "@")
        tmp += *i;
      else
        {
          utf8 u;
          ace_to_utf8(*i, u);
          tmp += u();
        }
      if (*i == "@")
        in_domain = true;
    }
  utf = utf8(tmp);
}

void
externalize_rsa_keypair_id(rsa_keypair_id const & key, external & ext)
{
  utf8 utf;
  externalize_rsa_keypair_id(key, utf);
  utf8_to_system_strict(utf, ext);
}

void
internalize_var_domain(utf8 const & utf, var_domain & d)
{
  string a;
  utf8_to_ace(utf, a);
  d = var_domain(a);
}

void
internalize_var_domain(external const & ext, var_domain & d)
{
  utf8 utf;
  system_to_utf8(ext, utf);
  internalize_var_domain(utf, d);
}

void
externalize_var_domain(var_domain const & d, utf8 & utf)
{
  ace_to_utf8(d(), utf);
}

void
externalize_var_domain(var_domain const & d, external & ext)
{
  utf8 utf;
  externalize_var_domain(d, utf);
  utf8_to_system_strict(utf, ext);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include <stdlib.h>

#define IDNA_ACE_PREFIX "xn--"
#define IDNA_SUCCESS 0

struct
idna
{
  char const * name;
  char const * utf;
  char const * ace;
} const idna_vec[] =
  {
    // In C, \x escapes consume an unbounded number of hexadecimal digits,
    // and if the resulting number is too big for a byte it is a semantic
    // error.  However, if a string constant is composed of more than one
    // string literal, they do not extend across a boundary between string
    // literals.  Thus, in some places in this array, string literals have
    // been split solely to end \x escapes after two hex digits.
    {
      "Arabic (Egyptian)",
      "\xd9\x84\xd9\x8a\xd9\x87\xd9\x85\xd8\xa7\xd8\xa8\xd8\xaa\xd9\x83\xd9"
      "\x84\xd9\x85\xd9\x88\xd8\xb4\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\x9f",
      IDNA_ACE_PREFIX "egbpdaj6bu4bxfgehfvwxn" },
    {
      "Chinese (simplified)",
      "\xe4\xbb\x96\xe4\xbb\xac\xe4\xb8\xba\xe4\xbb\x80\xe4\xb9\x88\xe4\xb8"
      "\x8d\xe8\xaf\xb4\xe4\xb8\xad\xe6\x96\x87",
      IDNA_ACE_PREFIX "ihqwcrb4cv8a8dqg056pqjye" },
    {
      "Chinese (traditional)",
      "\xe4\xbb\x96\xe5\x80\x91\xe7\x88\xb2\xe4\xbb\x80\xe9\xba\xbd\xe4\xb8"
      "\x8d\xe8\xaa\xaa\xe4\xb8\xad\xe6\x96\x87",
      IDNA_ACE_PREFIX "ihqwctvzc91f659drss3x8bo0yb" },
    {
      "Czech",
      "Pro\xc4\x8dprost\xc4\x9bnemluv\xc3\xad\xc4\x8d""esky",
      IDNA_ACE_PREFIX "Proprostnemluvesky-uyb24dma41a"},
    {
      "Hebrew",
      "\xd7\x9c\xd7\x9e\xd7\x94\xd7\x94\xd7\x9d\xd7\xa4\xd7\xa9\xd7\x95\xd7"
      "\x98\xd7\x9c\xd7\x90\xd7\x9e\xd7\x93\xd7\x91\xd7\xa8\xd7\x99\xd7\x9d"
      "\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa",
      IDNA_ACE_PREFIX "4dbcagdahymbxekheh6e0a7fei0b"},
    {
      "Hindi (Devanagari)",
      "\xe0\xa4\xaf\xe0\xa4\xb9\xe0\xa4\xb2\xe0\xa5\x8b\xe0\xa4\x97\xe0\xa4"
      "\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80\xe0"
      "\xa4\x95\xe0\xa5\x8d\xe0\xa4\xaf\xe0\xa5\x8b\xe0\xa4\x82\xe0\xa4\xa8"
      "\xe0\xa4\xb9\xe0\xa5\x80\xe0\xa4\x82\xe0\xa4\xac\xe0\xa5\x8b\xe0\xa4"
      "\xb2\xe0\xa4\xb8\xe0\xa4\x95\xe0\xa4\xa4\xe0\xa5\x87\xe0\xa4\xb9\xe0"
      "\xa5\x88\xe0\xa4\x82",
      IDNA_ACE_PREFIX "i1baa7eci9glrd9b2ae1bj0hfcgg6iyaf8o0a1dig0cd"},
    {
      "Japanese (kanji and hiragana)",
      "\xe3\x81\xaa\xe3\x81\x9c\xe3\x81\xbf\xe3\x82\x93\xe3\x81\xaa\xe6\x97"
      "\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x82\x92\xe8\xa9\xb1\xe3\x81\x97\xe3"
      "\x81\xa6\xe3\x81\x8f\xe3\x82\x8c\xe3\x81\xaa\xe3\x81\x84\xe3\x81\xae"
      "\xe3\x81\x8b",
      IDNA_ACE_PREFIX "n8jok5ay5dzabd5bym9f0cm5685rrjetr6pdxa"},
    {
      "Russian (Cyrillic)",
      "\xd0\xbf\xd0\xbe\xd1\x87\xd0\xb5\xd0\xbc\xd1\x83\xd0\xb6\xd0\xb5\xd0"
      "\xbe\xd0\xbd\xd0\xb8\xd0\xbd\xd0\xb5\xd0\xb3\xd0\xbe\xd0\xb2\xd0\xbe"
      "\xd1\x80\xd1\x8f\xd1\x82\xd0\xbf\xd0\xbe\xd1\x80\xd1\x83\xd1\x81\xd1"
      "\x81\xd0\xba\xd0\xb8",
      IDNA_ACE_PREFIX "b1abfaaepdrnnbgefbadotcwatmq2g4l"},
    {
      "Spanish",
      "Porqu\xc3\xa9nopuedensimplementehablarenEspa\xc3\xb1ol",
      IDNA_ACE_PREFIX "PorqunopuedensimplementehablarenEspaol-fmd56a"},
    {
      "Vietnamese",
      "T\xe1\xba\xa1isaoh\xe1\xbb\x8dkh\xc3\xb4ngth\xe1\xbb\x83""ch\xe1\xbb"
      "\x89n\xc3\xb3iti\xe1\xba\xbfngVi\xe1\xbb\x87t",
      IDNA_ACE_PREFIX "TisaohkhngthchnitingVit-kjcr8268qyxafd2f1b9g"},
    {
      "Japanese",
      "3\xe5\xb9\xb4""B\xe7\xb5\x84\xe9\x87\x91\xe5\x85\xab\xe5\x85\x88\xe7"
      "\x94\x9f",
      IDNA_ACE_PREFIX "3B-ww4c5e180e575a65lsy2b"},
    {
      "Japanese",
      "\xe5\xae\x89\xe5\xae\xa4\xe5\xa5\x88\xe7\xbe\x8e\xe6\x81\xb5-with-"
      "SUPER-MONKEYS",
      IDNA_ACE_PREFIX "-with-SUPER-MONKEYS-pc58ag80a8qai00g7n9n"},
    {
      "Japanese",
      "Hello-Another-Way-\xe3\x81\x9d\xe3\x82\x8c\xe3\x81\x9e\xe3\x82\x8c"
      "\xe3\x81\xae\xe5\xa0\xb4\xe6\x89\x80",
      IDNA_ACE_PREFIX "Hello-Another-Way--fc4qua05auwb3674vfr0b"},
    {
      "Japanese",
      "\xe3\x81\xb2\xe3\x81\xa8\xe3\x81\xa4\xe5\xb1\x8b\xe6\xa0\xb9\xe3\x81"
      "\xae\xe4\xb8\x8b""2",
      IDNA_ACE_PREFIX "2-u9tlzr9756bt3uc0v"},
    {
      "Japanese",
      "Maji\xe3\x81\xa7Koi\xe3\x81\x99\xe3\x82\x8b""5\xe7\xa7\x92\xe5\x89\x8d",
      IDNA_ACE_PREFIX "MajiKoi5-783gue6qz075azm5e"},
    {
      "Japanese",
      "\xe3\x83\x91\xe3\x83\x95\xe3\x82\xa3\xe3\x83\xbc""de\xe3\x83\xab\xe3\x83"
      "\xb3\xe3\x83\x90",
      IDNA_ACE_PREFIX "de-jg4avhby1noc0d"},
    {
      "Japanese",
      "\xe3\x81\x9d\xe3\x81\xae\xe3\x82\xb9\xe3\x83\x94\xe3\x83\xbc\xe3\x83"
      "\x89\xe3\x81\xa7",
      IDNA_ACE_PREFIX "d9juau41awczczp"},
    {
      "Greek",
      "\xce\xb5\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac",
      IDNA_ACE_PREFIX "hxargifdar"},
    {
      "Maltese (Malti)",
      "bon\xc4\xa1usa\xc4\xa7\xc4\xa7""a",
      IDNA_ACE_PREFIX "bonusaa-5bb1da"},
    {
      "Russian (Cyrillic)",
      "\xd0\xbf\xd0\xbe\xd1\x87\xd0\xb5\xd0\xbc\xd1\x83\xd0\xb6\xd0\xb5\xd0"
      "\xbe\xd0\xbd\xd0\xb8\xd0\xbd\xd0\xb5\xd0\xb3\xd0\xbe\xd0\xb2\xd0\xbe"
      "\xd1\x80\xd1\x8f\xd1\x82\xd0\xbf\xd0\xbe\xd1\x80\xd1\x83\xd1\x81\xd1"
      "\x81\xd0\xba\xd0\xb8",
      IDNA_ACE_PREFIX "b1abfaaepdrnnbgefbadotcwatmq2g4l"},
  };

UNIT_TEST(charset, idna_encoding)
{
  // putenv takes a char*, not a const char*, there is nothing we can do.
  putenv(const_cast<char *>("CHARSET=UTF-8"));

  for (size_t i = 0; i < sizeof(idna_vec) / sizeof(struct idna); ++i)
    {
      UNIT_TEST_CHECKPOINT(("IDNA language: "
                            + string(idna_vec[i].name)).c_str());

      string u = lowercase(idna_vec[i].utf);
      string a = lowercase(idna_vec[i].ace);
      string tace;
      utf8_to_ace(utf8(u), tace);
      L(FL("ACE-encoded %s: '%s'") % idna_vec[i].name % tace);
      UNIT_TEST_CHECK(a == lowercase(tace));

      utf8 tutf;
      ace_to_utf8(a, tutf);
      L(FL("UTF-encoded %s: '%s'") % idna_vec[i].name % tutf);
      UNIT_TEST_CHECK(u == lowercase(tutf()));
    }
}

UNIT_TEST(charset, utf8_validation)
{
  // these tests are based on the tests from the file utf8-validate.c of the
  // GLib library, and also include sequences from Markus Kuhn's UTF-8
  // example files.
  const char* good_strings[] = {
    "this is a valid but boring ASCII string",

    "\x28\x28\x56\xe2\x8d\xb3\x56\x29\x3d\xe2\x8d\xb3\xe2\x8d\xb4\x56\x29\x2f"
    "\x56\xe2\x86\x90\x2c\x56\x20\x20\x20\x20\xe2\x8c\xb7\xe2\x86\x90\xe2\x8d"
    "\xb3\xe2\x86\x92\xe2\x8d\xb4\xe2\x88\x86\xe2\x88\x87\xe2\x8a\x83\xe2\x80"
    "\xbe\xe2\x8d\x8e\xe2\x8d\x95\xe2\x8c\x88",

    "\xe2\x80\x98\x73\x69\x6e\x67\x6c\x65\xe2\x80\x99\x20\x61\x6e\x64\x20\xe2"
    "\x80\x9c\x64\x6f\x75\x62\x6c\x65\xe2\x80\x9d\x20\x71\x75\x6f\x74\x65\x73",

    "\xe2\x80\xa2\x20\x43\x75\x72\x6c\x79\x20\x61\x70\x6f\x73\x74\x72\x6f\x70"
    "\x68\x65\x73\x3a\x20\xe2\x80\x9c\x57\x65\xe2\x80\x99\x76\x65\x20\x62\x65"
    "\x65\x6e\x20\x68\x65\x72\x65\xe2\x80\x9d",

    "\xe2\x80\x9a\x64\x65\x75\x74\x73\x63\x68\x65\xe2\x80\x98\x20\xe2\x80\x9e"
    "\x41\x6e\x66\xc3\xbc\x68\x72\x75\x6e\x67\x73\x7a\x65\x69\x63\x68\x65\x6e"
    "\xe2\x80\x9c",

    "\xe2\x80\xa0\x2c\x20\xe2\x80\xa1\x2c\x20\xe2\x80\xb0\x2c\x20\xe2\x80\xa2"
    "\x2c\x20\x33\xe2\x80\x93\x34\x2c\x20\xe2\x80\x94\x2c\x20\xe2\x88\x92\x35"
    "\x2f\x2b\x35\x2c\x20\xe2\x84\xa2\x2c\x20\xe2\x80\xa6",

    "\xc2\xa9\xc2\xa9\xc2\xa9",
    "\xe2\x89\xa0\xe2\x89\xa0",
    "\xce\xba\xe1\xbd\xb9\xcf\x83\xce\xbc\xce\xb5",
    "\x00",
    "\xc2\x80",
    "\xe0\xa0\x80",
    "\xf0\x90\x80\x80",
    "\x7f",
    "\xdf\xbf",
    "\xed\x9f\xbf",
    "\xee\x80\x80",
    "\xef\xbf\xbd",
    0
  };
  const char* bad_strings[] = {
    "\xf8\x88\x80\x80\x80",
    "\xfc\x84\x80\x80\x80\x80",
    "\xef\xbf\xbf",
    "\xf7\xbf\xbf\xbf",
    "\xfb\xbf\xbf\xbf\xbf",
    "\xfd\xbf\xbf\xbf\xbf\xbf",
    "\xf4\x8f\xbf\xbf",
    "\xf4\x90\x80\x80",
    "\x80",
    "\xbf",
    "\x80\xbf",
    "\x80\xbf\x80",
    "\x80\xbf\x80\xbf",
    "\x80\xbf\x80\xbf\x80",
    "\x80\xbf\x80\xbf\x80\xbf",
    "\x80\xbf\x80\xbf\x80\xbf\x80",
    "\x80",
    "\x81",
    "\x82",
    "\x83",
    "\x84",
    "\x85",
    "\x86",
    "\x87",
    "\x88",
    "\x89",
    "\x8a",
    "\x8b",
    "\x8c",
    "\x8d",
    "\x8e",
    "\x8f",
    "\x90",
    "\x91",
    "\x92",
    "\x93",
    "\x94",
    "\x95",
    "\x96",
    "\x97",
    "\x98",
    "\x99",
    "\x9a",
    "\x9b",
    "\x9c",
    "\x9d",
    "\x9e",
    "\x9f",
    "\xa0",
    "\xa1",
    "\xa2",
    "\xa3",
    "\xa4",
    "\xa5",
    "\xa6",
    "\xa7",
    "\xa8",
    "\xa9",
    "\xaa",
    "\xab",
    "\xac",
    "\xad",
    "\xae",
    "\xaf",
    "\xb0",
    "\xb1",
    "\xb2",
    "\xb3",
    "\xb4",
    "\xb5",
    "\xb6",
    "\xb7",
    "\xb8",
    "\xb9",
    "\xba",
    "\xbb",
    "\xbc",
    "\xbd",
    "\xbe",
    "\xbf",
    "\xc0\x20",
    "\xc1\x20",
    "\xc2\x20",
    "\xc3\x20",
    "\xc4\x20",
    "\xc5\x20",
    "\xc6\x20",
    "\xc7\x20",
    "\xc8\x20",
    "\xc9\x20",
    "\xca\x20",
    "\xcb\x20",
    "\xcc\x20",
    "\xcd\x20",
    "\xce\x20",
    "\xcf\x20",
    "\xd0\x20",
    "\xd1\x20",
    "\xd2\x20",
    "\xd3\x20",
    "\xd4\x20",
    "\xd5\x20",
    "\xd6\x20",
    "\xd7\x20",
    "\xd8\x20",
    "\xd9\x20",
    "\xda\x20",
    "\xdb\x20",
    "\xdc\x20",
    "\xdd\x20",
    "\xde\x20",
    "\xdf\x20",
    "\xe0\x20",
    "\xe1\x20",
    "\xe2\x20",
    "\xe3\x20",
    "\xe4\x20",
    "\xe5\x20",
    "\xe6\x20",
    "\xe7\x20",
    "\xe8\x20",
    "\xe9\x20",
    "\xea\x20",
    "\xeb\x20",
    "\xec\x20",
    "\xed\x20",
    "\xee\x20",
    "\xef\x20",
    "\xf0\x20",
    "\xf1\x20",
    "\xf2\x20",
    "\xf3\x20",
    "\xf4\x20",
    "\xf5\x20",
    "\xf6\x20",
    "\xf7\x20",
    "\xf8\x20",
    "\xf9\x20",
    "\xfa\x20",
    "\xfb\x20",
    "\xfc\x20",
    "\xfd\x20",
    "\x20\xc0",
    "\x20\xe0\x80",
    "\x20\xf0\x80\x80",
    "\x20\xf8\x80\x80\x80",
    "\x20\xfc\x80\x80\x80\x80",
    "\x20\xdf",
    "\x20\xef\xbf",
    "\x20\xf7\xbf\xbf",
    "\x20\xfb\xbf\xbf\xbf",
    "\x20\xfd\xbf\xbf\xbf\xbf",
    "\x20\xfe\x20",
    "\x20\xff\x20",
    "\x20\xc0\xaf\x20",
    "\x20\xe0\x80\xaf\x20",
    "\x20\xf0\x80\x80\xaf\x20",
    "\x20\xf8\x80\x80\x80\xaf\x20",
    "\x20\xfc\x80\x80\x80\x80\xaf\x20",
    "\x20\xc1\xbf\x20",
    "\x20\xe0\x9f\xbf\x20",
    "\x20\xf0\x8f\xbf\xbf\x20",
    "\x20\xf8\x87\xbf\xbf\xbf\x20",
    "\x20\xfc\x83\xbf\xbf\xbf\xbf\x20",
    "\x20\xc0\x80\x20",
    "\x20\xe0\x80\x80\x20",
    "\x20\xf0\x80\x80\x80\x20",
    "\x20\xf8\x80\x80\x80\x80\x20",
    "\x20\xfc\x80\x80\x80\x80\x80\x20",
    "\x20\xed\xa0\x80\x20",
    "\x20\xed\xad\xbf\x20",
    "\x20\xed\xae\x80\x20",
    "\x20\xed\xaf\xbf\x20",
    "\x20\xed\xb0\x80\x20",
    "\x20\xed\xbe\x80\x20",
    "\x20\xed\xbf\xbf\x20",
    "\x20\xed\xa0\x80\xed\xb0\x80\x20",
    "\x20\xed\xa0\x80\xed\xbf\xbf\x20",
    "\x20\xed\xad\xbf\xed\xb0\x80\x20",
    "\x20\xed\xad\xbf\xed\xbf\xbf\x20",
    "\x20\xed\xae\x80\xed\xb0\x80\x20",
    "\x20\xed\xae\x80\xed\xbf\xbf\x20",
    "\x20\xed\xaf\xbf\xed\xb0\x80\x20",
    "\x20\xed\xaf\xbf\xed\xbf\xbf\x20",
    "\x20\xef\xbf\xbe\x20",
    "\x20\xef\xbf\xbf\x20",
    0
  };

  for (int i = 0; good_strings[i]; ++i)
    UNIT_TEST_CHECK(utf8_validate(utf8(good_strings[i])) == true);

  for (int i = 0; bad_strings[i]; ++i)
    UNIT_TEST_CHECK(utf8_validate(utf8(bad_strings[i])) == false);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
