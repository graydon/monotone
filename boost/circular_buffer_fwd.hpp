// Forward declaration of the circular buffer and its adaptor.

// Copyright (c) 2003
// Jan Gaspar, Whitestein Technologies

// Permission to use or copy this software for any purpose is hereby granted 
// without fee, provided the above notices are retained on all copies.
// Permission to modify the code and to distribute modified code is granted,
// provided the above notices are retained, and a notice that the code was
// modified is included with the above copyright notice.

// This material is provided "as is", with absolutely no warranty expressed
// or implied. Any use is at your own risk.

#if !defined(BOOST_CIRCULAR_BUFFER_FWD_HPP)
#define BOOST_CIRCULAR_BUFFER_FWD_HPP

#include <boost/config.hpp>
#include <vector>

namespace boost {

// Definition of the default allocator (needed because of non-standard
// default allocator in some STL implementations e.g. SGI STL).
#if !defined(BOOST_NO_DEPENDENT_TYPES_IN_TEMPLATE_VALUE_PARAMETERS)
    #define BOOST_CB_DEFAULT_ALLOCATOR(T) typename std::vector<T>::allocator_type
#else
    #define BOOST_CB_DEFAULT_ALLOCATOR(T) std::vector<T>::allocator_type
#endif

template <class T, class Alloc = BOOST_CB_DEFAULT_ALLOCATOR(T)>
class circular_buffer;

template <class T, class Alloc = BOOST_CB_DEFAULT_ALLOCATOR(T)>
class circular_buffer_space_optimized;

#undef BOOST_CB_DEFAULT_ALLOCATOR

} // namespace boost

#endif // #if !defined(BOOST_CIRCULAR_BUFFER_FWD_HPP)
