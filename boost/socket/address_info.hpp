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
#ifndef BOOST_SOCKET_ADDRESS_INFO_HPP
#define BOOST_SOCKET_ADDRESS_INFO_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/socket_errors.hpp"
#include "boost/iterator_adaptors.hpp"

#include <string>

namespace boost
{
  namespace socket
  {
    class addrinfo;

    class any_address;
    class any_protocol;

    class address_info
    {
    public:
      address_info(addrinfo const* info)
          : m_addrinfo(info)
      {}
      any_protocol protocol() const;
      any_address address() const;
      std::string hostname() const;
      const int flags() const;
    private:
      // help for the iterators
      friend class address_info_iterator_policies;
      addrinfo const* next() const;
      address_info() : m_addrinfo(0) {}
      void set(addrinfo const* addr){ m_addrinfo=addr; }

      addrinfo const* m_addrinfo;
    };


    class address_info_iterator_policies : public default_iterator_policies
    {
    public:
      template <class IteratorAdaptor>
      void increment(IteratorAdaptor& x)
      {
        x.base()=address_info(x.base()).next();
      }

      template <class IteratorAdaptor>
      const address_info& dereference(IteratorAdaptor& x) const
      {
        static address_info info;
        info.set(x.base());
        return info;
      }

    };

    class address_info_list
    {
    public:
      typedef boost::iterator_adaptor<const addrinfo*,
                                      address_info_iterator_policies,
                                      address_info,
                                      const address_info&,
                                      const address_info*,
                                      std::forward_iterator_tag,
                                      int>
        iterator;

      address_info_list(const char* name,
                        const char* service,
                        const int   flags=0,
                        const int   family=0,
                        const int   socktype=0,
                        const int   protocol=0);
      ~address_info_list();

      iterator begin() const
      {
        return iterator(m_addrinfo);
      }

      iterator end() const
      {
        return iterator(0);
      }

      enum hints { passive=1, canonname, numerichost };

    private:
      void get_addrinfo(
        const char* name,
        const char* service,
        const int   flags,
        const int   family,
        const int   socktype,
        const int   protocol);

      addrinfo* m_addrinfo;
    };



  }// namespace
}// namespace

#endif
