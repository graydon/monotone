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
#ifndef BOOST_SOCKET_SOCKET_BASE_HPP
#define BOOST_SOCKET_SOCKET_BASE_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/impl/default_socket_impl.hpp"
#include "boost/socket/impl/default_error_policy.hpp"
#include "boost/socket/socket_errors.hpp"
#include "boost/socket/concept/protocol.hpp"
#include "boost/socket/concept/address.hpp"

#include "boost/concept_check.hpp"
#include "boost/assert.hpp"

namespace boost
{
  namespace socket
  {

    //! platform independent, low level interface
    /** Implementation will depend on platform */
    template <typename ErrorPolicy=default_error_policy,
              typename SocketImpl=impl::default_socket_impl>
    class socket_base
    {
    public:
      typedef ErrorPolicy error_policy;
      typedef SocketImpl socket_impl;
      typedef typename socket_impl::socket_t socket_t;
      typedef socket_base<error_policy,socket_impl> self_t;

      socket_base()
          : m_socket_impl()
      {}

      explicit socket_base(socket_t socket)
          : m_socket_impl(socket)
      {}

      ~socket_base()
      {}

      void reset(socket_t socket = socket_t())
      {
        m_socket_impl.reset(socket);
      }

      //! releases ownership socket and leaves socket_base invalid.
      socket_t release()
      {
        return m_socket_impl.release();
      }

      template <typename SocketOption>
      socket_errno ioctl(SocketOption& option)
      {
        socket_errno ret = m_socket_impl.ioctl(option.optname(), option.data());
        if (ret!=Success)
          return m_error_policy.handle_error(function::ioctl,ret);
        return Success;
      }

      template <typename SocketOption>
      socket_errno getsockopt(SocketOption& option)
      {
        BOOST_STATIC_ASSERT(option.can_get);
        int len=option.size();
        socket_errno ret = ::getsockopt(socket_,
                                        option.level, option.option, &option.value,
                                        &len);
        if (ret!=Success)
          return m_error_policy.handle_error(function::getsockopt,ret);
        return Success;
      }

      template <typename SocketOption>
      socket_errno setsockopt(const SocketOption& option)
      {
        BOOST_STATIC_ASSERT(option.can_set);
        socket_errno ret = m_socket_impl.setsockopt(option.level(),
                                                    option.optname(),
                                                    option.data(),
                                                    option.size());
        if (ret!=Success)
          return m_error_policy.handle_error(function::setsockopt,ret);
        return Success;
      }

      // create a socket, Address family, type {SOCK_STREAM, SOCK_DGRAM },
      // protocol is af specific
      template <typename Protocol>
      socket_errno open(const Protocol& protocol)
      {
        boost::function_requires< ProtocolConcept<Protocol> >();

        // SOCKET socket(int af,int type,int protocol);
        socket_errno ret = m_socket_impl.open(protocol.family(),
                                              protocol.type(),
                                              protocol.protocol());
        if (ret!=Success)
          return m_error_policy.handle_error(function::open,ret);
        return Success;
      }

      template <class Addr>
      socket_errno connect(const Addr& address)
      {
        boost::function_requires< AddressConcept<Addr> >();
        socket_errno ret= m_socket_impl.connect(address.representation());
        if (ret!=Success)
          return m_error_policy.handle_error(function::connect,ret);
        return Success;
      }

      template <class Addr>
      socket_errno bind(const Addr& address)
      {
        socket_errno ret=m_socket_impl.bind(address.representation());
        if (ret!=Success)
          return m_error_policy.handle_error(function::bind,ret);
        return Success;
      }

      socket_errno listen(int backlog)
      {
        socket_errno ret=m_socket_impl.listen(0x7fffffff);
        if (ret!=Success)
          return m_error_policy.handle_error(function::listen,ret);
        return Success;
      }

      //! accept a connection
      template <class Addr>
      socket_errno accept(self_t& socket, Addr& address)
      {
        boost::function_requires< AddressConcept<Addr> >();
        std::pair<void*,size_t> rep=address.representation();
        socket_errno ret = m_socket_impl.accept(socket.m_socket_impl, rep);
        if (ret!=Success)
        {
          return m_error_policy.handle_error(function::accept,ret);
        }
        return Success;
      }

      //! receive data
      int recv(void* data, size_t len)
      {
        int ret=m_socket_impl.recv(data, len);
        if (ret<0)
          return m_error_policy.handle_error(function::recv,
                                             static_cast<socket_errno>(ret));
        return ret;
      }

      //! send data
      /** Returns the number of bytes sent */
      int send(const void* data, size_t len)
      {
        int ret=m_socket_impl.send(data, len);
        if (ret<0)
          return m_error_policy.handle_error(function::send,
                                             static_cast<socket_errno>(ret));
        return ret;
      }

      //! shut the socket down
      socket_errno shutdown(Direction how=Both)
      {
        socket_errno ret = m_socket_impl.shutdown(how);
        if (ret != Success && ret != socket_is_not_connected)
          return m_error_policy.handle_error(function::shutdown,ret);
        return Success;
      }

      //! close the socket
      socket_errno close()
      {
        BOOST_ASSERT(m_socket_impl.is_open()
                     && "trying to close handle that is not open");
        socket_errno ret=m_socket_impl.close();
        if (ret!=Success)
          m_error_policy.handle_error(function::close, ret);
        return Success;
      }

      //! check for a valid socket
      bool is_open() const
      {
        return m_socket_impl.is_open();
      }

      //! obtain OS socket
      socket_t socket()
      {
        return m_socket_impl.socket();
      }

      //! obtain OS socket
      const socket_t socket() const
      {
        return m_socket_impl.socket();
      }

      //! compare a socket
      bool operator<(const socket_base& socket) const
      {
        return m_socket_impl<socket.m_socket_impl;
      }

      //! compare a socket
      bool operator==(const socket_base& socket) const
      {
        return m_socket_impl==socket.m_socket_impl;
      }

      //! compare a socket
      bool operator!=(const socket_base& socket) const
      {
        return m_socket_impl!=socket.m_socket_impl;
      }

    protected: // available for async_socket base for now
        
        socket_impl m_socket_impl;
        error_policy m_error_policy;
        
    private:
        
      socket_base(const socket_impl& s)
          : m_socket_impl(s)
      {}

     };


  }
}

#endif
