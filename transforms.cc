// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <locale>

#include <boost/tokenizer.hpp>

#include "cryptopp/filters.h"
#include "cryptopp/files.h"
#include "cryptopp/sha.h"
#include "cryptopp/hex.h"
#include "cryptopp/base64.h"
#include "cryptopp/gzip.h"

#include "cleanup.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "sanity.hh"
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

string lowercase(string const & in)
{
  size_t const sz = in.size();
  char buf[sz];
  in.copy(buf, sz);
  use_facet< ctype<char> >(locale::locale()).tolower(buf, buf+sz);
  return string(buf,sz);
}

string uppercase(string const & in)
{
  size_t const sz = in.size();
  char buf[sz];
  in.copy(buf, sz);
  use_facet< ctype<char> >(locale::locale()).toupper(buf, buf+sz);
  return string(buf,sz);
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
		string & out)
{
  ostringstream oss;
  // FIXME: shall we bother joining lines as CRLF?
  copy(in.begin(), in.end(), ostream_iterator<string>(oss, "\n"));
  out = oss.str();
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

void add_transform_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&enc_test));
  suite->add(BOOST_TEST_CASE(&rdiff_test));
  suite->add(BOOST_TEST_CASE(&calculate_ident_test));
  suite->add(BOOST_TEST_CASE(&caseconv_test));
  suite->add(BOOST_TEST_CASE(&join_lines_test));
  suite->add(BOOST_TEST_CASE(&strip_ws_test));
}

#endif // BUILD_UNIT_TESTS
