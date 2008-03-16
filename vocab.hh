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

#include <boost/shared_ptr.hpp>

// the purpose of this file is to wrap things which are otherwise strings
// in a bit of typesafety, set up enumerations and tuple-types, and
// generally describe the "vocabulary" (nouns anyways) that modules in this
// program use.

// For some reason, shared_ptr copy is about a hundred times faster
// than string refcopy on my system (g++ 4). This only happens because
// we tell Boost not to worry about threads... but I don't recognize any
// thread stuff in the string headers.
namespace
{
  std::string empty;
}

class immutable_string
{
  boost::shared_ptr<std::string> _rep;

public:
  immutable_string()
  {}
  immutable_string(std::string const & s)
    : _rep(new std::string(s))
  {}

  std::string const & get() const
  {
    if (_rep)
      return *_rep;
    else
      return empty;
  }
};




#include "vocab_macros.hh"
#define ENCODING(enc) hh_ENCODING(enc)
#define ENCODING_NOVERIFY(enc) hh_ENCODING_NOVERIFY(enc)
#define DECORATE(dec) hh_DECORATE(dec)
#define ATOMIC(ty) hh_ATOMIC(ty)
#define ATOMIC_HOOKED(ty,hook) hh_ATOMIC_HOOKED(ty,hook)
#define ATOMIC_NOVERIFY(ty) hh_ATOMIC_NOVERIFY(ty)

#ifdef HAVE_EXTERN_TEMPLATE
#define EXTERN extern
#else
#define EXTERN /* */
#endif

#include "vocab_terms.hh"

#undef ATOMIC
#undef ATOMIC_HOOKED
#undef ATOMIC_NOVERIFY
#undef DECORATE
#undef ENCODING
#undef ENCODING_NOVERIFY

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

// diff type; this is here and not diff_patch.hh, because options_list.hh
// needs to refer to it
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
