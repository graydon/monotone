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
#ifndef BOOST_SOCKET_IMPL_DEFAULT_SOCKET_IMPL_HPP
#define BOOST_SOCKET_IMPL_DEFAULT_SOCKET_IMPL_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/socket_errors.hpp"
#include "boost/config.hpp"


namespace boost
{
  namespace socket
  {
    namespace impl
    {

      //! Provides an implementation of SocketImplConcept
      /** This provides a platform neutral set of calls,
          with no attempt at error handling.
          It also does not have tempolated methods to allow
          implementation hiding.
       */
      class default_socket_impl
      {
      public:
#if defined(USES_WINSOCK2)
        typedef unsigned long socket_t;
        BOOST_STATIC_CONSTANT(int, socket_error = -1);
        BOOST_STATIC_CONSTANT(socket_t, invalid_socket = -1);

#elif defined(__CYGWIN__)
        typedef int socket_t;
        BOOST_STATIC_CONSTANT(int, socket_error = -1);
        BOOST_STATIC_CONSTANT(socket_t, invalid_socket = -1);
#else
        typedef int socket_t;
        BOOST_STATIC_CONSTANT(int, socket_error = -1);
        BOOST_STATIC_CONSTANT(socket_t, invalid_socket = -1);
#endif

      public:

        default_socket_impl();
//         default_socket_impl(const default_socket_impl&);
        explicit default_socket_impl(socket_t socket);
        ~default_socket_impl();

        //! release the socket handle
        socket_t release();

        //! reset the socket handle
        void reset(socket_t socket = socket_t());

        socket_errno ioctl(int option, void* data);

        socket_errno getsockopt(
          int level, int optname, void *data, size_t& optlen);

        socket_errno setsockopt(
          int level,int optname, void const* data, size_t optlen);

        socket_errno open(
          family_t family, protocol_type_t type, protocol_t protocol);

        socket_errno connect(const std::pair<void const*,size_t>& address);

        socket_errno bind(const std::pair<void const*,size_t>& address);

        socket_errno listen(int backlog);

        //! accept a connection
        socket_errno accept(
          default_socket_impl&, std::pair<void *,size_t>& address);

        //! receive data
        int recv(void* data, size_t len, int flags=0);

        //! send data
        /** Returns the number of bytes sent */
        int send(const void* data, size_t len, int flags=0);

        //! shut the socket down
        socket_errno shutdown(Direction how=Both);

        //! close the socket
        socket_errno close();

        //! check for a valid socket
        bool is_open() const;

        //! obtain OS socket
        socket_t socket();

        //! obtain OS socket
        const socket_t socket() const;

        //! compare a socket
        bool operator<(const default_socket_impl& socket) const;

        //! compare a socket
        bool operator==(const default_socket_impl& socket) const;

        //! compare a socket
        bool operator!=(const default_socket_impl& socket) const;

        //! translate platform specific to general errors
        static socket_errno translate_error(int return_value);

      private:
        socket_t m_socket;
      };


    }// namespace
  }// namespace
}// namespace


#endif
