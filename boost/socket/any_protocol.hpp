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
#ifndef BOOST_SOCKET_ANY_PROTOCOL_HPP
#define BOOST_SOCKET_ANY_PROTOCOL_HPP 1

#include "boost/socket/config.hpp"

namespace boost
{
  namespace socket
  {
    //! any protocol class
    class any_protocol
    {
    public:
      any_protocol(family_t family, protocol_type_t type, protocol_t protocol)
          : m_family(family), m_type(type),  m_protocol(protocol)
      {}

      protocol_type_t type() const
      {
        return m_type;
      }

      protocol_t protocol() const
      {
        return m_protocol;
      }

      family_t family() const
      {
        return m_family;
      }

    private:
      family_t m_family;
      protocol_type_t m_type;
      protocol_t m_protocol;
    };
  }
}

#endif
