/*
 * Copyright (C) 2001-2004 Peter J Jones (pjones@pmade.org)
 * All Rights Reserved
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/** @file
 * This file contains definitions for OS utility functions.
**/

// declaration include
#include "common.h"
#include <sstream>

//####################################################################
Netxx::error_type Netxx::get_last_error (void) 
{
#if defined (WIN32)

    return WSAGetLastError();

#else

    return errno;

#endif
}
//####################################################################
std::string Netxx::str_error(Netxx::error_type errnum) 
{
#if defined (WIN32)

    std::ostringstream s;

    // try FormatMessage first--this will probably fail for anything < Win2k.
    LPTSTR msg;
    DWORD len;
    if ((len = FormatMessage(
                      FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                      0, errnum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      reinterpret_cast<LPTSTR>(&msg), 0,
                      static_cast<va_list *>(0))) != 0) {
        for (LPTSTR p = msg + len - 1; p > msg; --p)
            if (*p == '\r' || *p == '\n')
                *p = '\0';
        s << msg;
        LocalFree(msg);
    } else {
      // if it failed, try using our own table.
      struct {
          DWORD n;
          char const * m;
      } static const error_msgs[]  = {
          { WSAEINTR, "interrupted function call" },
          { WSAEBADF, "invalid socket handle" },
          { WSAEACCES, "access denied" },
          { WSAEFAULT, "invalid address" },
          { WSAEINVAL, "invalid argument" },
          { WSAEMFILE, "too many open files" },
          { WSAEWOULDBLOCK, "resource temporarily unavailable" },
          { WSAEINPROGRESS, "operation in progress" },
          { WSAEALREADY, "operating already in progress" },
          { WSAENOTSOCK, "not a socket" },
          { WSAEDESTADDRREQ, "destination address required" },
          { WSAEMSGSIZE, "message too long" },
          { WSAEPROTOTYPE, "incorrect protocol for socket" },
          { WSAENOPROTOOPT, "invalid protocol option" },
          { WSAEPROTONOSUPPORT, "protocol not supported" },
          { WSAESOCKTNOSUPPORT, "socket type not supported" },
          { WSAEOPNOTSUPP, "operation not supported" },
          { WSAEPFNOSUPPORT, "protocol family not supported" },
          { WSAEAFNOSUPPORT, "address family not supported" },
          { WSAEADDRINUSE, "address already in use" },
          { WSAEADDRNOTAVAIL, "unable to assign requested address" },
          { WSAENETDOWN, "network down" },
          { WSAENETUNREACH, "network unreachable" },
          { WSAENETRESET, "dropped connection on reset" },
          { WSAECONNABORTED, "connection aborted" },
          { WSAECONNRESET, "connect reset by peer" },
          { WSAENOBUFS, "no buffer space available" },
          { WSAEISCONN, "socket already connected" },
          { WSAENOTCONN, "socket not connected" },
          { WSAESHUTDOWN, "connection shut down" },
          { WSAETOOMANYREFS, "too many references to kernel object" },
          { WSAETIMEDOUT, "connection timed out" },
          { WSAECONNREFUSED, "connection refused" },
          { WSAELOOP, "unable to translate name" },
          { WSAENAMETOOLONG, "name or name component too long" },
          { WSAEHOSTDOWN, "host down" },
          { WSAEHOSTUNREACH, "host unreachable" },
          { WSAENOTEMPTY, "unable to remove non-empty directory" },
          { WSAEPROCLIM, "process limit exceeded" },
          { WSAEUSERS, "quota exceeded" },
          { WSAEDQUOT, "disk quota exceeded" },
          { WSAESTALE, "stale socket handle" },
          { WSAEREMOTE, "item not available locally" },
          { WSASYSNOTREADY, "network service not available" },
          { WSAVERNOTSUPPORTED, "unsupported winsock version requested" },
          { WSANOTINITIALISED, "winsock not initialised" },
          { WSAEDISCON, "peer disconnecting" },
          { WSAENOMORE, "no further lookup results" },
          { WSAECANCELLED, "lookup cancelled" },
          { WSAEINVALIDPROCTABLE, "invalid procedure call table" },
          { WSAEINVALIDPROVIDER, "invalid service provider" },
          { WSAEPROVIDERFAILEDINIT, "service provider initialization failed" },
          { WSASYSCALLFAILURE, "system call failure" },
          { WSASERVICE_NOT_FOUND, "unknown service" },
          { WSATYPE_NOT_FOUND, "unknown type" },
          { WSA_E_NO_MORE, "no further lookup results" },
          { WSA_E_CANCELLED, "lookup cancelled" },
          { WSAEREFUSED, "lookup query refused" },
          { WSAHOST_NOT_FOUND, "unknown host" },
          { WSATRY_AGAIN, "try again" },
          { WSANO_RECOVERY, "non-recoverable lookup failure" },
          { WSANO_DATA, "no data found" },
          { 0, 0 }
      };

      for (unsigned i = 0; error_msgs[i].m != 0; ++i)
          if (error_msgs[i].n == errnum) {
              s << error_msgs[i].m;
              break;
          }
    }

    s << " (" << errnum << ")";
    return s.str();

#else

    return strerror(errnum);

#endif
}
//####################################################################
