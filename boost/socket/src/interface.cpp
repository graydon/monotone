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

#include "boost/socket/config.hpp"
#include "boost/socket/interface.hpp"
#include "boost/socket/any_address.hpp"

#ifdef USES_WINSOCK2
#include <Winsock2.h>
#include <Ws2tcpip.h>
#else
#include <net/if.h>
#ifdef __CYGWIN__
#include <cygwin/in.h>
#else
#include <netinet/in.h>
#endif
#include <sys/ioctl.h>
#endif

#include <iostream>
#include <vector>
#include <string.h>

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif


namespace boost
{
  namespace socket
  {

#ifdef USES_WINSOCK2
    inline INTERFACE_INFO*&
    cast_interfaceinfo(boost::socket::interfaceinfo*& iface)
    {
      return (INTERFACE_INFO*&)(iface);
    }

    inline INTERFACE_INFO const*
    cast_interfaceinfo(const boost::socket::interfaceinfo* iface)
    {
      return (INTERFACE_INFO const*)(iface);
    }


    any_address interface_info::address() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiAddress,
        sizeof(sockaddr_in));
    }

    any_address interface_info::netmask() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiNetmask,
        sizeof(sockaddr_in));
    }

    any_address interface_info::broadcast() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiBroadcastAddress,
        sizeof(sockaddr_in));
    }

    bool interface_info::is_up() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_UP;
    }

    bool interface_info::is_point_to_point() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_POINTTOPOINT;
    }

    bool interface_info::is_loopback() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_LOOPBACK;
    }

    bool interface_info::can_broadcast() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_BROADCAST;
    }

    bool interface_info::can_multicast() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_MULTICAST;
    }

    interface_info::interface_info()
        : m_interfaceinfo(0)
    {}

    void interface_info::set(interfaceinfo const* info)
    {
      m_interfaceinfo=info;
    }


    namespace
    {
      const std::size_t buffer_len=20*sizeof(INTERFACE_INFO);
    }

    struct interface_info_list_impl
    {
      interface_info_list_impl()
          : m_buffer(new char[buffer_len])
      {}

      std::auto_ptr<char> m_buffer;
      std::size_t m_num_interfaces;
    };

    interface_info_list::interface_info_list()
        : m_impl(new interface_info_list_impl)
    {
      SOCKET socket=::WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
      unsigned long nBytesReturned;
      if (::WSAIoctl(socket,
                     SIO_GET_INTERFACE_LIST, 0, 0, &*m_impl->m_buffer,
                     buffer_len,
                     &nBytesReturned, 0, 0) == SOCKET_ERROR)
      {
        std::cerr << "Failed calling WSAIoctl: error "
                  << WSAGetLastError() << std::endl;
      }
      m_impl->m_num_interfaces=nBytesReturned / sizeof(INTERFACE_INFO);

      ::closesocket(socket);
    }

    interface_info_list::~interface_info_list()
    {
    }

    interface_info_list::iterator interface_info_list::begin() const
    {
      return iterator((interfaceinfo*)m_impl->m_buffer.get());
    }

    interface_info_list::iterator interface_info_list::end() const
    {
      return iterator((interfaceinfo*)
                      (((INTERFACE_INFO*)m_impl->m_buffer.get())
                       +m_impl->m_num_interfaces));
    }

    interfaceinfo const* interface_info_list::next(interfaceinfo const* arg)
    {
      return (interfaceinfo*) (((INTERFACE_INFO*)arg)+1);
    }
