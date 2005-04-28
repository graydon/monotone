// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/tokenizer.hpp>

#include "cryptopp/filters.h"
#include "cryptopp/files.h"
#include "cryptopp/sha.h"
#include "cryptopp/hex.h"
#include "cryptopp/base64.h"
#include "cryptopp/gzip.h"

#include "idna/idna.h"
#include "idna/stringprep.h"

#include "cleanup.hh"
#include "constants.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "work.hh"
#include "xdelta.hh"

using namespace std;

// this file contans various sorts of string transformations. each
// transformation should be self-explanatory from its type signature. see
// transforms.hh for the summary.

// NB this file uses very "value-centric" functional approach; even though
// many of the underlying transformations are "stream-centric" and the
// underlying libraries (eg. crypto++) are stream oriented. this will
// probably strike some people as contemptably inefficient, since it means
// that occasionally 1, 2, or even 3 copies of an entire file will wind up
// in memory at once. I am taking this approach for 3 reasons: first, I
// want the type system to help me and value types are much easier to work
// with than stream types. second, it is *much* easier to debug a program
// that operates on values than streams, and correctness takes precedence
// over all other features of this program. third, this is a peer-to-peer
// sort of program for small-ish source-code text files, not a fileserver,
// and is memory-limited anyways (for example, storing things in sqlite
// requires they be able to fit in memory). you're hopefully not going to
// be dealing with hundreds of users hammering on locks and memory
// concurrently.
//
// if future analysis proves these assumptions wrong, feel free to revisit
// the matter, but bring strong evidence along with you that the stream
// paradigm "must" be used. this program is intended for source code
// control and I make no bones about it.

using namespace std;

// the generic function
template<typename XFM> string xform(string const & in)
{
  string out;
  out.reserve(in.size() * 2);
  CryptoPP::StringSource 
    str(in, true, 
        new XFM(new CryptoPP::StringSink(out)));
  return out;
}

// specialize it
template string xform<CryptoPP::Base64Encoder>(string const &);
template string xform<CryptoPP::Base64Decoder>(string const &);
template string xform<CryptoPP::HexEncoder>(string const &);
template string xform<CryptoPP::HexDecoder>(string const &);
template string xform<CryptoPP::Gzip>(string const &);
template string xform<CryptoPP::Gunzip>(string const &);

// for use in hexenc encoding

string encode_hexenc(string const & in)
{
  char buf[in.size() * 2];
  static char const *tab = "0123456789abcdef";
  char *c = buf;
  for (string::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      *c++ = tab[(*i >> 4) & 0xf];
      *c++ = tab[*i & 0xf];
    }
  return string(buf, in.size() * 2);        
}

static inline char decode_hex_char(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  I(false);
}

string decode_hexenc(string const & in)
{
  I(in.size() % 2 == 0);
  char buf[in.size() / 2];
  char *c = buf;
  for (string::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      char t = decode_hex_char(*i++);
      t <<= 4;
      t |= decode_hex_char(*i);
      *c++ = t;
    }
  return string(buf, in.size() / 2);        
}

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


// diffing and patching


void 
diff(data const & olddata,
     data const & newdata,
     delta & del)
{
  string unpacked;
  compute_delta(olddata(), newdata(), unpacked);
  del = delta(unpacked);
}

void 
patch(data const & olddata,
      delta const & del,
      data & newdata)
{
  string result;
  apply_delta(olddata(), del(), result);
  newdata = result;
}

void 
diff(manifest_map const & oldman,
     manifest_map const & newman,
     delta & del)
{
  string xd;
  compute_delta(oldman, newman, xd);
  del = delta(xd);
}

// identifier (a.k.a. sha1 signature) calculation

void 
calculate_ident(data const & dat,
                hexenc<id> & ident)
{
  CryptoPP::SHA hash;
  hash.Update(reinterpret_cast<byte const *>(dat().c_str()), 
              static_cast<unsigned int>(dat().size()));
  char digest[CryptoPP::SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  string out(digest, CryptoPP::SHA::DIGESTSIZE);
  id ident_decoded(out);
  encode_hexenc(ident_decoded, ident);  
}

void 
calculate_ident(base64< gzip<data> > const & dat,
                hexenc<id> & ident)
{
  gzip<data> data_decoded;
  data data_decompressed;  
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);  
  calculate_ident(data_decompressed, ident);
}

