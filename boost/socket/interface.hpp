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
#ifndef BOOST_SOCKET_INTERFACE_HPP
#define BOOST_SOCKET_INTERFACE_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/socket_errors.hpp"
#include "boost/iterator_adaptors.hpp"

#include "boost/utility.hpp"

namespace boost
{
  namespace socket
  {
    class interfaceinfo;
    class any_address;
    template <typename ErrorPolicy, typename SocketImpl> class socket_base;

    //! Return interface information
    class interface_info
    {
    public:
      interface_info(interfaceinfo const* info)
          : m_interfaceinfo(info)
      {}

      any_address address() const;
      any_address netmask() const;
      any_address broadcast() const;

      bool is_up() const;
      bool is_point_to_point() const;
      bool is_loopback() const;
      bool can_broadcast() const;
      bool can_multicast() const;

    private:
      friend class interface_info_iterator_policies;
      interface_info();
      void set(interfaceinfo const* info);
    private:
      interfaceinfo const* m_interfaceinfo;
    };

    class interface_info_list;

    class interface_info_iterator_policies : public default_iterator_policies
    {
    public:
      template <class IteratorAdaptor>
      void increment(IteratorAdaptor& x)
      {
        x.base()=interface_info_list::next(x.base());
      }

      template <class IteratorAdaptor>
      const interface_info& dereference(IteratorAdaptor& x) const
      {
        static interface_info info;
        info.set(x.base());
        return info;
      }

    };

    struct interface_info_list_impl;

    class interface_info_list : boost::noncopyable
    {
    public:
      typedef boost::iterator_adaptor<const interfaceinfo*,
                                      interface_info_iterator_policies,
                                      interface_info,
                                      const interface_info&,
                                      const interface_info*,
                                      std::forward_iterator_tag,
                                      int>
        iterator;

      interface_info_list();
      ~interface_info_list();

      iterator begin() const;
      iterator end() const;

    private:
    private:
      friend class interface_info_iterator_policies;
      static interfaceinfo const* next(interfaceinfo const*);

      std::auto_ptr<interface_info_list_impl> m_impl;
    };



  }// namespace
}// namespace

#endif
