// Copyright (C) 2002 Michel André (michel@andre.net)
// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appear in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  Michel André makes no representations
// about the suitability of this software for any purpose.
// It is provided "as is" without express or implied warranty.

#include "boost/socket/impl/socket_init.hpp"
#include "boost/socket/config.hpp"

#ifdef USES_WINSOCK2
#include <Winsock2.h>
#include <Ws2tcpip.h>
#endif

namespace boost
{
    namespace socket
    {
        namespace detail
        {
            int socket_initializer::m_niftycounter = 0;
            socket_initializer::socket_initializer()
            {
                if (m_niftycounter++ == 0)
                {
#ifdef USES_WINSOCK2
                    WSADATA wsaData;
                    ::WSAStartup(MAKEWORD(2,0), &wsaData);
#endif
                }
            }

            socket_initializer::~socket_initializer()
            {
#ifdef USES_WINSOCK2
                if (--m_niftycounter == 0)
                    ::WSACleanup();
#endif
            }

        } // namespace detail
    } // namespace socket
} // namespace boost