void 
calculate_ident(file_data const & dat,
                file_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

void 
calculate_ident(manifest_map const & m,
                manifest_id & ident)
{
  CryptoPP::SHA hash;
  size_t sz = 0;
  static size_t bufsz = 0;
  static char *buf = NULL;

  for (manifest_map::const_iterator i = m.begin();
       i != m.end(); ++i)
    {
      sz += i->second.inner()().size();
      sz += i->first().size();
      sz += 3;      
    }

  if (sz > bufsz)
    {
      bufsz = sz;
      buf = static_cast<char *>(realloc(buf, bufsz));
      I(buf);
    }
  
  // this has to go quite fast, for cvs importing
  char *c = buf;
  for (manifest_map::const_iterator i = m.begin();
       i != m.end(); ++i)
    {
      memcpy(c, i->second.inner()().data(), i->second.inner()().size());
      c += i->second.inner()().size();
      *c++ = ' '; 
      *c++ = ' '; 
      memcpy(c, i->first().data(), i->first().size());
      c += i->first().size();
      *c++ = '\n'; 
    }
  
  hash.Update(reinterpret_cast<byte const *>(buf), 
              static_cast<unsigned int>(sz));

  char digest[CryptoPP::SHA::DIGESTSIZE];
  hash.Final(reinterpret_cast<byte *>(digest));
  string out(digest, CryptoPP::SHA::DIGESTSIZE);
  id ident_decoded(out);
  hexenc<id> raw_ident;
  encode_hexenc(ident_decoded, raw_ident);  
  ident = manifest_id(raw_ident);    
}

void 
calculate_ident(manifest_data const & dat,
                manifest_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}


void calculate_ident(revision_data const & dat,
                     revision_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

void calculate_ident(revision_set const & cs,
                     revision_id & ident)
{
  data tmp;
  hexenc<id> tid;
  write_revision_set(cs, tmp);
  calculate_ident(tmp, tid);
  ident = tid;
}

// this might reasonably go in file_io.cc too...
void 
calculate_ident(file_path const & file,
                hexenc<id> & ident,
                lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;

  bool do_lineconv = (lua.hook_get_linesep_conv(file, db_linesep, ext_linesep) 
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(file, db_charset, ext_charset) 
                      && db_charset != ext_charset);

  if (do_charconv || do_lineconv)
    {
      data dat;
      read_localized_data(file, dat, lua);
      calculate_ident(dat, ident);
    }
  else
    {
      // no conversions necessary, use streaming form
      // still have to localize the filename
      fs::path localized_file = localized(file);
      // crypto++'s FileSource will simply treat directories as empty files,
      // so we'd better check ourselves.
      I(fs::exists(localized_file) && !fs::is_directory(localized_file));
      CryptoPP::SHA hash;
      unsigned int const sz = 2 * CryptoPP::SHA::DIGESTSIZE;
      char buffer[sz];
      CryptoPP::FileSource f(localized_file.native_file_string().c_str(),
                             true, new CryptoPP::HashFilter
                             (hash, new CryptoPP::HexEncoder
                              (new CryptoPP::ArraySink(reinterpret_cast<byte *>(buffer), sz))));
      ident = lowercase(string(buffer, sz));
    }
}

void split_into_lines(std::string const & in,                 
                      std::string const & encoding,
                      std::vector<std::string> & out)
{
  std::string lc_encoding = lowercase(encoding);  
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
  
  if (lc_encoding == default_encoding
      || lc_encoding.find("ascii") != std::string::npos
      || lc_encoding.find("8859") != std::string::npos
      || lc_encoding.find("euc") != std::string::npos
      || lc_encoding.find("koi") != std::string::npos
      || lc_encoding.find("gb") != std::string::npos
      || lc_encoding == "utf-8"
      || lc_encoding == "utf_8"
      || lc_encoding == "utf8")
    {
      std::string::size_type begin = 0;
      std::string::size_type end = in.find_first_of("\r\n", begin);

      while (end != std::string::npos && end >= begin)
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
      if (begin < in.size())
        out.push_back(in.substr(begin, in.size() - begin));
    }
  else
    {
      out.push_back(in);
    }
  if (out.size() == 0)
    out.push_back("");
}


void 
split_into_lines(string const & in,
                 vector<string> & out)
{
  split_into_lines(in, default_encoding, out);
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
  std::vector<std::string> msgs;
  split_into_lines(lines, msgs);

  ostringstream oss;
  for (std::vector<string>::const_iterator i = msgs.begin();
       i != msgs.end();)
    {
      oss << prefix << *i;
      i++;
      if (i != msgs.end())
        oss << endl;
    }
  
  out = oss.str();
}

string 
remove_ws(string const & s)
{
  string tmp;
  tmp.reserve(s.size());
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
          tmp += *i;
          break;
        }
    }
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

string 
canonical_base64(string const & s)
{
  return xform<CryptoPP::Base64Encoder>
    (xform<CryptoPP::Base64Decoder>(s));
}


// general character code conversion routines

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
                string & dst)
{
  if (src_charset == dst_charset)
    dst = src;
  else
    {
      L(F("converting %d bytes from %s to %s\n") % src.size() 
        % src_charset % dst_charset);
      char * converted = stringprep_convert(src.c_str(),
                                            dst_charset.c_str(),
                                            src_charset.c_str());
      E(converted != NULL,
        F("failed to convert string from %s to %s: '%s'")
         % src_charset % dst_charset % src);
      dst = string(converted);
      free(converted);
    }
}


