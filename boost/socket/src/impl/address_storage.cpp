// Copyright (C) 2002 Hugo Duncan

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Hugo Duncan makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma warning (disable: 4786 4305)
  // 4786 truncated debug symbolic name
  // 4305 truncation from const double to float
#endif

#if defined(__BORLANDC__)
#pragma hdrstop
#endif

#include "boost/socket/impl/address_storage.hpp"

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif

namespace boost
{
  namespace socket
  {
    namespace impl
    {
      address_storage::address_storage()
      {
        clear();
      }

      address_storage::address_storage(void const* const addr, std::size_t l)
      {
        std::memcpy(storage, addr, l);
      }
      void address_storage::set(void const* const addr, std::size_t l)
      {
        std::memcpy(storage, addr, l);
      }


      address_storage::address_storage(const address_storage& address)
      {
        std::memcpy(storage, address.storage, len);
      }

      void address_storage::clear()
      {
        std::memset(storage, 0, len);
      }

      void address_storage::operator = (const address_storage& address)
      {
        std::memcpy(storage, address.storage, len);
      }

      void const* address_storage::get() const
      {
        return storage;
      }

      void* address_storage::get()
      {
        return storage;
      }

    }// namespace
  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
