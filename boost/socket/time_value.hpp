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
#ifndef BOOST_SOCKET_TIMEVALUE_HPP
#define BOOST_SOCKET_TIMEVALUE_HPP 1

#include "boost/operators.hpp"

namespace boost
{
  namespace socket
  {

    struct timeval
    {
      long tv_sec;
      long tv_usec;
    };


    //! placeholder until we use date_time
    struct time_value
    {
    public:
      time_value(long tv_sec, long tv_usec)
      {
        time_.tv_sec=tv_sec;
        time_.tv_usec=tv_usec;
      }

      void const* timevalue() const
      {
        return &time_;
      }

      unsigned short msec() const
      {
        return time_.tv_sec+time_.tv_usec/1000;
      }

      bool operator<(const struct time_value& t) const
      {
        return time_.tv_sec<t.time_.tv_sec
          || (time_.tv_sec==t.time_.tv_sec && time_.tv_usec<t.time_.tv_usec);
      }

      bool operator==(const struct time_value& t) const
      {
        return time_.tv_sec==t.time_.tv_sec && time_.tv_usec==t.time_.tv_usec;
      }

      time_value& operator+=(const struct time_value& t)
      {
        time_.tv_sec+=t.time_.tv_sec;
        time_.tv_usec+=t.time_.tv_usec;
        return *this;
      }

      time_value& operator-=(const struct time_value& t)
      {
        time_.tv_sec-=t.time_.tv_sec;
        time_.tv_usec-=t.time_.tv_usec;
        return *this;
      }
    private:
      timeval time_;
    };

    template struct boost::addable<time_value>;
    template struct boost::subtractable<time_value>;
    template struct boost::less_than_comparable<time_value>;
    template struct boost::equality_comparable<time_value>;
  }
}

#endif