void 
system_to_utf8(external const & ext, utf8 & utf)
{
  string out;
  charset_convert(system_charset(), "UTF-8", ext(), out);
  utf = out;
}

void 
utf8_to_system(utf8 const & utf, external & ext)
{
  string out;
  charset_convert("UTF-8", system_charset(), utf(), out);
  ext = out;
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
  L(F("converting %d bytes from IDNA ACE to UTF-8\n") % a().size());
  int res = idna_to_unicode_8z8z(a().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS || res == IDNA_NO_ACE_PREFIX,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % a().size()
    % decode_idna_error(res));
  utf = string(out);
  free(out);
}

void 
utf8_to_ace(utf8 const & utf, ace & a)
{
  char *out = NULL;
  L(F("converting %d bytes from UTF-8 to IDNA ACE\n") % utf().size());
  int res = idna_to_ascii_8z(utf().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % utf().size()
    % decode_idna_error(res));
  a = string(out);
  free(out);
}

// Lots of gunk to avoid charset conversion as much as possible.  Running
// iconv over every element of every path in a 30,000 file manifest takes
// multiple seconds, which then is a minimum bound on pretty much any
// operation we do...
static inline bool
filesystem_is_utf8_impl()
{
  std::string lc_encoding = lowercase(system_charset());
  return (lc_encoding == "utf-8"
          || lc_encoding == "utf_8"
          || lc_encoding == "utf8");
}

static inline bool
filesystem_is_utf8()
{
  static bool it_is = filesystem_is_utf8_impl();
  return it_is;
}

static inline bool
filesystem_is_ascii_extension_impl()
{
  if (filesystem_is_utf8())
    return true;
  std::string lc_encoding = lowercase(system_charset());
  // if your character set is identical to ascii in the lower 7 bits, then add
  // it here for a speed boost.
  return (lc_encoding.find("ascii") != std::string::npos
          || lc_encoding.find("8859") != std::string::npos
          || lc_encoding.find("ansi_x3.4") != std::string::npos
          // http://www.cs.mcgill.ca/~aelias4/encodings.html -- "EUC (Extended
          // Unix Code) is a simple and clean encoding, standard on Unix
          // systems.... It is backwards-compatible with ASCII (i.e. valid
          // ASCII implies valid EUC)."
          || lc_encoding.find("euc") != std::string::npos);
}

static inline bool
filesystem_is_ascii_extension()
{
  static bool it_is = filesystem_is_ascii_extension_impl();
  return it_is;
}

inline static fs::path 
localized_impl(string const & utf)
{
#ifdef __APPLE__
  // on OS X paths for the filesystem/kernel are UTF-8 encoded.
  return mkpath(utf);
#else
  if (filesystem_is_utf8())
    return mkpath(utf);
  if (filesystem_is_ascii_extension())
    {
      bool is_all_ascii = true;
      // could speed this up by vectorization -- mask against 0x80808080,
      // process a whole word at at time...
      for (std::string::const_iterator i = utf.begin(); i != utf.end(); ++i)
        if (0x80 & *i)
          {
            is_all_ascii = false;
            break;
          }
      if (is_all_ascii)
        return mkpath(utf);
    }
  fs::path tmp = mkpath(utf), ret;
  for (fs::path::iterator i = tmp.begin(); i != tmp.end(); ++i)
    {
      external ext;
      utf8_to_system(utf8(*i), ext);
      ret /= mkpath(ext());
    }
  return ret;
#endif
}

