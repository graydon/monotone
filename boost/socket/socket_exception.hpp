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
#ifndef SOCKET_EXCEPTION_HPP
#define SOCKET_EXCEPTION_HPP 1

#include <exception>
#include <string>

namespace boost
{
  namespace socket
  {

    enum error_type { system_error };

    // At the moment, this mirrors the approch taken
    // by the filesystem library.
    // this should follow whatever idiom is decided upon
    // for exceptions in boost in general
    class socket_exception : public std::exception
    {
    public:
      explicit socket_exception( std::string const& msg )
          : m_msg(msg), m_err(0)
      {}

      explicit socket_exception( std::string const& msg, int value)
          : m_msg(msg), m_err(value)
      {}

      ~socket_exception() throw () {}

      virtual const char *what( ) const throw()
      {
        return "boost::socket::socket_exception";
      }

      int error() const { return m_err; }
      const std::string& message() const { return m_msg; }

    private:
      std::string m_msg;
      int         m_err;
    };


  }// namespace
}// namespace

#endif
