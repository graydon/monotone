// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <functional>
#include <iterator>
#include <sstream>
#include "vector.hh"

#include <boost/tokenizer.hpp>
#include <boost/scoped_array.hpp>

#include "botan/botan.h"
#include "botan/sha160.h"
#include "gzip.hh"

#include "cleanup.hh"
#include "constants.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "vocab.hh"
#include "xdelta.hh"
#include "char_classifiers.hh"

using std::string;

using boost::scoped_array;

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

NORETURN(static inline void error_in_transform(Botan::Exception & e));

static inline void
error_in_transform(Botan::Exception & e)
{
  // why do people make up their own out-of-memory exceptions?
  if (typeid(e) == typeid(Botan::Memory_Exhaustion))
    throw std::bad_alloc();

  // these classes can all indicate data corruption
  else if (typeid(e) == typeid(Botan::Encoding_Error)
           || typeid(e) == typeid(Botan::Decoding_Error)
           || typeid(e) == typeid(Botan::Stream_IO_Error)
           || typeid(e) == typeid(Botan::Integrity_Failure))
    {
      // clean up the what() string a little: throw away the
      // "botan: TYPE: " part...
      string w(e.what());
      string::size_type pos = w.find(':');
      pos = w.find(':', pos+1);
      w = string(w.begin() + pos + 2, w.end());

      // ... downcase the rest of it and replace underscores with spaces.
      for (string::iterator p = w.begin(); p != w.end(); p++)
        {
          *p = to_lower(*p);
          if (*p == '_')
            *p = ' ';
        }

      E(false,
        F("%s\n"
          "this may be due to a memory glitch, data corruption during\n"
          "a network transfer, corruption of your database or workspace,\n"
          "or a bug in monotone.  if the error persists, please contact\n"
          "%s for assistance.\n")
        % w % PACKAGE_BUGREPORT);
    }
  else
    throw;

  I(false);  // can't get here
}

// worker function for the visible functions below
namespace {
template<typename XFM> string xform(XFM * x, string const & in)
{
  string out;
  try
    {
      Botan::Pipe pipe(x);
      pipe.process_msg(in);
      out = pipe.read_all_as_string();
    }
  catch (Botan::Exception & e)
    {
      error_in_transform(e);
    }
  return out;
}
}

// full specializations for the usable cases of xform<XFM>()
// use extra error checking in base64 and hex decoding
#define SPECIALIZE_XFORM(T, carg) \
  template<> string xform<T>(string const &in) \
  { return xform(new T(carg), in); }

SPECIALIZE_XFORM(Botan::Base64_Encoder,);
SPECIALIZE_XFORM(Botan::Base64_Decoder, Botan::IGNORE_WS);
SPECIALIZE_XFORM(Botan::Hex_Encoder, Botan::Hex_Encoder::Lowercase);
SPECIALIZE_XFORM(Botan::Hex_Decoder, Botan::IGNORE_WS);
SPECIALIZE_XFORM(Botan::Gzip_Compression,);
SPECIALIZE_XFORM(Botan::Gzip_Decompression,);

template <typename T>
void pack(T const & in, base64< gzip<T> > & out)
{
  string tmp;
  tmp.reserve(in().size()); // FIXME: do some benchmarking and make this a constant::

  try
    {
      Botan::Pipe pipe(new Botan::Gzip_Compression(),
                       new Botan::Base64_Encoder);
      pipe.process_msg(in());
      tmp = pipe.read_all_as_string();
      out = base64< gzip<T> >(tmp);
    }
  catch (Botan::Exception & e)
    {
      error_in_transform(e);
    }
}

template <typename T>
void unpack(base64< gzip<T> > const & in, T & out)
{
  try
    {
      Botan::Pipe pipe(new Botan::Base64_Decoder(),
                       new Botan::Gzip_Decompression());
      pipe.process_msg(in());
      out = T(pipe.read_all_as_string());
    }
  catch (Botan::Exception & e)
    {
      error_in_transform(e);
    }
}

// specialise them
template void pack<data>(data const &, base64< gzip<data> > &);
template void pack<delta>(delta const &, base64< gzip<delta> > &);
template void unpack<data>(base64< gzip<data> > const &, data &);
template void unpack<delta>(base64< gzip<delta> > const &, delta &);

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
  newdata = data(result);
}

// identifier (a.k.a. sha1 signature) calculation

void
calculate_ident(data const & dat,
                hexenc<id> & ident)
{
  try
    {
      Botan::Pipe p(new Botan::Hash_Filter("SHA-160"),
                    new Botan::Hex_Encoder(Botan::Hex_Encoder::Lowercase));
      p.process_msg(dat());
      ident = hexenc<id>(p.read_all_as_string());
    }
  catch (Botan::Exception & e)
    {
      error_in_transform(e);
    }
}

void
calculate_ident(file_data const & dat,
                file_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = file_id(tmp);
}

void
calculate_ident(manifest_data const & dat,
                manifest_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = manifest_id(tmp);
}

void
calculate_ident(revision_data const & dat,
                revision_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = revision_id(tmp);
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include <stdlib.h>

UNIT_TEST(transform, enc)
{
  data d2, d1("the rain in spain");
  gzip<data> gzd1, gzd2;
  base64< gzip<data> > bgzd;
  encode_gzip(d1, gzd1);
  bgzd = encode_base64(gzd1);
  gzd2 = decode_base64(bgzd);
  UNIT_TEST_CHECK(gzd2 == gzd1);
  decode_gzip(gzd2, d2);
  UNIT_TEST_CHECK(d2 == d1);
}

UNIT_TEST(transform, rdiff)
{
  data dat1(string("the first day of spring\nmakes me want to sing\n"));
  data dat2(string("the first day of summer\nis a major bummer\n"));
  delta del;
  diff(dat1, dat2, del);

  data dat3;
  patch(dat1, del, dat3);
  UNIT_TEST_CHECK(dat3 == dat2);
}

UNIT_TEST(transform, calculate_ident)
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"));
  hexenc<id> output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  UNIT_TEST_CHECK(output() == ident);
}

UNIT_TEST(transform, corruption_check)
{
  data input(string("i'm so fragile, fragile when you're here"));
  gzip<data> gzd;
  encode_gzip(input, gzd);

  // fake a single-bit error
  string gzs = gzd();
  string::iterator i = gzs.begin();
  while (*i != '+')
    i++;
  *i = 'k';

  gzip<data> gzbad(gzs);
  data output;
  UNIT_TEST_CHECK_THROW(decode_gzip(gzbad, output), informative_failure);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
