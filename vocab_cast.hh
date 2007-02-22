#ifndef __VOCAB_CAST_HH
#define __VOCAB_CAST_HH

#include <algorithm>

// You probably won't use this yourself, but it's needed by...
template<typename From, typename To>
To typecast_vocab(From const & from)
{ return To(from()); }

// There are a few places where we want to typecast an entire
// container full of vocab types.
template<typename From, typename To>
void typecast_vocab_container(From const & from, To & to)
{
  std::transform(from.begin(), from.end(), std::inserter(to, to.end()),
		 &typecast_vocab<typename From::value_type,
		 typename To::value_type>);
}

// You won't use this directly either.
template<typename From, typename To>
To add_decoration(From const & from)
{
  return To(from);
}

// There are also some places that want to decorate a container full
// of vocab types.
template<typename From, typename To>
void add_decoration_to_container(From const & from, To & to)
{
  std::transform(from.begin(), from.end(), std::inserter(to, to.end()),
		 &add_decoration<typename From::value_type,
		 typename To::value_type>);
}

template<typename From, typename To>
void vocabify_container(From const & from, To & to)
{
  add_decoration_to_container(from, to);
}

#endif
