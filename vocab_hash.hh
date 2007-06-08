#ifndef __VOCAB_HASH_HH__
#define __VOCAB_HASH_HH__

#include "vocab.hh"
#include "hash_map.hh"

#define ENCODING(enc)				       \
  namespace hashmap {				       \
    template<typename INNER>			       \
      struct hash<enc<INNER> >			       \
      {						       \
	size_t operator()(enc<INNER> const & t) const  \
	{					       \
	  return hash<std::string>()(t());	       \
	}					       \
      };					       \
  }

#define DECORATE(dec)				       \
  namespace hashmap {				       \
    template<typename INNER>			       \
      struct hash<dec<INNER> >			       \
      {						       \
	size_t operator()(dec<INNER> const & t) const  \
	{					       \
	  return hash<INNER>()(t.inner());	       \
	}					       \
      };					       \
  }

#define ATOMIC(ty)				       \
  namespace hashmap {				       \
    template<>					       \
      struct hash<ty>				       \
      {						       \
	size_t operator()(ty const & t) const	       \
	{					       \
	  return hash<std::string>()(t());	       \
	}					       \
      };					       \
  }

#define ATOMIC_NOVERIFY(ty) ATOMIC(ty)

#include "vocab_terms.hh"

#endif
