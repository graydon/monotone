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
#ifndef BOOST_SOCKET_IP4_PROTOCOL_HPP
#define BOOST_SOCKET_IP4_PROTOCOL_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/socket_option.hpp"

namespace boost
{
  namespace socket
  {
    namespace ip4
    {

      //! IP options
      class socket_option_ip_options
      {};

#if 0
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

      class tcp_protocol
      {
      public:
        static protocol_type_t type();
        static protocol_t protocol();
        static family_t family();
      };

      class udp_protocol
      {
      public:
        static protocol_type_t type();
        static protocol_t protocol();
        static family_t family();
      };


    }// namespace
  }// namespace
}// namespace

#endif
