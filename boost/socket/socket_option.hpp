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
#ifndef BOOST_SOCKET_SOCKET_OPTION_HPP
#define BOOST_SOCKET_SOCKET_OPTION_HPP 1

#include "boost/config.hpp"
#include "boost/socket/config.hpp"

namespace boost
{
  namespace socket
  {
    namespace option
    {

      class non_blocking
      {
      public:
        non_blocking(bool d) : m_data(d ? 1 : 0) {}
        static int optname();
        void* data() { return &m_data; }
        void const*  data() const { return &m_data; }
        BOOST_STATIC_CONSTANT(bool, can_get = true);
        BOOST_STATIC_CONSTANT(bool, can_set = true);
      private:
        unsigned long m_data;
      };

      class linger
      {
      public:
        linger(time_t t) : m_data(t) {}
        static int level();
        static int optname();
        void* data();
        void const* data() const;
        std::size_t size() const;
        BOOST_STATIC_CONSTANT(bool, can_get = true);
        BOOST_STATIC_CONSTANT(bool, can_set = true);
      private:
        time_t m_data;
      };

    }// namespace
  }// namespace
}// namespace

#endif
