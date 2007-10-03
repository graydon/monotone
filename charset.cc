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

void
ace_to_utf8(ace const & a, utf8 & utf)
{
  char *out = NULL;
  L(FL("converting %d bytes from IDNA ACE to UTF-8") % a().size());
  int res = idna_to_unicode_8z8z(a().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS || res == IDNA_NO_ACE_PREFIX,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % a().size()
    % decode_idna_error(res));
  utf = utf8(string(out));
  free(out);
}

void
utf8_to_ace(utf8 const & utf, ace & a)
{
  char *out = NULL;
  L(FL("converting %d bytes from UTF-8 to IDNA ACE") % utf().size());
  int res = idna_to_ascii_8z(utf().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % utf().size()
    % decode_idna_error(res));
  a = ace(string(out));
  free(out);
}

void
internalize_cert_name(utf8 const & utf, cert_name & c)
{
  ace a;
  utf8_to_ace(utf, a);
  c = cert_name(a());
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
          ace a;
          utf8_to_ace(utf8(*i), a);
          tmp += a();
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
          ace a(*i);
          utf8 u;
          ace_to_utf8(a, u);
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
  ace a;
  utf8_to_ace(utf, a);
  d = var_domain(a());
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
  ace_to_utf8(ace(d()), utf);
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
  size_t inlen;
  u32 in[100];
  char const * out;
  int allowunassigned;
  int usestd3asciirules;
  int toasciirc;
  int tounicoderc;
} const idna_vec[] =
  {
    {
      "Arabic (Egyptian)", 17,
      {
        0x0644, 0x064A, 0x0647, 0x0645, 0x0627, 0x0628, 0x062A, 0x0643,
        0x0644, 0x0645, 0x0648, 0x0634, 0x0639, 0x0631, 0x0628, 0x064A,
        0x061F},
      IDNA_ACE_PREFIX "egbpdaj6bu4bxfgehfvwxn", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Chinese (simplified)", 9,
      {
        0x4ED6, 0x4EEC, 0x4E3A, 0x4EC0, 0x4E48, 0x4E0D, 0x8BF4, 0x4E2D, 0x6587},
      IDNA_ACE_PREFIX "ihqwcrb4cv8a8dqg056pqjye", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Chinese (traditional)", 9,
      {
        0x4ED6, 0x5011, 0x7232, 0x4EC0, 0x9EBD, 0x4E0D, 0x8AAA, 0x4E2D, 0x6587},
      IDNA_ACE_PREFIX "ihqwctvzc91f659drss3x8bo0yb", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Czech", 22,
      {
        0x0050, 0x0072, 0x006F, 0x010D, 0x0070, 0x0072, 0x006F, 0x0073,
        0x0074, 0x011B, 0x006E, 0x0065, 0x006D, 0x006C, 0x0075, 0x0076,
        0x00ED, 0x010D, 0x0065, 0x0073, 0x006B, 0x0079},
      IDNA_ACE_PREFIX "Proprostnemluvesky-uyb24dma41a", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Hebrew", 22,
      {
        0x05DC, 0x05DE, 0x05D4, 0x05D4, 0x05DD, 0x05E4, 0x05E9, 0x05D5,
        0x05D8, 0x05DC, 0x05D0, 0x05DE, 0x05D3, 0x05D1, 0x05E8, 0x05D9,
        0x05DD, 0x05E2, 0x05D1, 0x05E8, 0x05D9, 0x05EA},
      IDNA_ACE_PREFIX "4dbcagdahymbxekheh6e0a7fei0b", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Hindi (Devanagari)", 30,
      {
        0x092F, 0x0939, 0x0932, 0x094B, 0x0917, 0x0939, 0x093F, 0x0928,
        0x094D, 0x0926, 0x0940, 0x0915, 0x094D, 0x092F, 0x094B, 0x0902,
        0x0928, 0x0939, 0x0940, 0x0902, 0x092C, 0x094B, 0x0932, 0x0938,
        0x0915, 0x0924, 0x0947, 0x0939, 0x0948, 0x0902},
      IDNA_ACE_PREFIX "i1baa7eci9glrd9b2ae1bj0hfcgg6iyaf8o0a1dig0cd", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese (kanji and hiragana)", 18,
      {
        0x306A, 0x305C, 0x307F, 0x3093, 0x306A, 0x65E5, 0x672C, 0x8A9E,
        0x3092, 0x8A71, 0x3057, 0x3066, 0x304F, 0x308C, 0x306A, 0x3044,
        0x306E, 0x304B},
      IDNA_ACE_PREFIX "n8jok5ay5dzabd5bym9f0cm5685rrjetr6pdxa", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Russian (Cyrillic)", 28,
      {
        0x043F, 0x043E, 0x0447, 0x0435, 0x043C, 0x0443, 0x0436, 0x0435,
        0x043E, 0x043D, 0x0438, 0x043D, 0x0435, 0x0433, 0x043E, 0x0432,
        0x043E, 0x0440, 0x044F, 0x0442, 0x043F, 0x043E, 0x0440, 0x0443,
        0x0441, 0x0441, 0x043A, 0x0438},
      IDNA_ACE_PREFIX "b1abfaaepdrnnbgefbadotcwatmq2g4l", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Spanish", 40,
      {
        0x0050, 0x006F, 0x0072, 0x0071, 0x0075, 0x00E9, 0x006E, 0x006F,
        0x0070, 0x0075, 0x0065, 0x0064, 0x0065, 0x006E, 0x0073, 0x0069,
        0x006D, 0x0070, 0x006C, 0x0065, 0x006D, 0x0065, 0x006E, 0x0074,
        0x0065, 0x0068, 0x0061, 0x0062, 0x006C, 0x0061, 0x0072, 0x0065,
        0x006E, 0x0045, 0x0073, 0x0070, 0x0061, 0x00F1, 0x006F, 0x006C},
      IDNA_ACE_PREFIX "PorqunopuedensimplementehablarenEspaol-fmd56a", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Vietnamese", 31,
      {
        0x0054, 0x1EA1, 0x0069, 0x0073, 0x0061, 0x006F, 0x0068, 0x1ECD,
        0x006B, 0x0068, 0x00F4, 0x006E, 0x0067, 0x0074, 0x0068, 0x1EC3,
        0x0063, 0x0068, 0x1EC9, 0x006E, 0x00F3, 0x0069, 0x0074, 0x0069,
        0x1EBF, 0x006E, 0x0067, 0x0056, 0x0069, 0x1EC7, 0x0074},
      IDNA_ACE_PREFIX "TisaohkhngthchnitingVit-kjcr8268qyxafd2f1b9g", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 8,
      {
        0x0033, 0x5E74, 0x0042, 0x7D44, 0x91D1, 0x516B, 0x5148, 0x751F},
      IDNA_ACE_PREFIX "3B-ww4c5e180e575a65lsy2b", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 24,
      {
        0x5B89, 0x5BA4, 0x5948, 0x7F8E, 0x6075, 0x002D, 0x0077, 0x0069,
        0x0074, 0x0068, 0x002D, 0x0053, 0x0055, 0x0050, 0x0045, 0x0052,
        0x002D, 0x004D, 0x004F, 0x004E, 0x004B, 0x0045, 0x0059, 0x0053},
      IDNA_ACE_PREFIX "-with-SUPER-MONKEYS-pc58ag80a8qai00g7n9n", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 25,
      {
        0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x002D, 0x0041, 0x006E,
        0x006F, 0x0074, 0x0068, 0x0065, 0x0072, 0x002D, 0x0057, 0x0061,
        0x0079, 0x002D, 0x305D, 0x308C, 0x305E, 0x308C, 0x306E, 0x5834,
        0x6240},
      IDNA_ACE_PREFIX "Hello-Another-Way--fc4qua05auwb3674vfr0b", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 8,
      {
        0x3072, 0x3068, 0x3064, 0x5C4B, 0x6839, 0x306E, 0x4E0B, 0x0032},
      IDNA_ACE_PREFIX "2-u9tlzr9756bt3uc0v", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 13,
      {
        0x004D, 0x0061, 0x006A, 0x0069, 0x3067, 0x004B, 0x006F, 0x0069,
        0x3059, 0x308B, 0x0035, 0x79D2, 0x524D},
      IDNA_ACE_PREFIX "MajiKoi5-783gue6qz075azm5e", 0, 0, 
      IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 9,
      {
        0x30D1, 0x30D5, 0x30A3, 0x30FC, 0x0064, 0x0065, 0x30EB, 0x30F3, 0x30D0},
      IDNA_ACE_PREFIX "de-jg4avhby1noc0d", 0, 0, IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Japanese", 7,
      {
        0x305D, 0x306E, 0x30B9, 0x30D4, 0x30FC, 0x30C9, 0x3067},
      IDNA_ACE_PREFIX "d9juau41awczczp", 0, 0, IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Greek", 8,
      {0x03b5, 0x03bb, 0x03bb, 0x03b7, 0x03bd, 0x03b9, 0x03ba, 0x03ac},
      IDNA_ACE_PREFIX "hxargifdar", 0, 0, IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Maltese (Malti)", 10,
      {0x0062, 0x006f, 0x006e, 0x0121, 0x0075, 0x0073, 0x0061, 0x0127,
       0x0127, 0x0061},
      IDNA_ACE_PREFIX "bonusaa-5bb1da", 0, 0, IDNA_SUCCESS, IDNA_SUCCESS},
    {
      "Russian (Cyrillic)", 28,
      {0x043f, 0x043e, 0x0447, 0x0435, 0x043c, 0x0443, 0x0436, 0x0435,
       0x043e, 0x043d, 0x0438, 0x043d, 0x0435, 0x0433, 0x043e, 0x0432,
       0x043e, 0x0440, 0x044f, 0x0442, 0x043f, 0x043e, 0x0440, 0x0443,
       0x0441, 0x0441, 0x043a, 0x0438},
      IDNA_ACE_PREFIX "b1abfaaepdrnnbgefbadotcwatmq2g4l", 0, 0,
      IDNA_SUCCESS, IDNA_SUCCESS},
  };

