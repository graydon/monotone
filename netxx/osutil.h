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
 * This file contains definitions for OS error functions.
**/

#ifndef _netxx_osutil_h_
#define _netxx_osutil_h_

#if defined (WIN32)
# include <winsock2.h>
# include <winbase.h>
# include <errno.h>

# undef  EINTR
# define EINTR WSAEINTR

# undef  EWOULDBLOCK
# define EWOULDBLOCK WSAEWOULDBLOCK

# undef  EINPROGRESS
# define EINPROGRESS WSAEINPROGRESS

# undef  EAFNOSUPPORT
# define EAFNOSUPPORT WSAEAFNOSUPPORT

# undef  ENOSPC
# define ENOSPC WSAENOSPC

# undef  ECONNRESET
# define ECONNRESET WSAECONNRESET

# undef  ECONNABORTED
# define ECONNABORTED WSAECONNABORTED
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <sys/param.h>
# include <sys/un.h>
# include <unistd.h>
# include <sys/stat.h>
# include <netdb.h>
# include <fcntl.h>
# include <errno.h>

# include <string.h>
# include <stdlib.h>
#endif

#include <string>
#include "config.h"

namespace Netxx 
{
#if defined (WIN32)
    typedef DWORD error_type;
#else
    typedef int error_type;
#endif
    error_type get_last_error (void);
    std::string str_error(error_type);


#if defined(HAVE_SOCKLEN_T)
    typedef socklen_t  os_socklen_type;
    typedef socklen_t* os_socklen_ptr_type;
#   define get_socklen_ptr(x) &x
#else
    typedef int  os_socklen_type;
    typedef int* os_socklen_ptr_type;
#   define get_socklen_ptr(x) reinterpret_cast<int*>(&x)
#endif

} // end Netxx namespace

#endif
