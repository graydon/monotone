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

#include "boost/socket/ip4/protocol.hpp"

#if defined(USES_WINSOCK2)
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

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif


namespace boost
{
  namespace socket
  {
    namespace ip4
    {

#if 0
      //! IP options
      typedef socket_option<IPPROTO_IP, IP_OPTIONS, true, true, char *>
      socket_option_ip_options;

      typedef socket_option<IPPROTO_IP, IP_TOS, true, false, int>
      socket_option_ip_tos;
      typedef socket_option<IPPROTO_IP, IP_TTL, true, true, int>
      socket_option_ip_ttl;
#ifdef USES_WINSOCK2
      typedef socket_option<IPPROTO_IP, IP_HDRINCL, true, false, BOOL>
      socket_option_ip_hdrincl;
#endif

#ifdef USES_WINSOCK2
      typedef socket_option<IPPROTO_IP, IP_MULTICAST_IF, true, true, IN_ADDR*>
      socket_option_ip_multicast_if;
#elif !defined(__CYGWIN__)
      typedef socket_option<IPPROTO_IP, IP_MULTICAST_IF, true, true, ip_mreqn*>
      socket_option_ip_multicast_if;
#endif

      typedef socket_option<IPPROTO_IP, IP_MULTICAST_TTL, true, true, int>
      socket_option_ip_multicast_ttl;
      typedef socket_option<IPPROTO_IP, IP_MULTICAST_LOOP, true, true, bool_t>
      socket_option_ip_multicast_loop;
//     typedef socket_option<IPPROTO_IP, IP_ADD_MEMBERSHIP, true, true, IP_MREQ*>
//       socket_option_ip_add_membership;
//     typedef socket_option<IPPROTO_IP, IP_DROP_MEMBERSHIP, true, true, IP_MREQ*>
//       socket_option_ip_drop_membership;

      //! IP UDP options
#ifdef USES_WINSOCK2
      typedef socket_option<IPPROTO_UDP, UDP_NOCHECKSUM, true, true, bool_t>
      socket_option_udp_nochecksum;
#endif

      //! IP TCP options
#ifdef USES_WINSOCK2
      typedef socket_option<IPPROTO_TCP, TCP_EXPEDITED_1122, true, true,bool_t>
      socket_option_tcp_expedited_1122;
#endif

#endif


      int tcp_protocol::type()
      {
        return SOCK_STREAM;
      }

      int tcp_protocol::protocol()
      {
        return IPPROTO_TCP;
      }

      family_t tcp_protocol::family()
      {
        return AF_INET;
      }

      protocol_type_t udp_protocol::type()
      {
        return SOCK_DGRAM;
      }

      protocol_t udp_protocol::protocol()
      {
        return IPPROTO_UDP;
      }

      family_t udp_protocol::family()
      {
        return PF_INET;
      }

    }// namespace
  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
