// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iterator>
#include "botan_pipe_cache.hh"
#include "botan/botan.h"
#include "botan/sha160.h"
#include "gzip.hh"

#include "transforms.hh"
#include "xdelta.hh"
#include "char_classifiers.hh"

using std::string;
using Botan::Pipe;
using Botan::Base64_Encoder;
using Botan::Base64_Decoder;
using Botan::Hex_Encoder;
using Botan::Hex_Decoder;
using Botan::Gzip_Compression;
using Botan::Gzip_Decompression;
using Botan::Hash_Filter;

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
  // these classes can all indicate data corruption
  if (typeid(e) == typeid(Botan::Encoding_Error)
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

// full specializations for the usable cases of xform<XFM>()
// use extra error checking in base64 and hex decoding
#define SPECIALIZE_XFORM(T, carg)                               \
  template<> string xform<T>(string const & in)                 \
  {                                                             \
    string out;                                                 \
    try                                                         \
      {                                                         \
        static cached_botan_pipe pipe(new Pipe(new T(carg)));   \
        /* this might actually be a problem here */             \
        I(pipe->message_count() < Pipe::LAST_MESSAGE);          \
        pipe->process_msg(in);                                  \
        out = pipe->read_all_as_string(Pipe::LAST_MESSAGE);     \
      }                                                         \
    catch (Botan::Exception & e)                                \
      {                                                         \
        error_in_transform(e);                                  \
      }                                                         \
    return out;                                                 \
  }

SPECIALIZE_XFORM(Base64_Encoder,);
SPECIALIZE_XFORM(Base64_Decoder, Botan::IGNORE_WS);
//SPECIALIZE_XFORM(Hex_Encoder, Hex_Encoder::Lowercase);
template<> string xform<Botan::Hex_Encoder>(string const & in)
{
  string out;
  out.reserve(in.size()<<1);
  for (string::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      int h = (*i>>4) & 0x0f;
      if (h < 10)
        out.push_back(h + '0');
      else
        out.push_back(h + 'a' - 10);
      int l = *i & 0x0f;
      if (l < 10)
        out.push_back(l + '0');
      else
        out.push_back(l + 'a' - 10);
    }
  return out;
}
//SPECIALIZE_XFORM(Hex_Decoder, Botan::IGNORE_WS);
template<> string xform<Botan::Hex_Decoder>(string const & in)
{
  string out;
  out.reserve(in.size()>>1);
  bool high(true);
  int o = 0;
  for (string::const_iterator i = in.begin();
       i != in.end(); ++i)
    {
      int c = *i;
      if (c >= '0' && c <= '9')
        {
          o += (c - '0');
        }
      else if (c >= 'a' && c <= 'f')
        {
          o += (c - 'a' + 10);
        }
      else if (c >= 'A' && c <= 'F')
        {
          o += (c - 'A' + 10);
        }
      else if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
          continue;
        }
      else // garbage
        {
          try
            {
              throw Botan::Decoding_Error(string("invalid hex character '") + (char)c + "'");
            }
          catch(Botan::Exception & e)
            {
              error_in_transform(e);
            }
        }
      if (high)
        {
          o <<= 4;
        }
      else
        {
          out.push_back(o);
          o = 0;
        }
      high = !high;
    }
  if (!high)
    { // Hex string wasn't a whole number of bytes
      //I(false); // Drop the last char (!!)
    }
  return out;
}
SPECIALIZE_XFORM(Gzip_Compression,);
SPECIALIZE_XFORM(Gzip_Decompression,);

template <typename T>
void pack(T const & in, base64< gzip<T> > & out)
{
  string tmp;
  tmp.reserve(in().size()); // FIXME: do some benchmarking and make this a constant::

  try
    {
      static cached_botan_pipe pipe(new Pipe(new Gzip_Compression,
                                             new Base64_Encoder));
      pipe->process_msg(in());
      tmp = pipe->read_all_as_string(Pipe::LAST_MESSAGE);
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
      static cached_botan_pipe pipe(new Pipe(new Base64_Decoder,
                                             new Gzip_Decompression));
      pipe->process_msg(in());
      out = T(pipe->read_all_as_string(Pipe::LAST_MESSAGE));
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


// identifier (a.k.a. sha1 signature) calculation

void
calculate_ident(data const & dat,
                id & ident)
{
  try
    {
      static cached_botan_pipe p(new Pipe(new Hash_Filter("SHA-160")));
      p->process_msg(dat());
      ident = id(p->read_all_as_string(Pipe::LAST_MESSAGE));
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
  id tmp;
  calculate_ident(dat.inner(), tmp);
  ident = file_id(tmp);
}

void
calculate_ident(manifest_data const & dat,
                manifest_id & ident)
{
  id tmp;
  calculate_ident(dat.inner(), tmp);
  ident = manifest_id(tmp);
}

void
calculate_ident(revision_data const & dat,
                revision_id & ident)
{
  id tmp;
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
  id output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  UNIT_TEST_CHECK(output() == decode_hexenc(ident));
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
