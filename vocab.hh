#ifndef __VOCAB_HH__
#define __VOCAB_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "config.h"

#include <utility>
#include <string>
#include <iosfwd>

// the purpose of this file is to wrap things which are otherwise strings
// in a bit of typesafety, set up enumerations and tuple-types, and
// generally describe the "vocabulary" (nouns anyways) that modules in this
// program use.

// this template must be specialized for each type you want to dump.
// there are a few stock dumpers in appropriate places.
template <typename T>
void dump(T const &, std::string &)
{
  // the compiler will evaluate this somewhat odd construct (and issue an
  // error) if and only if this base template is instantiated.  we do not
  // use BOOST_STATIC_ASSERT mainly to avoid dragging it in everywhere;
  // also we get better diagnostics this way (the error tells you what is
  // wrong, not just that there's an assertion failure).
  enum dummy { d = (sizeof(struct dump_must_be_specialized_for_this_type)
                    == sizeof(T)) };
}

#include "vocab_macros.hh"
#define ENCODING(enc) hh_ENCODING(enc)
#define DECORATE(dec) hh_DECORATE(dec)
#define ATOMIC(ty) hh_ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) hh_ATOMIC_NOVERIFY(ty)

inline bool is_xdigit(char x)
{
  return ((x >= '0' && x <= '9')
          || (x >= 'a' && x <= 'f'));
}

inline bool is_alpha(char x)
{
  return ((x >= 'a' && x <= 'z')
          || (x >= 'A' && x <= 'Z'));
}

inline bool is_alnum(char x)
{
  return ((x >= '0' && x <= '9')
          || (x >= 'a' && x <= 'z')
          || (x >= 'A' && x <= 'Z'));
}

inline bool is_space(char x)
{
  return (x == ' ')
    || (x == '\n')
    || (x == '\t')
    || (x == '\r')
    || (x == '\v')
    || (x == '\f');
}


#ifdef HAVE_EXTERN_TEMPLATE
#define EXTERN extern
#else
#define EXTERN /* */
#endif

#include "vocab_terms.hh"

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
typedef   roster< data >     roster_data;
typedef manifest< data >   manifest_data;
typedef     file< data >       file_data;

typedef   roster< delta >    roster_delta;
typedef manifest< delta >  manifest_delta;
typedef     file< delta >      file_delta;

typedef std::pair<var_domain, var_name> var_key;


struct keypair
{
  base64<rsa_pub_key> pub;
  base64<rsa_priv_key> priv;
  keypair()
  {}
  keypair(base64<rsa_pub_key> const & a,
          base64<rsa_priv_key> const & b)
   : pub(a), priv(b)
  {}
};

// fs::path is our "generic" safe path type, pointing potentially anywhere
// in the filesystem. if you want to *define* or work with any of these you
// need to include boost/filesystem/path.hpp.

//namespace boost { namespace filesystem { class path; } }
//namespace fs = boost::filesystem;

// diff type
enum diff_type
{
  unified_diff,
  context_diff,
  external_diff
};

// do these belong here?
inline bool
null_id(hexenc<id> const & i)
{
  return i().empty();
}

inline bool
null_id(file_id const & i)
{
  return i.inner()().empty();
}

inline bool
null_id(manifest_id const & i)
{
  return i.inner()().empty();
}

inline bool
null_id(revision_id const & i)
{
  return i.inner()().empty();
}


hexenc<id>
fake_id();

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __VOCAB_HH__
