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

#include "boost/socket/any_address.hpp"
#include "boost/lexical_cast.hpp"
#include <cstring>

#ifdef USES_WINSOCK2
#include <Winsock2.h>
#else
#include <sys/socket.h>
#endif

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif

namespace boost
{
  namespace socket
  {

    any_address::any_address(const void* addr, std::size_t size)
        : m_address(addr, size), m_size(size)
    { }

    family_t any_address::family() const
    {
      return ((::sockaddr const*)m_address.get())->sa_family;
    }

    std::string any_address::to_string() const
    {
      std::string s("Any address: family : ");
      s+=boost::lexical_cast<std::string>(
        ((sockaddr const*)m_address.get())->sa_family);
      return s;
    }

    std::pair<void*,size_t> any_address::representation()
    {
      return std::make_pair(m_address.get(), m_size);
    }

    std::pair<const void*,size_t> any_address::representation() const
    {
      return std::make_pair(m_address.get(), m_size);
    }


    bool any_address::operator < (const any_address& addr) const
    {
      const int cmp=std::memcmp(m_address.get(),
                                addr.m_address.get(),
                                m_size);
      return cmp<0;
    }

    bool any_address::operator == (const any_address& addr) const
    {
      const int cmp=std::memcmp(m_address.get(),
                                addr.m_address.get(),
                                m_size);
      return cmp==0;
    }

    bool any_address::operator != (const any_address& addr) const
    {
      const int cmp=std::memcmp(m_address.get(),
                                addr.m_address.get(),
                                m_size);
      return cmp!=0;
    }

  }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif
