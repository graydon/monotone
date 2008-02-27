#ifndef __TRANSFORMS_HH__
#define __TRANSFORMS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"

// this file contans various sorts of string transformations. each
// transformation should be self-explanatory from its type signature. see
// transforms.cc for the implementations (most of which are delegations to
// crypto++ and librsync)

namespace Botan {
  class Base64_Encoder;
  class Base64_Decoder;
  class Hex_Encoder;
  class Hex_Decoder;
  class Gzip_Compression;
  class Gzip_Decompression;
}

// this generic template cannot actually be used, except with the
// specializations given below.  give a compile error instead of a link
// error.
template<typename XFM> std::string xform(std::string const & in)
{
  enum dummy { d = (sizeof(struct xform_must_be_specialized_for_this_type)
                    == sizeof(XFM)) };
  return in; // avoid warnings about no return statement
}

// these specializations of the template are defined in transforms.cc
template<> std::string xform<Botan::Base64_Encoder>(std::string const &);
template<> std::string xform<Botan::Base64_Decoder>(std::string const &);
template<> std::string xform<Botan::Hex_Encoder>(std::string const &);
template<> std::string xform<Botan::Hex_Decoder>(std::string const &);
template<> std::string xform<Botan::Gzip_Compression>(std::string const &);
template<> std::string xform<Botan::Gzip_Decompression>(std::string const &);

// base64 encoding

template <typename T>
base64<T> encode_base64(T const & in)
{ return base64<T>(T(xform<Botan::Base64_Encoder>(in()))); }

template <typename T>
T decode_base64(base64<T> const & in)
{ return T(xform<Botan::Base64_Decoder>(in())); }

template <typename T>
T decode_base64_as(std::string const & in)
{
  return T(xform<Botan::Base64_Decoder>(in));
}
// hex encoding

template <typename T>
void encode_hexenc(T const & in, hexenc<T> & out)
{ out = hexenc<T>(T(xform<Botan::Hex_Encoder>(in()))); }

template <typename T>
void decode_hexenc(hexenc<T> const & in, T & out)
{ out = T(xform<Botan::Hex_Decoder>(in())); }

inline std::string encode_hexenc(std::string const & in)
{ return xform<Botan::Hex_Encoder>(in); }
inline std::string decode_hexenc(std::string const & in)
{ return xform<Botan::Hex_Decoder>(in); }


// gzip

template <typename T>
void encode_gzip(T const & in, gzip<T> & out)
{ out = gzip<T>(xform<Botan::Gzip_Compression>(in())); }

template <typename T>
void decode_gzip(gzip<T> const & in, T & out)
{ out = T(xform<Botan::Gzip_Decompression>(in())); }

// string variant for netsync
template <typename T>
void encode_gzip(std::string const & in, gzip<T> & out)
{ out = xform<Botan::Gzip_Compression>(in); }

// both at once (this is relatively common)
// these are usable for T = data and T = delta

template <typename T>
void pack(T const & in, base64< gzip<T> > & out);

template <typename T>
void unpack(base64< gzip<T> > const & in, T & out);


// version (a.k.a. sha1 fingerprint) calculation

void calculate_ident(data const & dat,
                     id & ident);

void calculate_ident(file_data const & dat,
                     file_id & ident);

void calculate_ident(manifest_data const & dat,
                     manifest_id & ident);

void calculate_ident(revision_data const & dat,
                     revision_id & ident);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __TRANSFORMS_HH__
