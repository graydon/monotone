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
#pragma option -w-8061 -w-8060
#endif

#include "boost/socket/ip4/address.hpp"
#include "boost/socket/any_address.hpp"
#include "boost/socket/impl/socket_init.hpp"
#include "boost/socket/socket_exception.hpp"

#include "boost/lexical_cast.hpp"

//! implementation
#ifdef USES_WINSOCK2

#include <winsock2.h>
#include <Ws2tcpip.h>

#else

#ifdef __CYGWIN__
#include "sys/socket.h"
#include "cygwin/in_systm.h"
#include "cygwin/in.h"
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>

#endif

#include <utility>
#include <cstring>
#include "boost/config.hpp"
#ifdef BOOST_NO_STDC_NAMESPACE
namespace std { using ::memset; using ::memcpy;  using ::memcmp; }
#endif


#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif


namespace
{
  const int address_size = sizeof(sockaddr_in);

  inline sockaddr_in*
  sockaddr_ptr(boost::socket::impl::address_storage& addr)
  {
    return reinterpret_cast<sockaddr_in*>(addr.get());
  }

  inline sockaddr_in const*
  sockaddr_ptr(boost::socket::impl::address_storage const& addr)
  {
    return reinterpret_cast<const sockaddr_in*>(addr.get());
  }
}

namespace boost
{
  namespace socket
  {

    namespace ip4
    {

      address::address()
      {
        sockaddr_ptr(m_address)->sin_family = AF_INET;
      }

      address::address(const any_address& addr)
      {
        std::pair<const void*,size_t> rep=addr.representation();
        m_address.set(rep.first, rep.second);
      }

      address::address(char const* ip, port_t port)
      {
        sockaddr_ptr(m_address)->sin_family = AF_INET;
        this->port(port);
        this->ip(ip);
      }

      
      family_t address::family() const
      {
        return sockaddr_ptr(m_address)->sin_family;
      }

      port_t address::port() const
      {
        return ntohs(sockaddr_ptr(m_address)->sin_port);
      }

      //! set the (host ordered) port number
      void address::port(port_t port)
      {
        sockaddr_ptr(m_address)->sin_port = htons(port);
      }

//       void address::hostname(const char* hostname)
//       {
//         hostent *hp = ::gethostbyname(hostname);
//         if (hp == 0)
//           throw "hostname not found";
//         std::memcpy((char*)&(sockaddr_ptr(m_address)->sin_addr),
//                     (char*)hp->h_addr, hp->h_length);
//       }

//       std::string address::hostname() const
//       {
//         hostent *hp =
//           gethostbyaddr((const char*)&sockaddr_ptr(m_address)->sin_addr,
//                         sizeof(unsigned long),
//                         AF_INET );
//         if (hp == 0)
//           throw "hostname not found";
//         return hp->h_name;
//       }

      void address::ip(const char* ip_string)
      {
        unsigned long i = inet_addr(ip_string);
        if (i == INADDR_NONE)
          throw "ip not valid";
#ifdef USES_WINSOCK2
        sockaddr_ptr(m_address)->sin_addr.S_un.S_addr=i;
#else
        sockaddr_ptr(m_address)->sin_addr.s_addr=i;
#endif
      }

      std::string address::ip() const
      {
        const char* ret=inet_ntoa(sockaddr_ptr(m_address)->sin_addr);
        if (!ret)
          return "";
        return ret;
      }

      std::string address::to_string() const
      {
        const char *ret=inet_ntoa(sockaddr_ptr(m_address)->sin_addr);
        if (!ret)
          return "";

        std::string s(ret);
        s+=":";
        try{
          s+=boost::lexical_cast<std::string>(
            ntohs(sockaddr_ptr(m_address)->sin_port));
        }
        catch (boost::bad_lexical_cast&)
        {}

        return s;
      }

      std::pair<const void*,unsigned> address::representation() const
      {
        return std::make_pair(static_cast<const void*>(sockaddr_ptr(m_address)),
                              sizeof(sockaddr_in));
      }

      // should return something that can be passed to other functions
      /** This is to make sure we can passs the "correct" sockaddr structure,
          to eg. accept.
      */
      std::pair<void*,unsigned> address::representation()
      {
        return std::make_pair(static_cast<void*>(sockaddr_ptr(m_address)),
                              sizeof(sockaddr_in));
      }

      bool address::operator < (const address& addr) const
      {
        const int cmp=std::memcmp(m_address.get(),
                                  addr.m_address.get(),
                                  address_size);
        return cmp<0;
      }

      bool address::operator == (const address& addr) const
      {
        const int cmp=std::memcmp(m_address.get(),
                                  addr.m_address.get(),
                                  address_size);
        return cmp==0;
      }

      bool address::operator != (const address& addr) const
      {
        const int cmp=std::memcmp(m_address.get(),
                                  addr.m_address.get(),
                                  address_size);
        return cmp!=0;
      }


    }// namespace
  }// namespace
}// namespace


#ifdef _MSC_VER
#pragma warning (pop)
#endif
