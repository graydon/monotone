#ifndef __VOCAB_HH__
#define __VOCAB_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

#include <utility>
#include <string>
#include <iosfwd>

// the purpose of this file is to wrap things which are otherwise strings
// in a bit of typesafety, set up enumerations and tuple-types, and
// generally describe the "vocabulary" (nouns anyways) that modules in this
// program use.

template <typename T>
void dump(T const &, std::string &);

#include "vocab_macros.hh"
#define ENCODING(enc) hh_ENCODING(enc)
#define DECORATE(dec) hh_DECORATE(dec)
#define ATOMIC(ty) hh_ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) hh_ATOMIC_NOVERIFY(ty)

inline bool is_xdigit(char x) 
{ 
  return ((x >= '0' && x <= '9')
	  || (x >= 'a' && x <= 'f')
	  || (x >= 'A' && x <= 'F'));
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
typedef   roster< hexenc<id> >    roster_id;
typedef manifest< hexenc<id> >  manifest_id;
typedef     file< hexenc<id> >      file_id;
typedef      key< hexenc<id> >       key_id;
typedef    epoch< hexenc<id> >     epoch_id;
typedef    epoch< hexenc<data> > epoch_data;

typedef revision< data >   revision_data;
typedef   roster< data >     roster_data;
typedef manifest< data >   manifest_data;
typedef     file< data >       file_data;

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

namespace boost { namespace filesystem { struct path; } }
namespace fs = boost::filesystem;

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


#endif // __VOCAB_HH__
