#ifndef __VOCAB_HH__
#define __VOCAB_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <utility>
#include <string>
#include <iosfwd>

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
std::ostream & operator<<(std::ostream &,              \
                          enc<INNER> const &);         \
                                                       \
template<typename INNER>                               \
class enc {                                            \
  INNER i;                                             \
public:                                                \
  bool ok;                                             \
  enc() : ok(false) {}                                 \
  enc(std::string const & s);                          \
  enc(INNER const & inner);                            \
  enc(enc<INNER> const & other);                       \
  std::string const & operator()() const               \
    { return i(); }                                    \
  bool operator<(enc<INNER> const & x) const           \
    { return i() < x(); }                              \
  enc<INNER> const &                                   \
  operator=(enc<INNER> const & other);                 \
  bool operator==(enc<INNER> const & x) const          \
    { return i() == x(); }                             \
  friend std::ostream & operator<< <>(std::ostream &,  \
                                 enc<INNER> const &);  \
};


#define DECORATE(dec)                                  \
                                                       \
template<typename INNER>                               \
class dec;                                             \
                                                       \
template <typename INNER>                              \
std::ostream & operator<<(std::ostream &,              \
                          dec<INNER> const &);         \
                                                       \
template<typename INNER>                               \
class dec {                                            \
  INNER i;                                             \
public:                                                \
  bool ok;                                             \
  dec() : ok(false) {}                                 \
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
  friend std::ostream & operator<< <>(std::ostream &,  \
                                 dec<INNER> const &);  \
};


#define ATOMIC(ty)                                     \
class ty {                                             \
  std::string s;                                       \
public:                                                \
  bool ok;                                             \
  ty() : s(""), ok(false) {}                           \
  ty(std::string const & str);                         \
  ty(ty const & other);                                \
  std::string const & operator()() const               \
    { return s; }                                      \
  bool operator<(ty const & other) const               \
    { return s < other(); }                            \
  ty const & operator=(ty const & other);              \
  bool operator==(ty const & other) const              \
    { return s == other(); }                           \
  friend void verify(ty &);                            \
  friend std::ostream & operator<<(std::ostream &,     \
                                   ty const &);        \
};                                                     \
std::ostream & operator<<(std::ostream &, ty const &);

#define ATOMIC_NOVERIFY(ty)                            \
ATOMIC(ty)                                             \
inline void verify(ty &) {}

#define EXTERN extern

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef ATOMIC_NOVERIFY
#undef DECORATE
#undef ENCODING

// most of the time you want to use these typedefs and forget
// about the stuff in vocab_terms.hh

typedef revision< hexenc<id> >  revision_id;
typedef manifest< hexenc<id> >  manifest_id;
typedef     file< hexenc<id> >      file_id;
typedef      key< hexenc<id> >       key_id;
typedef    epoch< hexenc<id> >     epoch_id;
typedef    epoch< hexenc<data> > epoch_data;

typedef revision< data >   revision_data;
typedef manifest< data >   manifest_data;
typedef     file< data >       file_data;

typedef manifest< delta >  manifest_delta;
typedef     file< delta >      file_delta;

typedef std::pair<var_domain, var_name> var_key;

// fs::path is our "generic" safe path type, pointing potentially anywhere
// in the filesystem. if you want to *define* or work with any of these you
// need to include boost/filesystem/path.hpp.

namespace boost { namespace filesystem { struct path; } }
namespace fs = boost::filesystem;

// kludge: certs are derived types. what else can we do?
#ifndef __CERT_HH__
#include "cert.hh"
extern template class revision<cert>;
extern template class manifest<cert>;
#endif

// diff type
enum diff_type
{
  unified_diff,
  context_diff,
  external_diff
};


#endif // __VOCAB_HH__
