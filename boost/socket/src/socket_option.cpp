// Copyright (C) 2002 Hugo Duncan

// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Hugo Duncan makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma warning (disable: 4786 4305)
  // 4786 truncated debug symbolic name
  // 4305 truncation from const double to float
#endif

#if defined(__BORLANDC__)
#pragma hdrstop
#endif

#include "boost/socket/socket_option.hpp"
#include "boost/socket/config.hpp"

#if defined(USES_WINSOCK2)
#include <Winsock2.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#endif

#ifdef _MSC_VER
#pragma warning (push, 4)
#pragma warning (disable: 4786 4305)
#endif

namespace boost
{
  namespace socket
  {
    namespace option
    {
      int non_blocking::optname()
      {
#if defined(FIONBIO)
        return FIONBIO;
#elif defined( O_NONBLOCK)
        return O_NONBLOCK;
#else
        BOOST_STATIC_ASSERT(false);
#endif
      }

      int linger::level()
      {
        return SOL_SOCKET;
      }
      int linger::optname()
      {
        return SO_LINGER;
      }

      void* linger::data()
      {
        static ::linger l;
        l.l_onoff=(u_short)(m_data!=time_t(0,0,0,0) ? 1 : 0);
        l.l_linger=(u_short)
          (m_data.ticks()/boost::posix_time::millisec(1).ticks());
        return &l;
      }
      void const* linger::data() const
      {
        static ::linger l;
        l.l_onoff=(u_short)(m_data!=time_t(0,0,0,0) ? 1 : 0);
        l.l_linger=(u_short)
          (m_data.ticks()/boost::posix_time::millisec(1).ticks());
        return &l;
      }
      std::size_t linger::size() const
      {
        return sizeof(::linger);
      }

#if 0
    //! non-blocking
#ifdef O_NONBLOCK
    typedef socket_option<option_level_ioctl, O_NONBLOCK, false, true, u_long>
    socket_option_non_blocking;
#elif defined(FNONBIO)
    typedef socket_option<option_level_ioctl, FNONBIO, false, true, u_long>
    socket_option_non_blocking;
#endif

#ifdef _WIN32
     typedef socket_option<option_level_ioctl, FIONBIO, false, true, u_long>
     socket_option_non_blocking;
#endif
#ifdef FIONREAD //JKG -- FIONREAD isn't available on Linux at least...
    //! amount of data that can be read
    typedef socket_option<option_level_ioctl, FIONREAD, false, true, u_long>
    socket_option_data_pending;
#endif
    //! all OOB data read
    typedef socket_option<option_level_ioctl, SIOCATMARK, false, true, u_long>
      socket_option_at_mark;

    typedef socket_option<SOL_SOCKET, SO_ACCEPTCONN, true, false, bool>
      socket_option_listening;
    typedef socket_option<SOL_SOCKET, SO_BROADCAST, true, true, bool>
      socket_option_broadcast;

#if defined(USES_WINSOCK2) && defined(_MSC_VER) && _MSC_VER > 1200
    typedef socket_option<SOL_SOCKET, SO_CONDITIONAL_ACCEPT, true, true, bool>
      socket_option_conditional_accept;
#endif
    typedef socket_option<SOL_SOCKET, SO_DEBUG, true, true, bool>
      socket_option_debug;


    struct linger : public ::linger
    {
      linger()
      {
        l_onoff=false;
      }

      linger(u_short time_sec)
      {
        l_onoff=true;
        l_linger=time_sec;
      }
    };


//JKG removed temporary for Linux with gcc3.2 on Mandrake 9
//Linux certainly has SO_LINGER but not DONTLINGER -- maybe we
//will need to define it?
#ifdef SO_DONTLINGER
    typedef socket_option<SOL_SOCKET, SO_DONTLINGER, true, true, bool>
    socket_option_dontlinger;
#endif
    typedef socket_option<SOL_SOCKET, SO_DONTROUTE, true, true, bool>
      socket_option_dontroute;
    typedef socket_option<SOL_SOCKET, SO_ERROR, true, false, bool>
      socket_option_error;
#if defined(USES_WINSOCK2)
    typedef socket_option<SOL_SOCKET, SO_GROUP_ID, true, false, GROUP>
      socket_option_group_id;
    typedef socket_option<SOL_SOCKET, SO_GROUP_PRIORITY, true, true, int>
      socket_option_group_priority;
#endif
    typedef socket_option<SOL_SOCKET, SO_KEEPALIVE, true, true, bool>
      socket_option_keepalive;
    typedef socket_option<SOL_SOCKET, SO_LINGER, true, true, linger>
      socket_option_linger;
#if defined(USES_WINSOCK2)
    typedef socket_option<SOL_SOCKET, SO_MAX_MSG_SIZE, true,false,unsigned int>
      socket_option_max_msg_size;
#endif
    typedef socket_option<SOL_SOCKET, SO_OOBINLINE, true, true, bool>
      socket_option_oobinline;
#if defined(USES_WINSOCK2)
    typedef socket_option<SOL_SOCKET, SO_PROTOCOL_INFO, true, false,
                          WSAPROTOCOL_INFO>
    socket_option_protocol_info;
#endif
    typedef socket_option<SOL_SOCKET, SO_RCVBUF, true, true, int>
      socket_option_rcvbuf;
    typedef socket_option<SOL_SOCKET, SO_REUSEADDR, true, true, bool>
      socket_option_reuseaddr;
    typedef socket_option<SOL_SOCKET, SO_SNDBUF, true, true, int>
      socket_option_sndbuf;
    typedef socket_option<SOL_SOCKET, SO_TYPE, true, false, int>
      socket_option_type;
    // PVD_CONFIG

#endif

    }// namespace
  }// namespace
}// namespace

#ifdef _MSC_VER
#pragma warning (pop)
#endif
