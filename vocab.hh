#ifndef __VOCAB_HH__
#define __VOCAB_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

using namespace std;

// the purpose of this file is to wrap things which are otherwise strings
// in a bit of typesafety, set up enumerations and tuple-types, and
// generally describe the "vocabulary" (nouns anyways) that modules in this
// program use.

#define ENCODING(enc)                                  \
                                                       \
template<typename INNER>                               \
class enc;                                             \
                                                       \
template <typename INNER>                              \
ostream & operator<<(ostream &, enc<INNER> const &);   \
                                                       \
template<typename INNER>                               \
class enc {                                            \
  INNER i;                                             \
public:                                                \
  enc() {}                                             \
  enc(string const & s);                               \
  enc(INNER const & inner);                            \
  enc(enc<INNER> const & other);                       \
  string const & operator()() const                    \
    { return i(); }                                    \
  bool operator<(enc<INNER> const & x) const           \
    { return i() < x(); }                              \
  enc<INNER> const &                                   \
  operator=(enc<INNER> const & other);                 \
  bool operator==(enc<INNER> const & x) const          \
    { return i() == x(); }                             \
  friend ostream & operator<< <>(ostream &,            \
                                 enc<INNER> const &);  \
};


#define DECORATE(dec)                                  \
                                                       \
template<typename INNER>                               \
class dec;                                             \
                                                       \
template <typename INNER>                              \
ostream & operator<<(ostream &, dec<INNER> const &);   \
                                                       \
template<typename INNER>                               \
class dec {                                            \
  INNER i;                                             \
public:                                                \
  dec() {}                                             \
  dec(INNER const & inner);                            \
  dec(dec<INNER> const & other);                       \
  bool operator<(dec<INNER> const & x) const           \
    { return i < x.i; }                                \
  INNER const & inner() const                          \
    { return i; }                                      \
  dec<INNER> const &                                   \
  operator=(dec<INNER> const & other);                 \
  bool operator==(dec<INNER> const & x) const          \
    { return i == x.i; }                               \
  friend ostream & operator<< <>(ostream &,            \
                                 dec<INNER> const &);  \
};


#define ATOMIC(ty)                                     \
class ty {                                             \
  string s;                                            \
public:                                                \
  ty() : s("") {}                                      \
  ty(string const & str);                              \
  ty(ty const & other);                                \
  string const & operator()() const                    \
    { return s; }                                      \
  bool operator<(ty const & other) const               \
    { return s < other(); }                            \
  ty const & operator=(ty const & other);              \
  bool operator==(ty const & other) const              \
    { return s == other(); }                           \
  friend ostream & operator<<(ostream &, ty const &);  \
};                                                     \
ostream & operator<<(ostream &, ty const &);


#define EXTERN extern

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef DECORATE
#undef ENCODING

// most of the time you want to use these typedefs and forget
// about the stuff in vocab_terms.hh

typedef manifest< hexenc<id> >  manifest_id;
typedef     file< hexenc<id> >      file_id;

typedef manifest< base64< gzip<data> > >   manifest_data;
typedef     file< base64< gzip<data> > >       file_data;

typedef manifest< base64< gzip<delta> > >  manifest_delta;
typedef     file< base64< gzip<delta> > >      file_delta;


// fs::path is our "generic" safe path type, pointing potentially anywhere
// in the filesystem. if you want to *define* or work with any of these you
// need to include boost/filesystem/path.hpp.

namespace boost { namespace filesystem { struct path; } }
namespace fs = boost::filesystem;

// kludge: certs are derived types. what else can we do?
#ifndef __CERT_HH__
#include "cert.hh"
extern template class manifest<cert>;
extern template class file<cert>;
#endif

#endif // __VOCAB_HH__
