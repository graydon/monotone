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
#ifndef BOOST_SOCKET_IP4_ADDRESS_HPP
#define BOOST_SOCKET_IP4_ADDRESS_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/impl/address_storage.hpp"

#include <string>

namespace boost
{
  namespace socket
  {
    class any_address;

    namespace ip4
    {

      //! Address class for IP4
      class address
      {
      public:
        address();
        address(const any_address&);
        address(char const* ip, port_t port);
        
        family_t family() const;
        port_t port() const;
        void port(port_t port);
        void ip(char const * ip_string);
        std::string ip() const;

        std::string to_string() const;
        std::pair<void*,size_t> representation();
        std::pair<const void*,size_t> representation() const;

        bool operator < (const address& addr) const;
        bool operator == (const address& addr) const;
        bool operator != (const address& addr) const;

      private:
        impl::address_storage m_address;
      };


    }// namespace
  }// namespace
}// namespace






#endif
