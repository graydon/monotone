// Copyright (C) 2002 Michel André (michel@andre.net)

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Michel André makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma once
#endif

/// include guard
#ifndef BOOST_SOCKET_IMPL_SOCKET_INIT_HPP
#define BOOST_SOCKET_IMPL_SOCKET_INIT_HPP 1

namespace boost
{
  namespace socket
  {
    namespace detail
    {
      struct socket_initializer
      {
        static int m_niftycounter;
        socket_initializer();
        ~socket_initializer();
      };

      namespace
      {
        socket_initializer socket_initializer_instance;
      }
    }
  }
}

#endif
