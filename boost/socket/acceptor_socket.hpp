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
#ifndef BOOST_SOCKET_ACCEPTOR_SOCKET_HPP
#define BOOST_SOCKET_ACCEPTOR_SOCKET_HPP 1

#include "boost/socket/socket_base.hpp"
#include "boost/socket/data_socket.hpp"

namespace boost
{
  namespace socket
  {
    //! restricted interfaces using socket_base, but with reduced functionality
    /** The aim here is to provide a restricted programming interface, built
        on the platform neutral socket_base. */

    //! acceptor
    /** interface for a socket that can accept connections. */
    template <typename SocketBase=socket_base<> >
    class acceptor_socket
    {
    public:
      typedef SocketBase socket_base_t;
      typedef typename socket_base_t::socket_t socket_t;
      typedef typename socket_base_t::error_policy error_policy;
      typedef data_socket<socket_base_t> data_connection_t;

      template <typename SocketOption>
      socket_errno ioctl(SocketOption& option)
      {
        return m_base.ioctl(option);
      }

      template <typename SocketOption>
      socket_errno getsockopt(SocketOption& option)
      {
        return m_base.getsockopt(option);
      }

      template <typename SocketOption>
      socket_errno setsockopt(const SocketOption& option)
      {
        return m_base.setsockopt(option);
      }

      //! Open a socket for passive connection acceptance
      /** Blocking version */
      template <typename Protocol, class Addr>
      socket_errno open(const Protocol& protocol,
                        const Addr& address)
      {
        const socket_errno open_error=m_base.open(protocol);
        if (open_error!=Success)
          return open_error;

        const socket_errno bind_error=m_base.bind(address);
        if (bind_error!=Success)
          return bind_error;

        return Success;
      }

      //! Open a socket for passive connection acceptance
      /** Non-blocking version */
      template <typename Protocol, class Addr>
      socket_errno open(const Protocol& protocol,
                        const Addr& address,
                        std::size_t backlog)
      {
        const socket_errno open_error=m_base.open(protocol);
        if (open_error!=Success)
          return open_error;

        const socket_errno bind_error=m_base.bind(address);
        if (bind_error!=Success)
          return bind_error;

        // set the socket to non-blocking so that listen will succeed
        option::non_blocking non_block(true);
        m_base.ioctl(non_block);

        const socket_errno listen_error=m_base.listen(backlog);
        if (listen_error==Success || listen_error==WouldBlock)
          return Success;
        return listen_error;
      }

      //! accept a connection
      template <class Addr>
      socket_errno accept(data_connection_t& data_socket,
                          Addr& address)
      {
        socket_base_t base_socket;
        socket_errno ret=m_base.accept(base_socket, address);
        data_socket.reset(base_socket.release());
        return ret;
      }

      //! accept a connection
      /** Blocking version */
      template <class Addr>
      socket_errno accept(data_connection_t& data_socket,
                          Addr& address,
                          std::size_t backlog)
      {
        const socket_errno listen_error=m_base.listen(backlog);
        if (listen_error!=Success && listen_error!=WouldBlock)
          return listen_error;

        socket_base_t base_socket;
        socket_errno ret=m_base.accept(base_socket,address);
        data_socket.reset(base_socket.release());
        return ret;
      }


      //! close a connection
      socket_errno close()
      {
        return m_base.close();
      }

      //! obtain OS socket
      socket_t socket()
      {
        return m_base.socket();
      }

      //! obtain socket_base
      socket_base_t& base()
      {
        return m_base;
      }
    private:
      socket_base_t m_base;
    };

  }// namespace
}// namespace

#endif
