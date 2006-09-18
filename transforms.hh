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

#include <vector>

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

#ifdef HAVE_EXTERN_TEMPLATE
#define EXTERN extern
#else
#define EXTERN /* */
#endif

template<typename XFM> std::string xform(std::string const &);
EXTERN template std::string xform<Botan::Base64_Encoder>(std::string const &);
EXTERN template std::string xform<Botan::Base64_Decoder>(std::string const &);
EXTERN template std::string xform<Botan::Hex_Encoder>(std::string const &);
EXTERN template std::string xform<Botan::Hex_Decoder>(std::string const &);
EXTERN template std::string xform<Botan::Gzip_Compression>(std::string const &);
EXTERN template std::string xform<Botan::Gzip_Decompression>(std::string const &);

// base64 encoding

template <typename T>
void encode_base64(T const & in, base64<T> & out)
{ out = xform<Botan::Base64_Encoder>(in()); }

template <typename T>
void decode_base64(base64<T> const & in, T & out)
{ out = xform<Botan::Base64_Decoder>(in()); }


// hex encoding

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
{ out = xform<Botan::Gzip_Compression>(in()); }

template <typename T>
void decode_gzip(gzip<T> const & in, T & out)
{ out = xform<Botan::Gzip_Decompression>(in()); }

// string variant for netsync
template <typename T>
void encode_gzip(std::string const & in, gzip<T> & out)
{ out = xform<Botan::Gzip_Compression>(in); }

// both at once (this is relatively common)

template <typename T>
void pack(T const & in, base64< gzip<T> > & out);
EXTERN template void pack<data>(data const &, base64< gzip<data> > &);
EXTERN template void pack<delta>(delta const &, base64< gzip<delta> > &);

template <typename T>
void unpack(base64< gzip<T> > const & in, T & out);
EXTERN template void unpack<data>(base64< gzip<data> > const &, data &);
EXTERN template void unpack<delta>(base64< gzip<delta> > const &, delta &);


// diffing and patching

void diff(data const & olddata,
          data const & newdata,
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

void calculate_ident(revision_data const & dat,
                     revision_id & ident);

// canonicalize base64 encoding
std::string canonical_base64(std::string const & s);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __TRANSFORMS_HH__
