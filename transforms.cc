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
#include "vocab.hh"
#include "transforms.hh"
#include "sanity.hh"
#include "url.hh"
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

struct lowerize
{
  char operator()(char const & c) const 
  { 
    return ::tolower(static_cast<int>(c)); 
  }
};

string lowercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), lowerize());
  return n;
}

struct upperize
{
  char operator()(char const & c) const 
  { 
    return ::toupper(static_cast<int>(c)); 
  }
};

string uppercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), upperize());
  return n;
}


// diffing and patching


void diff(data const & olddata,
	  data const & newdata,
	  base64< gzip<delta> > & del)
{
  string unpacked;
  compute_delta(olddata(), newdata(), unpacked);
  pack(delta(unpacked), del);
}

void patch(data const & olddata,
	   base64< gzip<delta> > const & del,
	   data & newdata)
{
  delta unpacked;
  unpack(del, unpacked);
  string result;
  apply_delta(olddata(), unpacked(), result);
  newdata = result;
}

void diff(manifest_map const & oldman,
	  manifest_map const & newman,
	  base64< gzip<delta> > & del)
{
  string xd;
  compute_delta(oldman, newman, xd);
  pack(delta(xd), del);
}

void diff(base64< gzip<data> > const & olddata,
	  base64< gzip<data> > const & newdata,
	  base64< gzip<delta> > & del)
{
  gzip<data> olddata_decoded;
  gzip<data> newdata_decoded;

  decode_base64(olddata, olddata_decoded);
  decode_base64(newdata, newdata_decoded);

  data olddata_decompressed;
  data newdata_decompressed;

  decode_gzip(olddata_decoded, olddata_decompressed);
  decode_gzip(newdata_decoded, newdata_decompressed);

  diff(olddata_decompressed,
       newdata_decompressed,
       del);
}

void patch(base64< gzip<data> > const & olddata,
	   base64< gzip<delta> > const & del,
	   base64< gzip<data> > & newdata)
{
  data olddata_unpacked, newdata_unpacked;
  unpack(olddata, olddata_unpacked);
  patch(olddata_unpacked, del, newdata_unpacked);
  pack(newdata_unpacked, newdata);
}


// identifier (a.k.a. sha1 signature) calculation

void calculate_ident(data const & dat,
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

void calculate_ident(base64< gzip<data> > const & dat,
		     hexenc<id> & ident)
{
  gzip<data> data_decoded;
  data data_decompressed;  
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);  
  calculate_ident(data_decompressed, ident);
}

