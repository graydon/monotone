#ifndef __TRANSFORMS_HH__
#define __TRANSFORMS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include <vector>

// this file contans various sorts of string transformations. each
// transformation should be self-explanatory from its type signature. see
// transforms.cc for the implementations (most of which are delegations to
// crypto++ and librsync)

namespace CryptoPP {
  class Base64Encoder;
  class Base64Decoder;
  class HexEncoder;
  class HexDecoder;
  class Gzip;
  class Gunzip;
}

template<typename XFM> string xform(string const &);
extern template string xform<CryptoPP::Base64Encoder>(string const &);
extern template string xform<CryptoPP::Base64Decoder>(string const &);
extern template string xform<CryptoPP::HexEncoder>(string const &);
extern template string xform<CryptoPP::HexDecoder>(string const &);
extern template string xform<CryptoPP::Gzip>(string const &);
extern template string xform<CryptoPP::Gunzip>(string const &);

// base64 encoding

template <typename T>
void encode_base64(T const & in, base64<T> & out)
{ out = xform<CryptoPP::Base64Encoder>(in()); }

template <typename T>
void decode_base64(base64<T> const & in, T & out)
{ out = xform<CryptoPP::Base64Decoder>(in()); }


// hex encoding

string uppercase(string const & in);
string lowercase(string const & in);

template <typename T>
void decode_hexenc(hexenc<T> const & in, T & out)
{ out = xform<CryptoPP::HexDecoder>(uppercase(in())); }

template <typename T>
void encode_hexenc(T const & in, hexenc<T> & out)
{ out = lowercase(xform<CryptoPP::HexEncoder>(in())); }


// gzip

template <typename T>
void encode_gzip(T const & in, gzip<T> & out)
{ out = xform<CryptoPP::Gzip>(in()); }

template <typename T>
void decode_gzip(gzip<T> const & in, T & out)
{ out = xform<CryptoPP::Gunzip>(in()); }


// both at once (this is relatively common)

template <typename T>
void pack(T const & in, base64< gzip<T> > & out)
{
  gzip<T> tmp;
  encode_gzip(in, tmp);
  encode_base64(tmp, out);
}

template <typename T>
void unpack(base64< gzip<T> > const & in, T & out)
{
  gzip<T> tmp;
  decode_base64(in, tmp);
  decode_gzip(tmp, out);
}


// diffing and patching

void diff(data const & olddata,
	  data const & newdata,
	  base64< gzip<delta> > & del);

void diff(base64< gzip<data> > const & old_data,
	  base64< gzip<data> > const & new_data,
	  base64< gzip<delta> > & delta);

void patch(data const & olddata,
	   base64< gzip<delta> > const & del,
	   data & newdata);

void patch(base64< gzip<data> > const & old_data,
	   base64< gzip<delta> > const & delta,
	   base64< gzip<data> > & new_data);

// version (a.k.a. sha1 fingerprint) calculation

void calculate_ident(data const & dat,
		     hexenc<id> & ident);

void calculate_ident(base64< gzip<data> > const & dat,
		     hexenc<id> & ident);

void calculate_ident(file_data const & dat,
		     file_id & ident);

void calculate_ident(manifest_data const & dat,
		     manifest_id & ident);

// quick streamy variant which doesn't load the whole file

void calculate_ident(file_path const & file,
		     hexenc<id> & ident);

void split_into_lines(string const & in,
		      vector<string> & out);

void join_lines(vector<string> const & in,
		string & out);

// remove all whitespace
string remove_ws(string const & s);

// remove leading and trailing whitespace
string trim_ws(string const & s);

// canonicalize base64 encoding
string canonical_base64(string const & s);

#endif // __TRANSFORMS_HH__
