// Copyright (C) 2002 Hugo Duncan

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Hugo Duncan makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma once
#endif

/// include guard
#ifndef BOOST_SOCKET_IMPL_ADDRESS_STORAGE_HPP
#define BOOST_SOCKET_IMPL_ADDRESS_STORAGE_HPP 1

#include "boost/config.hpp"
#include <cstddef>
#include <cstring>

namespace boost
{
  namespace socket
  {
    namespace impl
    {
      //! reserve storage for socket addresses
      class address_storage
      {
      public:
        address_storage();
        address_storage(void const* const addr, std::size_t l);
        address_storage(const address_storage& address);
        void clear();
        void operator = (const address_storage& address);
        void set(void const* const addr, std::size_t l);
        void const* get() const;
        void* get();
      private:
        BOOST_STATIC_CONSTANT(std::size_t, len=128);
        unsigned char storage[len];
      };

    }// namespace
  }// namespace
}// namespace

#endif