fs::path 
localized(file_path const & fp)
{
  return localized_impl(fp());
}

fs::path 
localized(local_path const & lp)
{
  return localized_impl(lp());
}

fs::path
localized(utf8 const & utf)
{
  return localized_impl(utf());
}


void 
internalize_cert_name(utf8 const & utf, cert_name & c)
{
  ace a;
  utf8_to_ace(utf, a);
  c = a();
}

void 
internalize_cert_name(external const & ext, cert_name & c)
{
  utf8 utf;
  system_to_utf8(ext(), utf);
  internalize_cert_name(utf, c);
}

void 
externalize_cert_name(cert_name const & c, utf8 & utf)
{
  ace_to_utf8(ace(c()), utf);
}

void 
externalize_cert_name(cert_name const & c, external & ext)
{
  utf8 utf;
  externalize_cert_name(c, utf);
  utf8_to_system(utf, ext);  
}

void 
internalize_rsa_keypair_id(utf8 const & utf, rsa_keypair_id & key)
{
  string tmp;
  typedef boost::tokenizer<boost::char_separator<char> > 
    tokenizer;
  boost::char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(utf(), sep);
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (*i == "." || *i == "@")
        tmp += *i;
      else
        {
          ace a;
          utf8_to_ace(*i, a);
          tmp += a();
        }
    }
  key = tmp;
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
  typedef boost::tokenizer<boost::char_separator<char> > 
    tokenizer;
  boost::char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(key(), sep);
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (*i == "." || *i == "@")
        tmp += *i;
      else
        {
          ace a(*i);
          utf8 u;
          ace_to_utf8(a, u);
          tmp += u();
        }
    }
  utf = tmp;
}

void 
externalize_rsa_keypair_id(rsa_keypair_id const & key, external & ext)
{
  utf8 utf;
  externalize_rsa_keypair_id(key, utf);
  utf8_to_system(utf, ext);
}

void 
internalize_var_domain(utf8 const & utf, var_domain & d)
{
  ace a;
  utf8_to_ace(utf, a);
  d = a();
}

void 
internalize_var_domain(external const & ext, var_domain & d)
{
  utf8 utf;
  system_to_utf8(ext(), utf);
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
  utf8_to_system(utf, ext);  
}

void 
line_end_convert(string const & linesep, string const & src, string & dst)
{
  string linesep_str("\n");
  if (linesep == "CR" || linesep == "\r")
    linesep_str = "\r";
  else if (linesep == "CRLF" || linesep == "\r\n")
    linesep_str = "\r\n";
  else if (linesep == "LF"|| linesep == "\n")
    linesep_str = "\n";

  L(F("doing linesep conversion to %s\n") % linesep);  
  vector<string> tmp;
  split_into_lines(src, tmp);
  join_lines(tmp, dst, linesep_str);
  if (src.size() >= linesep.size() &&
      (src.compare(src.size() - linesep.size(), linesep.size(), linesep) == 0))
    dst += linesep_str;
}

