#ifndef __TRANSFORMS_HH__
#define __TRANSFORMS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "manifest.hh"
#include "lua.hh"

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

template<typename XFM> std::string xform(std::string const &);
extern template std::string xform<CryptoPP::Base64Encoder>(std::string const &);
extern template std::string xform<CryptoPP::Base64Decoder>(std::string const &);
extern template std::string xform<CryptoPP::HexEncoder>(std::string const &);
extern template std::string xform<CryptoPP::HexDecoder>(std::string const &);
extern template std::string xform<CryptoPP::Gzip>(std::string const &);
extern template std::string xform<CryptoPP::Gunzip>(std::string const &);

// base64 encoding

template <typename T>
void encode_base64(T const & in, base64<T> & out)
{ out = xform<CryptoPP::Base64Encoder>(in()); }

template <typename T>
void decode_base64(base64<T> const & in, T & out)
{ out = xform<CryptoPP::Base64Decoder>(in()); }


// hex encoding

std::string uppercase(std::string const & in);
std::string lowercase(std::string const & in);

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

void diff(manifest_map const & oldman,
	  manifest_map const & newman,
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

void calculate_ident(manifest_map const & mm,
		     manifest_id & ident);


// quick streamy variant which doesn't load the whole file

void calculate_ident(file_path const & file,
		     hexenc<id> & ident);

void split_into_lines(std::string const & in,
		      std::vector<std::string> & out);

void join_lines(std::vector<std::string> const & in,
		std::string & out,
		std::string const & linesep);

void join_lines(std::vector<std::string> const & in,
		std::string & out);

// remove all whitespace
std::string remove_ws(std::string const & s);

// remove leading and trailing whitespace
std::string trim_ws(std::string const & s);

// canonicalize base64 encoding
std::string canonical_base64(std::string const & s);

// charset conversions
void charset_convert(std::string const & src_charset, std::string const & dst_charset,
		     std::string const & src, std::string & dst);
void system_to_utf8(std::string const & system, std::string & utf8, lua_hooks & lua);
void utf8_to_system(std::string const & utf8, std::string & system, lua_hooks & lua);
void ace_to_utf8(std::string const & ace, std::string & utf8);
void utf8_to_ace(std::string const & utf8, std::string & ace);

// line-ending conversion
void line_end_convert(std::string const & linesep, std::string const & src, std::string & dst);

#endif // __TRANSFORMS_HH__
