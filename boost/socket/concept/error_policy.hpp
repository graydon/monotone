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
#ifndef BOOST_SOCKET_CONCEPT_ERROR_POLICY_HPP
#define BOOST_SOCKET_CONCEPT_ERROR_POLICY_HPP 1

#include "boost/socket/socket_errors.hpp"

namespace boost
{
  namespace socket
  {

    //! concept check for addresses
    template <typename ErrorPolicy>
    struct ErrorPolicyConcept
    {
    public:
      void constraints()
      {
        // family_type family() const
        error_policy.handle_error(function::close,0);
        const_constraints();
      }

      void const_constraints() const
      {
      }

    private:
      ErrorPolicy error_policy;
    };


  }// namespace
}// namespace

#endif