// glob_to_regexp converts a sh file glob to a regexp.  The regexp should
// be usable by the Boost regexp library.
//
// Pattern tranformation:
//
// - Any character except those described below are copied as they are.
// - The backslash (\) escapes the following character.  The escaping
//   backslash is copied to the regexp along with the following character.
// - * is transformed to .* in the regexp.
// - ? is transformed to . in the regexp.
// - { is transformed to ( in the regexp, unless within [ and ].
// - } is transformed to ) in the regexp, unless within [ and ].
// - , is transformed to | in the regexp, if within { and } and not
//    within [ and ].
// - ^ is escaped unless it comes directly after an unescaped [.
// - ! is transformed to ^ in the regexp if it comes directly after an
//   unescaped [.
// - ] directly following an unescaped [ is escaped.
string glob_to_regexp(const string & glob)
{
  int in_braces = 0;            // counter for levels if {}
  bool in_brackets = false;     // flags if we're inside a [], which
                                // has higher precedence than {}.
                                // Also, [ is accepted inside [] unescaped.
  bool this_was_opening_bracket = false;
  string tmp;

  tmp.reserve(glob.size() * 2);

#ifdef BUILD_UNIT_TESTS
  cerr << "DEBUG[glob_to_regexp]: input = \"" << glob << "\"" << endl;
#endif

  for (string::const_iterator i = glob.begin(); i != glob.end(); ++i)
    {
      char c = *i;
      bool last_was_opening_bracket = this_was_opening_bracket;
      this_was_opening_bracket = false;

      // Special case ^ and ! at the beginning of a [] expression.
      if (in_brackets && last_was_opening_bracket
          && (c == '!' || c == '^'))
        {
          tmp += '^';
          if (++i == glob.end())
            break;
          c = *i;
        }

      if (c == '\\')
        {
          tmp += c;
          if (++i == glob.end())
            break;
          tmp += *i;
        }
      else if (in_brackets)
        {
          switch(c)
            {
            case ']':
              if (!last_was_opening_bracket)
                {
                  in_brackets = false;
                  tmp += c;
                  break;
                }
              // Trickling through to the standard character conversion,
              // because ] as the first character of a set is regarded as
              // a normal character.
            default:
              if (!(isalnum(c) || c == '_'))
                {
                  tmp += '\\';
                }
              tmp += c;
              break;
            }
        }
      else
        {
          switch(c)
            {
            case '*':
              tmp += ".*";
              break;
            case '?':
              tmp += '.';
              break;
            case '{':
              in_braces++;
              tmp += '(';
              break;
            case '}':
              N(in_braces != 0,
                F("trying to end a brace expression in a glob when none is started"));
              tmp += ')';
              in_braces--;
              break;
            case '[':
              in_brackets = true;
              this_was_opening_bracket = true;
              tmp += c;
              break;
            case ',':
              if (in_braces > 0)
                {
                  tmp += '|';
                  break;
                }
              // Trickling through to default: here, since a comma outside of
              // brace notation is just a normal character.
            default:
              if (!(isalnum(c) || c == '_'))
                {
                  tmp += '\\';
                }
              tmp += c;
              break;
            }
        }
    }

  N(!in_brackets,
    F("run-away bracket expression in glob"));
  N(in_braces == 0,
    F("run-away brace expression in glob"));

#ifdef BUILD_UNIT_TESTS
  cerr << "DEBUG[glob_to_regexp]: output = \"" << tmp << "\"" << endl;
#endif

  return tmp;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void 
enc_test()
{
  data d2, d1("the rain in spain");
  gzip<data> gzd1, gzd2;
  base64< gzip<data> > bgzd;
  encode_gzip(d1, gzd1);
  encode_base64(gzd1, bgzd);
  decode_base64(bgzd, gzd2);
  BOOST_CHECK(gzd2 == gzd1);
  decode_gzip(gzd2, d2);
  BOOST_CHECK(d2 == d1);
}

static void 
rdiff_test()
{
  data dat1(string("the first day of spring\nmakes me want to sing\n"));
  data dat2(string("the first day of summer\nis a major bummer\n"));
  delta del;
  diff(dat1, dat2, del);
  
  data dat3;
  patch(dat1, del, dat3);
  BOOST_CHECK(dat3 == dat2);
}

static void 
calculate_ident_test()
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"));
  hexenc<id> output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  BOOST_CHECK(output() == ident);
}

static void 
caseconv_test()
{
  BOOST_CHECK(uppercase("hello") == "HELLO");
  BOOST_CHECK(uppercase("heLlO") == "HELLO");
  BOOST_CHECK(lowercase("POODLE DAY") == "poodle day");
  BOOST_CHECK(lowercase("PooDLe DaY") == "poodle day");
  BOOST_CHECK(uppercase("!@#$%^&*()") == "!@#$%^&*()");
  BOOST_CHECK(lowercase("!@#$%^&*()") == "!@#$%^&*()");
}

static void 
join_lines_test()
{
  vector<string> strs;
  string joined;

  strs.clear();
  join_lines(strs, joined);
  BOOST_CHECK(joined == "");

  strs.push_back("hi");
  join_lines(strs, joined);
  BOOST_CHECK(joined == "hi\n");

  strs.push_back("there");
  join_lines(strs, joined);
  BOOST_CHECK(joined == "hi\nthere\n");

  strs.push_back("user");
  join_lines(strs, joined);
  BOOST_CHECK(joined == "hi\nthere\nuser\n");  
}

static void 
strip_ws_test()
{
  BOOST_CHECK(trim_ws("\n  leading space") == "leading space");
  BOOST_CHECK(trim_ws("trailing space  \n") == "trailing space");
  BOOST_CHECK(trim_ws("\t\n both \r \n\r\n") == "both");
  BOOST_CHECK(remove_ws("  I like going\tfor walks\n  ")
              == "Ilikegoingforwalks");
}