#else

    struct INTERFACE_INFO
    {
      unsigned long   iiFlags;             //< flags
      impl::address_storage iiAddress;           //< Interface address
      impl::address_storage iiBroadcastAddress;  //< Broadcast address
      impl::address_storage iiNetmask;           //< Netmask
    };


    inline INTERFACE_INFO*&
    cast_interfaceinfo(boost::socket::interfaceinfo*& iface)
    {
      return (INTERFACE_INFO*&)(iface);
    }

    inline INTERFACE_INFO const*
    cast_interfaceinfo(const boost::socket::interfaceinfo* iface)
    {
      return (INTERFACE_INFO const*)(iface);
    }

    any_address interface_info::address() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiAddress,
        sizeof(::sockaddr));
    }

    any_address interface_info::netmask() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiNetmask,
        sizeof(::sockaddr));
    }

    any_address interface_info::broadcast() const
    {
      return any_address(
        &cast_interfaceinfo(m_interfaceinfo)->iiBroadcastAddress,
        sizeof(::sockaddr));
    }

    bool interface_info::is_up() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_UP;
    }

    bool interface_info::is_point_to_point() const
    {
      return false;//cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_POINTTOPOINT;
    }

    bool interface_info::is_loopback() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_LOOPBACK;
    }

    bool interface_info::can_broadcast() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_BROADCAST;
    }

    bool interface_info::can_multicast() const
    {
      return cast_interfaceinfo(m_interfaceinfo)->iiFlags && IFF_MULTICAST;
    }

    interface_info::interface_info()
        : m_interfaceinfo(0)
    {}

    void interface_info::set(interfaceinfo const* info)
    {
      m_interfaceinfo=info;
    }


    namespace
    {
      const std::size_t buffer_len=20*sizeof(::ifreq);
    }


    struct interface_info_list_impl
    {
      interface_info_list_impl()
          : m_buffer(new char[buffer_len])
      {}

      std::auto_ptr<char> m_buffer;
      std::size_t m_num_interfaces;
      std::vector<INTERFACE_INFO> m_iface;
    };

    interface_info_list::interface_info_list()
        : m_impl(new interface_info_list_impl)
    {
      int socket=::socket(AF_INET, SOCK_DGRAM, 0);

      ::ifconf  conf;
      conf.ifc_len=buffer_len;
      conf.ifc_buf=m_impl->m_buffer.get();

      if (::ioctl(socket, SIOCGIFCONF, &conf) == -1)
      {
        std::cerr << "Failed calling ioctl: error "
                  << errno << std::endl;
      }
      m_impl->m_num_interfaces=conf.ifc_len / sizeof(::ifreq);
      m_impl->m_iface.resize(m_impl->m_num_interfaces);

      std::cerr << m_impl->m_num_interfaces << " ifaces found" << std::endl;

      ::ifreq* ifr=conf.ifc_req;
      std::vector<INTERFACE_INFO>::iterator iface=m_impl->m_iface.begin();

      for (std::size_t i=0; i<m_impl->m_num_interfaces; ++i, ++ifr, ++iface)
      {
        // Flags
        if (ioctl(socket, SIOCGIFFLAGS, (char *) ifr) < 0)
        {
          iface->iiFlags=0;
          continue;
        }
        iface->iiFlags=ifr->ifr_flags;

        // Interface address
        if (ioctl(socket, SIOCGIFADDR, (char *) ifr) < 0)
          continue;
        iface->iiAddress.set(&ifr->ifr_addr,sizeof(::sockaddr));

        // Broadcast address
        if (ioctl(socket, SIOCGIFBRDADDR, (char *) ifr) < 0)
          continue;
        iface->iiBroadcastAddress.set(&ifr->ifr_netmask,sizeof(::sockaddr));

        // Netmask address
        if (ioctl(socket, SIOCGIFNETMASK, (char *) ifr) < 0)
          continue;
        iface->iiNetmask.set(&ifr->ifr_broadaddr,sizeof(::sockaddr));

//         // Hardware address
//         if (ioctl(socket, SIOCGIFHWADDR, (char *) ifr) < 0)
//           continue;
//         iface->iiFlags=ifr->ifc_flags;

//         // Metric
//         if (ioctl(socket, SIOCGIFMETRIC, (char *) ifr) < 0)
//           continue;
//         iface->iiFlags=ifr->ifc_flags;

//         // MTU
//         if (ioctl(socket, SIOCGIFMTU, (char *) ifr) < 0)
//           continue;
//         iface->iiFlags=ifr->ifc_flags;
      }

      ::close(socket);
    }

    interface_info_list::~interface_info_list()
    {
    }

    interface_info_list::iterator interface_info_list::begin() const
    {
      return iterator((interfaceinfo*)&m_impl->m_iface.front());
    }

    interface_info_list::iterator interface_info_list::end() const
    {
      return iterator((interfaceinfo*)
                      (((INTERFACE_INFO*)&m_impl->m_iface.front())
                       +m_impl->m_num_interfaces));
    }

    interfaceinfo const* interface_info_list::next(interfaceinfo const* arg)
    {
      return (interfaceinfo*) (((INTERFACE_INFO*)arg)+1);
    }

#endif

  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
