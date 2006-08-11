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
 * This file contains the implementation of the call_accept function.
**/

// common header
#include "common.h"

// Netxx includes
#include "accept.h"
#include "netxx/sockopt.h"
#include "netxx/types.h"
#include "sockaddr.h"
#include "socket.h"

//####################################################################
Netxx::Peer Netxx::call_accept (Socket &socket, bool dont_block) 
{
    SockOpt socket_options(socket.get_socketfd(), true);
    if (dont_block) socket_options.set_non_blocking();

    SockAddr socket_address(socket.get_type());
    sockaddr *sa = socket_address.get_sa();
    os_socklen_type sa_size = socket_address.get_sa_size();
    os_socklen_ptr_type sa_size_ptr = get_socklen_ptr(sa_size);

    for (;;) {
	socket_type client = accept(socket.get_socketfd(), sa, sa_size_ptr);
	if (client >= 0) return Peer(client, sa, sa_size);

	error_type error_code = get_last_error();

	switch (error_code) {
	    case EINTR:
		continue;

	    case EWOULDBLOCK:
	    case ECONNABORTED:
		return Peer();

	    default:
	    {
		std::string error("accept(2) error: ");
		error += str_error(error_code);
		throw Netxx::Exception(error);
	    }
	}
    }
}
//####################################################################
