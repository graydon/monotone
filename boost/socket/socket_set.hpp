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
#ifndef SOCKET_SET_HPP
#define SOCKET_SET_HPP 1

#include "boost/socket/config.hpp"
#include "boost/iterator_adaptors.hpp"

#ifdef USES_WINSOCK2
#include "Winsock2.h"
#endif

namespace boost
{
  namespace socket
  {

#ifdef USES_WINSOCK2
    class socket_set_iterator_policies : public default_iterator_policies
    {
    public:
      socket_set_iterator_policies(fd_set& base)
        : set_(base) { }

      // The Iter template argument is necessary for compatibility with a MWCW
      // bug workaround
      template <class IteratorAdaptor>
      void increment(IteratorAdaptor& x)
      {
        do {
          ++x.base();
        } while (x.base()!=set_.fd_array+set_.fd_count
                 && !FD_ISSET(*x.base(),&set_));
      }

    private:
      fd_set& set_;
    };

    //! Set of socket (fd_set)
    /**
     */
    class socket_set
    {
    public:
      struct iter_policy{};

      typedef boost::iterator_adaptor<socket_t*,
                                      socket_set_iterator_policies,
                                      socket_t>
        iterator;

      socket_set()
      {
        clear();
      }

      socket_set(const socket_set& set)
          : set_(set.set_)
      {
      }

      void swap(socket_set& set)
      {
        std::swap(set_,set.set_);
      }

      void operator =(const socket_set& set)
      {
        set_ = set.set_;
      }

      void clear()
      {
        FD_ZERO(&set_);
      }

      void erase(socket_t socket)
      {
        FD_CLR(socket, &set_) ;
      }

      void insert(socket_t socket)
      {
        FD_SET(socket, &set_);
      }

      int width() const
      {
        return FD_SETSIZE;
      }

      bool is_set(socket_t socket) const
      {
          return FD_ISSET(socket, &set_) ? true : false;
      }

      fd_set* fdset()
      {
        return &set_;
      }

      iterator begin()
      {
        return iterator(set_.fd_array, set_);
      }

      iterator end()
      {
        return iterator(set_.fd_array+set_.fd_count, set_);
      }

    private:
      fd_set set_;
    };


#else
    class socket_set_iterator_policies : public default_iterator_policies
    {
    public:
      socket_set_iterator_policies(fd_set& base)
        : set_(base)
      { }

      template <class Base>
      void initialize(Base& base)
      {
        while (base!=FD_SETSIZE && !FD_ISSET(base,&set_))
          ++base;
      }

      template <class IteratorAdaptor>
      void increment(IteratorAdaptor& x)
      {
        do {
          ++x.base();
        } while (x.base()!=FD_SETSIZE && !FD_ISSET(x.base(),&set_));
      }


      template <class IteratorAdaptor>
      typename IteratorAdaptor::reference
      dereference(const IteratorAdaptor& x) const
      {
        return const_cast<socket_t&>(x.base());
      }

    private:
      fd_set& set_;
    };


    class socket_set
    {
    public:
      struct iter_policy{};

      typedef boost::iterator_adaptor<socket_t,
                                      socket_set_iterator_policies,
                                      socket_t,
                                      socket_t&,
                                      socket_t,
                                      std::forward_iterator_tag,
                                      int>
        iterator;

      socket_set()
      {
        clear();
      }

      socket_set(const socket_set& set)
          : set_(set.set_)
      {
      }

      void swap(socket_set& set)
      {
        std::swap(set_,set.set_);
      }

      void operator =(const socket_set& set)
      {
        set_ = set.set_;
      }

      void clear()
      {
        FD_ZERO(&set_);
      }

      void erase(socket_t socket)
      {
        FD_CLR(socket, &set_) ;
      }

      void insert(socket_t socket)
      {
        FD_SET(socket, &set_);
      }

      int width() const
      {
        return FD_SETSIZE;
      }

      bool is_set(socket_t socket) const
      {
        return FD_ISSET(socket, &set_);
      }

      fd_set* fdset()
      {
        return &set_;
      }

      iterator begin()
      {
        return iterator(0, set_);
      }

      iterator end()
      {
        return iterator(FD_SETSIZE, set_);
      }

    private:
      fd_set set_;
    };
#endif
  }// namespace
}// namespace

#endif
