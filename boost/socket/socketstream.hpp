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
#ifndef SOCKETTREAM_HPP
#define SOCKETTREAM_HPP 1

#include "boost/socket/data_socket.hpp"
#include "boost/socket/socket_streambuf.hpp"
#include "boost/io/streambuf_wrapping.hpp"
#include "boost/ref.hpp"

namespace boost
{
  namespace socket
  {

    template <typename Element,
              typename Traits = std::char_traits<Element>,
              typename SocketType=data_socket< socket_base<> >
              >
    class basic_socket_stream
      : public boost::io::basic_wrapping_iostream<
      basic_socket_streambuf<Element, Traits> >
    {
    public:
      typedef SocketType data_socket_t;
      typedef typename data_socket_t::socket_t socket_t;
      typedef typename data_socket_t::error_policy error_policy;
      typedef Element  char_type;
      typedef Traits  traits_type;

      explicit basic_socket_stream(SocketType& socket)
          : boost::io::basic_wrapping_iostream<
        basic_socket_streambuf<Element, Traits> >(boost::ref(socket))
      {}
    };  // boost::io::basic_array_stream

  }// namespace
}// namespace

#endif
