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

#include "boost/socket/address_info.hpp"
#include "boost/socket/any_protocol.hpp"
#include "boost/socket/any_address.hpp"

#ifdef USES_WINSOCK2
#include <Winsock2.h>
#include <Ws2tcpip.h>
#else

#endif

#ifdef __sun
#define INADDR_NONE -1
#endif

#if defined(USES_WINSOCK2)
#define HAVE_GETADDRINFO
#else

#ifdef __CYGWIN__
#include <cygwin/in.h>
#else
#include <netinet/in.h>
#endif

#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include "boost/lexical_cast.hpp"

#endif

#include <iostream>
#include <string.h>

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif


namespace boost
{
  namespace socket
  {

#if !defined(HAVE_GETADDRINFO)
    namespace detail
    {
      // this work around should be made thread safe(r)
      // by copy result structures

      struct addrinfo {
        addrinfo()
            : ai_flags(0),
              ai_family(0),
              ai_socktype(0),
              ai_protocol(0),
              ai_addrlen(0),
              ai_canonname(0),
              ai_addr(0),
              ai_next(0)
        {}

        ~addrinfo()
        {
          delete ai_canonname;
          delete ai_addr;
        }

        int     ai_flags;
        int     ai_family;
        int     ai_socktype;
        int     ai_protocol;
        size_t  ai_addrlen;
        char   *ai_canonname;
        struct sockaddr  *ai_addr;
        struct addrinfo  *ai_next;
      };


      void freeaddrinfo(addrinfo* info)
      {
        delete info;
      }

      int getaddrinfo(
        const char* name,
        const char* service,
        addrinfo* hint,
        addrinfo** result )
      {
        *result=0;

        if (name && *name)
        {
          if (::isalpha(*name))
          {
            //assume host name
            ::hostent *hp = ::gethostbyname(name);
            if (hp == 0)
              return -1;

            addrinfo* res=*result;
            addrinfo* prev_res=0;

            for (char** hp_addr=hp->h_addr_list; *hp_addr; ++hp_addr)
            {
              res=new addrinfo;
              if (*result)
                prev_res->ai_next=res;
              else
                *result=res;


              res->ai_canonname=0;
              if (hp->h_name)
                res->ai_canonname=::strdup(hp->h_name);
              else if (*hp->h_aliases)
                res->ai_canonname=::strdup(*hp->h_aliases);
              else if (hint->ai_flags & 2)
              {
                hostent *hp =
                  gethostbyaddr(*hp_addr,sizeof(unsigned long),AF_INET );
                if (hp->h_name)
                  res->ai_canonname=::strdup(hp->h_name);
              }

              if (hp->h_addrtype==AF_INET)
              {
                res->ai_family=hp->h_addrtype;
                sockaddr_in* addr=new sockaddr_in;
                res->ai_addr=(sockaddr*)addr;
                memset(addr,0,sizeof(sockaddr_in));
                res->ai_addrlen=sizeof(sockaddr_in);
                addr->sin_family=hp->h_addrtype;
                std::memcpy(&addr->sin_addr, *hp_addr, hp->h_length);
              }
              else
              {
                res->ai_family=hp->h_addrtype;
                sockaddr_in6* addr=new sockaddr_in6;
                res->ai_addr=(sockaddr*)addr;
                memset(addr,0,sizeof(sockaddr_in6));
                res->ai_addrlen=sizeof(sockaddr_in6);
                addr->sin6_family=hp->h_addrtype;
                std::memcpy(&addr->sin6_addr, *hp_addr, hp->h_length);
              }
              prev_res=res;
            }

          }
          else if (::isdigit(*name))
          {
            unsigned long i = ::inet_addr(name);
            if (i == INADDR_NONE)
              return -1;

            addrinfo* res=new detail::addrinfo;
            *result=res;
            res->ai_family=AF_INET;
            sockaddr_in* addr=new sockaddr_in;
            res->ai_addr=(sockaddr*)addr;
            memset(addr,0,sizeof(sockaddr_in));
            res->ai_addrlen=sizeof(sockaddr_in);
            addr->sin_addr.s_addr=i;
            addr->sin_family=AF_INET;
          }
          else
          {
            return -1;
          }
        }

        if (service && *service)
        {
          ::servent *hp = 0;
          if (::isalpha(*service))
            hp=::getservbyname(service,0);
          else
            try{
              hp=::getservbyport(boost::lexical_cast<int>(service),0);
            }
            catch (boost::bad_lexical_cast &)
            {
              return -1;
            }

          if (hp == 0)
            return -1;

          addrinfo* addr=*result;
          while (addr)
          {
            if (strncmp(hp->s_proto,"udp",3)==0)
              addr->ai_protocol=IPPROTO_UDP;
            else if (strncmp(hp->s_proto,"tcp",3)==0)
              addr->ai_protocol=IPPROTO_TCP;
            else
              return -1;

            if (addr->ai_protocol==IPPROTO_UDP)
              addr->ai_socktype=SOCK_DGRAM;
            else
              addr->ai_socktype=SOCK_STREAM;
            if (addr->ai_family==AF_INET)
              std::memcpy(&((sockaddr_in*)addr->ai_addr)->sin_port,
                          &hp->s_port,
                          sizeof(hp->s_port));
            else
              std::memcpy(&((sockaddr_in6*)addr->ai_addr)->sin6_port,
                          &hp->s_port,
                          sizeof(hp->s_port));

            addr=addr->ai_next;
          }
        }

        return 0;
      }
    }
#define ADDRINFO_NS detail

