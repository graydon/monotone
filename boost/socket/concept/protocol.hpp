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
#ifndef BOOST_SOCKET_CONCEPT_PROTOCOL_HPP
#define BOOST_SOCKET_CONCEPT_PROTOCOL_HPP 1

namespace boost
{
  namespace socket
  {
    //! concept check for protocols
    template <class Protocol>
    struct ProtocolConcept
    {
      void constraints()
      {
        int p=protocol.protocol();
        int t=protocol.type();
        short s=protocol.family();
        boost::ignore_unused_variable_warning(p);
        boost::ignore_unused_variable_warning(t);
        boost::ignore_unused_variable_warning(s);
      }
      Protocol protocol;
    };
  }
}

#endif
