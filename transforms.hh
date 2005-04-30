#ifndef __TRANSFORMS_HH__
#define __TRANSFORMS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "revision.hh"
#include "lua.hh"
#include "manifest.hh"
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

std::string encode_hexenc(std::string const & in);
std::string decode_hexenc(std::string const & in);

template <typename T>
void decode_hexenc(hexenc<T> const & in, T & out)
{ out = decode_hexenc(in()); }

template <typename T>
void encode_hexenc(T const & in, hexenc<T> & out)
{ out = encode_hexenc(in()); }


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
          delta & del);

void diff(manifest_map const & oldman,
          manifest_map const & newman,
          delta & del);

void patch(data const & olddata,
           delta const & del,
           data & newdata);


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

void calculate_ident(revision_data const & dat,
                     revision_id & ident);

void calculate_ident(revision_set const & cs,
                     revision_id & ident);


// quick streamy variant which doesn't necessarily load the whole file

void calculate_ident(file_path const & file,
                     hexenc<id> & ident, 
                     lua_hooks & lua);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out);

void join_lines(std::vector<std::string> const & in,
                std::string & out,
                std::string const & linesep);

void join_lines(std::vector<std::string> const & in,
                std::string & out);

void prefix_lines_with(std::string const & prefix,
                       std::string const & lines,
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
void system_to_utf8(external const & system, utf8 & utf);
void utf8_to_system(utf8 const & utf, external & system);
void ace_to_utf8(ace const & ac, utf8 & utf);
void utf8_to_ace(utf8 const & utf, ace & a);

fs::path localized(file_path const & path);
fs::path localized(local_path const & path);
fs::path localized(utf8 const & path);
std::string localized_as_string(file_path const & path);

// specific internal / external conversions for various vocab terms
void internalize_cert_name(utf8 const & utf, cert_name & c);
void internalize_cert_name(external const & ext, cert_name & c);
void externalize_cert_name(cert_name const & c, utf8 & utf);
void externalize_cert_name(cert_name const & c, external & ext);
void internalize_rsa_keypair_id(utf8 const & utf, rsa_keypair_id & key);
void internalize_rsa_keypair_id(external const & ext, rsa_keypair_id & key);
void externalize_rsa_keypair_id(rsa_keypair_id const & key, utf8 & utf);
void externalize_rsa_keypair_id(rsa_keypair_id const & key, external & ext);
void internalize_var_domain(utf8 const & utf, var_domain & d);
void internalize_var_domain(external const & ext, var_domain & d);
void externalize_var_domain(var_domain const & d, utf8 & utf);
void externalize_var_domain(var_domain const & d, external & ext);

// line-ending conversion
void line_end_convert(std::string const & linesep, std::string const & src, std::string & dst);



#endif // __TRANSFORMS_HH__