    inline detail::addrinfo*&
    cast_addrinfo(boost::socket::addrinfo*& addr)
    {
      return (detail::addrinfo*&)(addr);
    }

    inline detail::addrinfo const*
    cast_addrinfo(boost::socket::addrinfo const* addr)
    {
      return (detail::addrinfo const*)(addr);
    }

#else
#define ADDRINFO_NS

    inline ::addrinfo*&
    cast_addrinfo(boost::socket::addrinfo*& addr)
    {
      return (::addrinfo*&)(addr);
    }

    inline ::addrinfo const*
    cast_addrinfo(const boost::socket::addrinfo* addr)
    {
      return (::addrinfo const*)(addr);
    }
#endif


    any_protocol address_info::protocol() const
    {
      return any_protocol(cast_addrinfo(m_addrinfo)->ai_family,
                          cast_addrinfo(m_addrinfo)->ai_socktype,
                          cast_addrinfo(m_addrinfo)->ai_protocol);
    }

    any_address address_info::address() const
    {
      return any_address(cast_addrinfo(m_addrinfo)->ai_addr,
                         cast_addrinfo(m_addrinfo)->ai_addrlen);
    }

    const int address_info::flags() const
    {
      return cast_addrinfo(m_addrinfo)->ai_flags;
    }

    std::string address_info::hostname() const
    {
      if (!cast_addrinfo(m_addrinfo)->ai_canonname)
        return "";
      return cast_addrinfo(m_addrinfo)->ai_canonname;
    }

    addrinfo const* address_info::next() const
    {
      return (addrinfo const*)cast_addrinfo(m_addrinfo)->ai_next;
    }

    address_info_list::address_info_list(const char* name,
                                         const char* service,
                                         const int   flags,
                                         const int   family,
                                         const int   socktype,
                                         const int   protocol)
        : m_addrinfo(0)
    {
      get_addrinfo(name,service,flags,family,socktype,protocol);
    }

    address_info_list::~address_info_list()
    {
      ADDRINFO_NS::freeaddrinfo(cast_addrinfo(m_addrinfo));
    }

    void address_info_list::get_addrinfo(
      const char* name,
      const char* service,
      const int   flags,
      const int   family,
      const int   socktype,
      const int   protocol )
    {
      int result;
      ADDRINFO_NS::addrinfo hint;

      memset(&hint, 0, sizeof(hint));
      hint.ai_flags = flags;
      hint.ai_family = family;
      hint.ai_socktype = socktype;
      hint.ai_protocol = protocol;

      result = ADDRINFO_NS::getaddrinfo(
        name, service, &hint, &cast_addrinfo(m_addrinfo));
    }

  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
