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
#ifndef BOOST_SOCKET_CONCEPT_ADDRESS_HPP
#define BOOST_SOCKET_CONCEPT_ADDRESS_HPP 1

#include <utility>
#include <string>

namespace boost
{
  namespace socket
  {
    /* NB: Address familiy is strongly linked to protocol family, but
       is independent.
    */

    //! concept check for addresses
    template <typename Address>
    struct AddressConcept
    {
      void constraints()
      {
        // family_type family() const
        address.family();

        // should return something that can be passed to C API
        std::pair<void*,size_t> rep=address.representation();
        address=address;

        boost::ignore_unused_variable_warning(rep);

        const_constraints();
      }

      void const_constraints() const
      {
        std::pair<void const*,size_t> rep=address.representation();
        // convert to string
        std::string s=address.to_string();

        // make sure we can use this as a key
        bool x = address < address;
        bool y = address==address;
        bool z = address!=address;
        boost::ignore_unused_variable_warning(rep);
        boost::ignore_unused_variable_warning(x);
        boost::ignore_unused_variable_warning(y);
        boost::ignore_unused_variable_warning(z);
      }

    private:
      Address address;
    };




  }
}

#endif