void calculate_ident(file_data const & dat,
		     file_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

void calculate_ident(manifest_map const & m,
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

void calculate_ident(manifest_data const & dat,
		     manifest_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

// this might reasonably go in file_io.cc too..
void calculate_ident(file_path const & file,
		     hexenc<id> & ident)
{
  CryptoPP::SHA hash;
  unsigned int const sz = 2 * CryptoPP::SHA::DIGESTSIZE;
  char buffer[sz];
  CryptoPP::FileSource f(file().c_str(), true, new CryptoPP::HashFilter
			 (hash, new CryptoPP::HexEncoder
			  (new CryptoPP::ArraySink(reinterpret_cast<byte *>(buffer), sz))));
  ident = lowercase(string(buffer, sz));
}


void split_into_lines(string const & in,
		      vector<string> & out)
{
  typedef boost::tokenizer<boost::char_separator<char> > 
    tokenizer;
  boost::char_separator<char> sep("\r\n", "", boost::keep_empty_tokens);
  tokenizer tokens(in, sep);
  out.clear();
  copy(tokens.begin(), tokens.end(), back_inserter(out));
  if (out.size() > 0 
      && out.at(out.size()-1) == "")
    out.pop_back();
}

void join_lines(vector<string> const & in,
		string & out,
		string const & linesep)
{
  ostringstream oss;
  copy(in.begin(), in.end(), ostream_iterator<string>(oss, linesep.c_str()));
  out = oss.str();
}

void join_lines(vector<string> const & in,
		string & out)
{
  join_lines(in, out, "\n");
}

string remove_ws(string const & s)
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

string trim_ws(string const & s)
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

string canonical_base64(string const & s)
{
  return xform<CryptoPP::Base64Encoder>
    (xform<CryptoPP::Base64Decoder>(s));
}


// general character code conversion routines

static string system_charset(lua_hooks & lua)
{
  char const * locale_charset_name = stringprep_locale_charset ();
  I(locale_charset_name != NULL);
  string sys_charset(locale_charset_name);
  lua.hook_get_system_charset(sys_charset);
  return sys_charset;
}

void charset_convert(string const & src_charset,
		     string const & dst_charset,
		     string const & src, 
		     string & dst)
{
  L(F("converting %d bytes from %s to %s\n") % src.size() 
    % src_charset % dst_charset);
  char * converted = stringprep_convert(src.c_str(), 
					src_charset.c_str(), 
					dst_charset.c_str());
  I(converted != NULL);
  dst = string(converted);
  free(converted);
}


void system_to_utf8(string const & system, string & utf8, lua_hooks & lua)
{
  charset_convert(system_charset(lua), "UTF-8", system, utf8);
}

void utf8_to_system(string const & utf8, string & system, lua_hooks & lua)
{
  charset_convert("UTF-8", system_charset(lua), utf8, system);
}

static string decode_idna_error(int err)
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

void ace_to_utf8(string const & ace, string & utf8)
{
  char *out = NULL;
  L(F("converting %d bytes from IDNA ACE to UTF-8\n") % ace.size());
  int res = idna_to_unicode_8z8z(ace.c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS || res == IDNA_NO_ACE_PREFIX,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % ace.size()
    % decode_idna_error(res));
  utf8 = string(out);
  free(out);
}

void utf8_to_ace(string const & utf8, string & ace)
{
  char *out = NULL;
  L(F("converting %d bytes from UTF-8 to IDNA ACE\n") % utf8.size());
  int res = idna_to_ascii_8z(utf8.c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  N(res == IDNA_SUCCESS,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % utf8.size()
    % decode_idna_error(res));
  ace = string(out);
  free(out);
}

void utf8_to_urlenc(string const & utf8, string & urlenc)
{
  string ok_bytes(constants::legal_url_bytes);
  for (string::const_iterator i = utf8.begin(); i != utf8.end(); ++i)
    {
      if (ok_bytes.find(*i) == string::npos)
	urlenc += (F("%%%2.2x") % (0xff & static_cast<unsigned long>(*i))).str();
      else
	urlenc += *i;
    }
}

void urlenc_to_utf8(string const & urlenc, string & utf8)
{
  istringstream iss(urlenc);
  string ok_bytes(constants::legal_url_bytes);
  char c = 0;

  while (iss.get(c), iss.gcount() != 0)
    {
      if (c == '%')
	{
	  unsigned long val = 0;
	  iss >> std::hex >> val;
	  N(val > 0 && val <= 0xff,
	    F("bad URL-encoding escape value '%%%x'") % val);
	  utf8 += static_cast<char>(val);
	}
      else
	{
	  N(ok_bytes.find(c) != string::npos,
	    F("bad char 0x%x in URL-encoded string") % static_cast<unsigned long>(c));
	  utf8 += c;
	}
    }
}


// specific internal / external conversions for various vocab terms

void internalize_url(external const & ext, url & u, lua_hooks & lua)
{
  string utf8;
  system_to_utf8(ext(), utf8, lua);

  string proto, user, host, path, group;
  unsigned long port;
  N(parse_utf8_url(utf8, proto, user, host, path, group, port),
    F("UTF8-URL parse failed"));
  
  if (proto == "mailto")
    {
      string ace_user, ace_host;
      utf8_to_ace(user, ace_user);
      utf8_to_ace(host, ace_host);
      u = (F("mailto:%s@%s:%d") % ace_user % ace_host % port).str();
    }
  else if (proto == "http")
    {
      string urlenc_path, ace_host, ace_group;
      utf8_to_urlenc(path, urlenc_path);
      utf8_to_ace(host, ace_host);
      utf8_to_ace(group, ace_group);
      u = (F("http://%s:%d%s/%s") % ace_host % port % urlenc_path % ace_group).str();
    }
  else if (proto == "nntp")
    {
      string ace_host, ace_group;
      utf8_to_ace(host, ace_host);
      utf8_to_ace(group, ace_group);
      u = (F("nntp://%s:%d/%s") % ace_host % port % ace_group).str();
    }
  else
    {
      throw informative_failure("unknown URL protocol '" + proto + "'");
    }
  
  L(F("checking internalized URL '%s'\n") % u);
  N(parse_url(u, proto, user, host, path, group, port),
    F("confirmation parse of internalized URL '%s' failed") % u);
}


void externalize_url(url const & u, external & ext, lua_hooks & lua)
{
  string utf8, e;
  string proto, user, host, path, group;
  unsigned long port;

  L(F("externalizing URL '%s'\n") % u);
  N(parse_url(u(), proto, user, host, path, group, port),
    F("URL parse failed on '%s'") % u);
  
  if (proto == "mailto")
    {
      string utf8_user, utf8_host;
      ace_to_utf8(user, utf8_user);
      ace_to_utf8(host, utf8_host);
      utf8 = (F("mailto:%s@%s:%d") % utf8_user % utf8_host % port).str();
    }
  else if (proto == "http")
    {
      string utf8_path, utf8_host, utf8_group;
      urlenc_to_utf8(path, utf8_path);
      ace_to_utf8(host, utf8_host);
      ace_to_utf8(group, utf8_group);
      utf8 = (F("http://%s:%d%s/%s") % utf8_host % port % utf8_path % utf8_group).str();
    }
  else if (proto == "nntp")
    {
      string utf8_path, utf8_host, utf8_group;
      ace_to_utf8(host, utf8_host);
      ace_to_utf8(group, utf8_group);
      utf8 = (F("nntp://%s:%d/%s") % utf8_host % port % utf8_group).str();
    }
  else
    {
      throw informative_failure("unknown URL protocol '" + proto + "'");
    }

  N(parse_utf8_url(utf8, proto, user, host, path, group, port),
    F("confirmation parse of UTF8-URL failed"));
  utf8_to_system(utf8, e, lua);
  ext = e;
}


void internalize_cert_name(external const & ext, cert_name & c, lua_hooks & lua)
{
  string utf8, ace;
  system_to_utf8(ext(), utf8, lua);
  utf8_to_ace(utf8, ace);
  c = ace;
}

void externalize_cert_name(cert_name const & c, external & ext, lua_hooks & lua)
{
  string utf8, e;
  ace_to_utf8(c(), utf8);
  utf8_to_system(utf8, e, lua);
  ext = e;
}


void internalize_rsa_keypair_id(external const & ext, rsa_keypair_id & key, lua_hooks & lua)
{
  string utf8, ace;
  system_to_utf8(ext(), utf8, lua);
  typedef boost::tokenizer<boost::char_separator<char> > 
    tokenizer;
  boost::char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(utf8, sep);
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (*i == "." || *i == "@")
	ace += *i;
      else
	{
	  string a;
	  utf8_to_ace(*i, a);
	  ace += a;
	}
    }
  key = ace;
}

void externalize_rsa_keypair_id(rsa_keypair_id const & key, external & ext, lua_hooks & lua)
{
  string utf8, e;
  typedef boost::tokenizer<boost::char_separator<char> > 
    tokenizer;
  boost::char_separator<char> sep("", ".@", boost::keep_empty_tokens);
  tokenizer tokens(key(), sep);
  for(tokenizer::iterator i = tokens.begin(); i != tokens.end(); ++i)
    {
      if (*i == "." || *i == "@")
	utf8 += *i;
      else
	{
	  string u;
	  ace_to_utf8(*i, u);
	  utf8 += u;
	}
    }
  utf8_to_system(utf8, e, lua);
  ext = e;
}


void line_end_convert(string const & linesep, string const & src, string & dst)
{
  string linesep_str("\n");
  if (linesep == "CR" || linesep == "\r")
    linesep_str = "\r";
  else if (linesep == "CRLF" || linesep == "\r\n")
    linesep_str = "\r\n";
  else if (linesep == "LF"|| linesep == "\n")
    linesep_str = "\n";

  vector<string> tmp;
  split_into_lines(src, tmp);
  join_lines(tmp, dst, linesep_str);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void enc_test()
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

static void rdiff_test()
{
  data dat1(string("the first day of spring\nmakes me want to sing\n"));
  data dat2(string("the first day of summer\nis a major bummer\n"));
  data dat3;
  gzip<data> dat1_gz, dat2_gz, dat3_gz;
  base64< gzip<data> > dat1_bgz, dat2_bgz, dat3_bgz;
  encode_gzip(dat1, dat1_gz);
  encode_gzip(dat2, dat2_gz);
  encode_base64(dat1_gz, dat1_bgz);
  encode_base64(dat2_gz, dat2_bgz);
  base64< gzip<delta> > del_bgz;
  diff(dat1_bgz, dat2_bgz, del_bgz);
  
  patch(dat1_bgz, del_bgz, dat3_bgz);
  decode_base64(dat3_bgz, dat3_gz);
  decode_gzip(dat3_gz, dat3);
  BOOST_CHECK(dat3 == dat2);
}

static void calculate_ident_test()
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"));
  hexenc<id> output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  BOOST_CHECK(output() == ident);
}

static void caseconv_test()
{
  BOOST_CHECK(uppercase("hello") == "HELLO");
  BOOST_CHECK(uppercase("heLlO") == "HELLO");
  BOOST_CHECK(lowercase("POODLE DAY") == "poodle day");
  BOOST_CHECK(lowercase("PooDLe DaY") == "poodle day");
  BOOST_CHECK(uppercase("!@#$%^&*()") == "!@#$%^&*()");
  BOOST_CHECK(lowercase("!@#$%^&*()") == "!@#$%^&*()");
}

static void join_lines_test()
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

static void strip_ws_test()
{
  BOOST_CHECK(trim_ws("\n  leading space") == "leading space");
  BOOST_CHECK(trim_ws("trailing space  \n") == "trailing space");
  BOOST_CHECK(trim_ws("\t\n both \r \n\r\n") == "both");
  BOOST_CHECK(remove_ws("  I like going\tfor walks\n  ")
	      == "Ilikegoingforwalks");
}

#define IDNA_ACE_PREFIX "xn--"
#define IDNA_SUCCESS 0

struct idna
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

static void check_idna_encoding()
{
  lua_hooks lua;
  lua.add_std_hooks();
  lua.add_test_hooks();

  for (size_t i = 0; i < sizeof(idna_vec) / sizeof(struct idna); ++i)
    {
      BOOST_CHECKPOINT("IDNA language: " + string(idna_vec[i].name));

      size_t p, q;
      char *uc = stringprep_ucs4_to_utf8(idna_vec[i].in, 
					 idna_vec[i].inlen, 
					 &p, &q);
      string utf8(uc);
      free(uc);

      string ace(idna_vec[i].out);
      string tmp;
      utf8_to_ace(utf8, tmp);
      L(F("ACE-encoded %s: '%s'\n") % idna_vec[i].name % tmp);
      BOOST_CHECK(lowercase(ace) == lowercase(tmp));
      ace_to_utf8(ace, tmp);
      BOOST_CHECK(lowercase(utf8) == lowercase(tmp));

      external tmp_external;      
      url tmp_url;

      external utf8_host_url("http://" + utf8 + ":80/depot.cgi/path.to.group");
      url ace_host_url("http://" + ace + ":80/depot.cgi/path.to.group");

      internalize_url(utf8_host_url, tmp_url, lua);
      L(F("ACE-encoded %s: '%s'\n") % idna_vec[i].name % tmp_url());
      BOOST_CHECK(lowercase(ace_host_url()) == lowercase(tmp_url()));
      externalize_url(ace_host_url, tmp_external, lua);
      BOOST_CHECK(lowercase(tmp_external()) == lowercase(utf8_host_url()));

      external utf8_group_url("http://www.gurgle.com:80/depot.cgi/" + utf8);
      url ace_group_url("http://www.gurgle.com:80/depot.cgi/" + ace);

      internalize_url(utf8_group_url, tmp_url, lua);
      L(F("ACE-encoded %s: '%s'\n") % idna_vec[i].name % tmp_url());
      BOOST_CHECK(lowercase(ace_group_url()) == lowercase(tmp_url()));
      externalize_url(ace_group_url, tmp_external, lua);
      BOOST_CHECK(lowercase(tmp_external()) == lowercase(utf8_group_url()));
    }
}

static void check_url_encoding(string const & dec, string const & enc)
{
  string tmp1, tmp2;
  utf8_to_urlenc(dec, tmp1);
  L(F("URL-encoded to '%s'\n") % tmp1);
  BOOST_CHECK(enc == tmp1);
  urlenc_to_utf8(tmp1, tmp2);
  BOOST_CHECK(tmp2 == dec);
}

static void encode_test()
{
  check_url_encoding("hello\xF1there", "hello%f1there");
  check_url_encoding("hello\xF2there", "hello%f2there");
  check_url_encoding("hello\xF3there", "hello%f3there");
  check_url_encoding("hello\xF4there", "hello%f4there");
  check_url_encoding("hello\xF5there", "hello%f5there");
  check_url_encoding("hello\xF6there", "hello%f6there");
  check_url_encoding("hello\xE6there", "hello%e6there");
  check_url_encoding("hello\xD6there", "hello%d6there");
  check_url_encoding("hello\xC6there", "hello%c6there");
  check_url_encoding("\xC6there", "%c6there");
  check_url_encoding("hello\xC6", "hello%c6");
  check_url_encoding("hello\xC6\xA9there", "hello%c6%a9there");
  check_url_encoding("hello\xC6\xA9\xD4there", "hello%c6%a9%d4there");
  check_idna_encoding();
}

void add_transform_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&enc_test));
  suite->add(BOOST_TEST_CASE(&rdiff_test));
  suite->add(BOOST_TEST_CASE(&calculate_ident_test));
  suite->add(BOOST_TEST_CASE(&caseconv_test));
  suite->add(BOOST_TEST_CASE(&join_lines_test));
  suite->add(BOOST_TEST_CASE(&strip_ws_test));
  suite->add(BOOST_TEST_CASE(&encode_test));
}

#endif // BUILD_UNIT_TESTS