UNIT_TEST(charset, idna_encoding)
{
  putenv("CHARSET=UTF-8");

  for (size_t i = 0; i < sizeof(idna_vec) / sizeof(struct idna); ++i)
    {
      UNIT_TEST_CHECKPOINT(("IDNA language: " + string(idna_vec[i].name)).c_str());

      size_t p, q;
      char *uc = stringprep_ucs4_to_utf8(idna_vec[i].in,
                                         idna_vec[i].inlen,
                                         &p, &q);
      utf8 utf = utf8(uc);
      utf8 tutf;
      free(uc);

      ace a = ace(idna_vec[i].out);
      ace tace;
      utf8_to_ace(utf, tace);
      L(FL("ACE-encoded %s: '%s'") % idna_vec[i].name % tace());
      UNIT_TEST_CHECK(lowercase(a()) == lowercase(tace()));
      ace_to_utf8(a, tutf);
      UNIT_TEST_CHECK(lowercase(utf()) == lowercase(tutf()));
    }
}

UNIT_TEST(charset, utf8_validation)
{
  // these tests are based on the tests from the file utf8-validate.c of the
  // GLib library, and also include sequences from Markus Kuhn's UTF-8
  // example files.
  const char* good_strings[] = {
    "this is a valid but boring ASCII string",
    "\x28\x28\x56\xe2\x8d\xb3\x56\x29\x3d\xe2\x8d\xb3\xe2\x8d\xb4\x56\x29\x2f\x56\xe2\x86\x90\x2c\x56\x20\x20\x20\x20\xe2\x8c\xb7\xe2\x86\x90\xe2\x8d\xb3\xe2\x86\x92\xe2\x8d\xb4\xe2\x88\x86\xe2\x88\x87\xe2\x8a\x83\xe2\x80\xbe\xe2\x8d\x8e\xe2\x8d\x95\xe2\x8c\x88",
    "\xe2\x80\x98\x73\x69\x6e\x67\x6c\x65\xe2\x80\x99\x20\x61\x6e\x64\x20\xe2\x80\x9c\x64\x6f\x75\x62\x6c\x65\xe2\x80\x9d\x20\x71\x75\x6f\x74\x65\x73",
    "\xe2\x80\xa2\x20\x43\x75\x72\x6c\x79\x20\x61\x70\x6f\x73\x74\x72\x6f\x70\x68\x65\x73\x3a\x20\xe2\x80\x9c\x57\x65\xe2\x80\x99\x76\x65\x20\x62\x65\x65\x6e\x20\x68\x65\x72\x65\xe2\x80\x9d",
    "\xe2\x80\x9a\x64\x65\x75\x74\x73\x63\x68\x65\xe2\x80\x98\x20\xe2\x80\x9e\x41\x6e\x66\xc3\xbc\x68\x72\x75\x6e\x67\x73\x7a\x65\x69\x63\x68\x65\x6e\xe2\x80\x9c",
    "\xe2\x80\xa0\x2c\x20\xe2\x80\xa1\x2c\x20\xe2\x80\xb0\x2c\x20\xe2\x80\xa2\x2c\x20\x33\xe2\x80\x93\x34\x2c\x20\xe2\x80\x94\x2c\x20\xe2\x88\x92\x35\x2f\x2b\x35\x2c\x20\xe2\x84\xa2\x2c\x20\xe2\x80\xa6",
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
