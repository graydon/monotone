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
#ifndef BOOST_SOCKET_CONNECTOR_SOCKET_HPP
#define BOOST_SOCKET_CONNECTOR_SOCKET_HPP 1

#include "boost/socket/socket_base.hpp"
#include "boost/socket/data_socket.hpp"
#include "boost/socket/socket_option.hpp"
#include "boost/socket/socket_set.hpp"

#ifdef __CYGWIN__
#include "sys/select.h"
#endif

namespace boost
{
  namespace socket
  {
    template <typename SocketBase=socket_base<> >
    struct connector
    {
      typedef SocketBase socket_base_t;
      typedef typename socket_base_t::socket_t socket_t;
      typedef typename socket_base_t::error_policy error_policy;
      typedef data_socket<socket_base_t> data_connection_t;

      template <typename Protocol, typename Addr>
      socket_errno connect(
        data_connection_t& data_socket,
        const Protocol& protocol,
        const Addr& address)
      {
        socket_base_t socket;
        socket_errno err=socket.open(protocol);
        if (err!=Success)
          return err;

        err=socket.connect(address);
        if (err!=Success)
          return err;

        data_socket.reset(socket.release());
        return Success;
      }

      template <typename Protocol, typename Addr>
      socket_errno connect(
        data_connection_t& data_socket,
        const Protocol& protocol,
        const Addr& address,
        const time_t& timeout)
      {
        socket_base_t socket_to_connect;
        socket_errno err=socket_to_connect.open(protocol);
        if (err!=Success)
          return err;

        option::non_blocking non_block(true);
        socket_errno ioctl_err=socket_to_connect.ioctl(non_block);
        err=socket_to_connect.connect(address);
        if (err==WouldBlock)
        {
          socket_set fdset;
          ::timeval tval;
          tval.tv_sec=timeout.ticks()/boost::posix_time::seconds(1).ticks();
          tval.tv_usec=time_t(0,0,0,timeout.fractional_seconds()).ticks()
            /boost::posix_time::millisec(1).ticks();

          fdset.insert(socket_to_connect.socket());
          socket_errno sel = ::select(fdset.width(), 0, fdset.fdset(), 0,
                                      &tval);
          // this needs reworking !!!
          if (sel==-1)
            throw "unexpected select problem";
          else if (sel==0)
            return -1;
        }
        data_socket.reset(socket_to_connect.release());
      }

    };

  }// namespace
}// namespace

#endif