#define IDNA_ACE_PREFIX "xn--"
#define IDNA_SUCCESS 0

struct 
idna
{
  char *name;
  size_t inlen;
  uint32_t in[100];
  char *out;
  int allowunassigned;
  int usestd3asciirules;
  int toasciirc;
  int tounicoderc;
} idna_vec[] =
  {
    {
      "Arabic (Egyptian)", 17,
      {
        0x0644, 0x064A, 0x0647, 0x0645, 0x0627, 0x0628, 0x062A, 0x0643,
        0x0644, 0x0645, 0x0648, 0x0634, 0x0639, 0x0631, 0x0628, 0x064A,
        0x061F},
      IDNA_ACE_PREFIX "egbpdaj6bu4bxfgehfvwxn", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Chinese (simplified)", 9,
      {
        0x4ED6, 0x4EEC, 0x4E3A, 0x4EC0, 0x4E48, 0x4E0D, 0x8BF4, 0x4E2D, 0x6587},
      IDNA_ACE_PREFIX "ihqwcrb4cv8a8dqg056pqjye", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Chinese (traditional)", 9,
      {
        0x4ED6, 0x5011, 0x7232, 0x4EC0, 0x9EBD, 0x4E0D, 0x8AAA, 0x4E2D, 0x6587},
      IDNA_ACE_PREFIX "ihqwctvzc91f659drss3x8bo0yb", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Czech", 22,
      {
        0x0050, 0x0072, 0x006F, 0x010D, 0x0070, 0x0072, 0x006F, 0x0073,
        0x0074, 0x011B, 0x006E, 0x0065, 0x006D, 0x006C, 0x0075, 0x0076,
        0x00ED, 0x010D, 0x0065, 0x0073, 0x006B, 0x0079},
      IDNA_ACE_PREFIX "Proprostnemluvesky-uyb24dma41a", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Hebrew", 22,
      {
        0x05DC, 0x05DE, 0x05D4, 0x05D4, 0x05DD, 0x05E4, 0x05E9, 0x05D5,
        0x05D8, 0x05DC, 0x05D0, 0x05DE, 0x05D3, 0x05D1, 0x05E8, 0x05D9,
        0x05DD, 0x05E2, 0x05D1, 0x05E8, 0x05D9, 0x05EA},
      IDNA_ACE_PREFIX "4dbcagdahymbxekheh6e0a7fei0b", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Hindi (Devanagari)", 30,
      {
        0x092F, 0x0939, 0x0932, 0x094B, 0x0917, 0x0939, 0x093F, 0x0928,
        0x094D, 0x0926, 0x0940, 0x0915, 0x094D, 0x092F, 0x094B, 0x0902,
        0x0928, 0x0939, 0x0940, 0x0902, 0x092C, 0x094B, 0x0932, 0x0938,
        0x0915, 0x0924, 0x0947, 0x0939, 0x0948, 0x0902},
      IDNA_ACE_PREFIX "i1baa7eci9glrd9b2ae1bj0hfcgg6iyaf8o0a1dig0cd", 0, 0,
      IDNA_SUCCESS},
    {
      "Japanese (kanji and hiragana)", 18,
      {
        0x306A, 0x305C, 0x307F, 0x3093, 0x306A, 0x65E5, 0x672C, 0x8A9E,
        0x3092, 0x8A71, 0x3057, 0x3066, 0x304F, 0x308C, 0x306A, 0x3044,
        0x306E, 0x304B},
      IDNA_ACE_PREFIX "n8jok5ay5dzabd5bym9f0cm5685rrjetr6pdxa", 0, 0,
      IDNA_SUCCESS},
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
      IDNA_SUCCESS},
    {
      "Vietnamese", 31,
      {
        0x0054, 0x1EA1, 0x0069, 0x0073, 0x0061, 0x006F, 0x0068, 0x1ECD,
        0x006B, 0x0068, 0x00F4, 0x006E, 0x0067, 0x0074, 0x0068, 0x1EC3,
        0x0063, 0x0068, 0x1EC9, 0x006E, 0x00F3, 0x0069, 0x0074, 0x0069,
        0x1EBF, 0x006E, 0x0067, 0x0056, 0x0069, 0x1EC7, 0x0074},
      IDNA_ACE_PREFIX "TisaohkhngthchnitingVit-kjcr8268qyxafd2f1b9g", 0, 0,
      IDNA_SUCCESS},
    {
      "Japanese", 8,
      {
        0x0033, 0x5E74, 0x0042, 0x7D44, 0x91D1, 0x516B, 0x5148, 0x751F},
      IDNA_ACE_PREFIX "3B-ww4c5e180e575a65lsy2b", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Japanese", 24,
      {
        0x5B89, 0x5BA4, 0x5948, 0x7F8E, 0x6075, 0x002D, 0x0077, 0x0069,
        0x0074, 0x0068, 0x002D, 0x0053, 0x0055, 0x0050, 0x0045, 0x0052,
        0x002D, 0x004D, 0x004F, 0x004E, 0x004B, 0x0045, 0x0059, 0x0053},
      IDNA_ACE_PREFIX "-with-SUPER-MONKEYS-pc58ag80a8qai00g7n9n", 0, 0,
      IDNA_SUCCESS},
    {
      "Japanese", 25,
      {
        0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x002D, 0x0041, 0x006E,
        0x006F, 0x0074, 0x0068, 0x0065, 0x0072, 0x002D, 0x0057, 0x0061,
        0x0079, 0x002D, 0x305D, 0x308C, 0x305E, 0x308C, 0x306E, 0x5834,
        0x6240},
      IDNA_ACE_PREFIX "Hello-Another-Way--fc4qua05auwb3674vfr0b", 0, 0,
      IDNA_SUCCESS},
    {
      "Japanese", 8,
      {
        0x3072, 0x3068, 0x3064, 0x5C4B, 0x6839, 0x306E, 0x4E0B, 0x0032},
      IDNA_ACE_PREFIX "2-u9tlzr9756bt3uc0v", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
    {
      "Japanese", 13,
      {
        0x004D, 0x0061, 0x006A, 0x0069, 0x3067, 0x004B, 0x006F, 0x0069,
        0x3059, 0x308B, 0x0035, 0x79D2, 0x524D},
      IDNA_ACE_PREFIX "MajiKoi5-783gue6qz075azm5e", 0, 0, IDNA_SUCCESS,
      IDNA_SUCCESS},
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

static void 
check_idna_encoding()
{
  putenv("CHARSET=UTF-8");

  for (size_t i = 0; i < sizeof(idna_vec) / sizeof(struct idna); ++i)
    {
      BOOST_CHECKPOINT("IDNA language: " + string(idna_vec[i].name));

      size_t p, q;
      char *uc = stringprep_ucs4_to_utf8(idna_vec[i].in, 
                                         idna_vec[i].inlen, 
                                         &p, &q);
      utf8 utf = string(uc);
      utf8 tutf;
      free(uc);

      ace a = string(idna_vec[i].out);
      ace tace;
      utf8_to_ace(utf, tace);
      L(F("ACE-encoded %s: '%s'\n") % idna_vec[i].name % tace());
      BOOST_CHECK(lowercase(a()) == lowercase(tace()));
      ace_to_utf8(a, tutf);
      BOOST_CHECK(lowercase(utf()) == lowercase(tutf()));
    }
}

static void encode_test()
{
  check_idna_encoding();
}

static void glob_to_regexp_test()
{
  BOOST_CHECK(glob_to_regexp("abc,v") == "abc\\,v");
  BOOST_CHECK(glob_to_regexp("foo[12m,]") == "foo[12m\\,]");
  // A full fledged, use all damn features test...
  BOOST_CHECK(glob_to_regexp("foo.{bar*,cookie?{haha,hehe[^\\123!,]}}[!]a^b]")
              == "foo\\.(bar.*|cookie.(haha|hehe[^\\123\\!\\,]))[^\\]a\\^b]");
}

void 
add_transform_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&enc_test));
  suite->add(BOOST_TEST_CASE(&rdiff_test));
  suite->add(BOOST_TEST_CASE(&calculate_ident_test));
  suite->add(BOOST_TEST_CASE(&caseconv_test));
  suite->add(BOOST_TEST_CASE(&join_lines_test));
  suite->add(BOOST_TEST_CASE(&strip_ws_test));
  suite->add(BOOST_TEST_CASE(&encode_test));
  suite->add(BOOST_TEST_CASE(&glob_to_regexp_test));
}

#endif // BUILD_UNIT_TESTS
