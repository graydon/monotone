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
 * This file contains the implementation of the resolve_hostname function
 * using the getaddrinfo system call.
**/

// common header
#include "common.h"

// Netxx includes
#include "resolve.h"
#include "netxx/types.h"
#include "sockaddr.h"

// standard includes
#include <cstring>

#ifndef AF_UNSPEC
# define AF_UNSPEC PF_UNSPEC
#endif

//####################################################################
namespace 
{
    struct auto_addrinfo 
    {
	explicit auto_addrinfo (addrinfo *in) : ai(in) { }
	~auto_addrinfo (void) { freeaddrinfo(ai); }
	addrinfo *ai;
    }; // end auto_addrinfo
}
//####################################################################
void Netxx::resolve_hostname (const char *hostname, port_type port, bool use_ipv6, std::vector<Peer> &addrs) 
{
    addrinfo flags;
    addrinfo *info;

    std::memset(&flags, 0, sizeof(flags));

    if (use_ipv6) flags.ai_family = AF_UNSPEC;
    else flags.ai_family = AF_INET;
    flags.ai_flags = AI_CANONNAME;
#ifdef AI_ADDRCONFIG
    flags.ai_flags |= AI_ADDRCONFIG;
#endif

    // FIXME: this is a local monotone hack; it appears that getaddrinfo
    // will return datagram and stream addresses here, and we want to avoid
    // that because we only use stream addresses.  I wonder if the netxx
    // maintainer knows this. hmm.
    flags.ai_socktype = SOCK_STREAM;

    int gai_error = getaddrinfo(hostname, 0, &flags, &info);

    // Because we might compile on a computer that has AI_ADDRCONFIG and
    // run on one that doesn't, we need to check for EAI_BADFLAGS, and
    // try again without AI_ADDRCONFIG in that case.
#ifdef AI_ADDRCONFIG
    if (gai_error == EAI_BADFLAGS) {
	flags.ai_flags &= ~AI_ADDRCONFIG;
	gai_error = getaddrinfo(hostname, 0, &flags, &info);
    }
#endif

    if (gai_error != 0) {
	std::string error("name resolution failure for "); error += hostname;
	error += ": "; error += gai_strerror(gai_error);
	throw NetworkException(error);
    }

    // auto clean up
    auto_addrinfo ai(info);
    
    const char *canonname;

    for (; info != 0; info = info->ai_next) {
	canonname = info->ai_canonname ? info->ai_canonname : hostname;

	switch (info->ai_family) {
	    case AF_INET:
	    {
		SockAddr saddr(AF_INET, port);
		sockaddr_in *sai = reinterpret_cast<sockaddr_in*>(saddr.get_sa());
		std::memcpy(&(sai->sin_addr), &(reinterpret_cast<sockaddr_in*>(info->ai_addr)->sin_addr), sizeof(sai->sin_addr));
		addrs.push_back(Peer(canonname, port, sai, saddr.get_sa_size()));
	    }
	    break;

#   ifndef NETXX_NO_INET6
	    case AF_INET6:
	    {
		SockAddr saddr(AF_INET6, port);
		sockaddr_in6 *sai6 = reinterpret_cast<sockaddr_in6*>(saddr.get_sa());
		std::memcpy(&(sai6->sin6_addr), &(reinterpret_cast<sockaddr_in6*>(info->ai_addr)->sin6_addr), sizeof(sai6->sin6_addr));
		addrs.push_back(Peer(canonname, port, sai6, saddr.get_sa_size()));
	    }
	    break;
#    endif
	}
    }
}
//####################################################################
Netxx::port_type Netxx::resolve_service (const char *service)
{
    addrinfo flags;
    addrinfo *info;

    std::memset(&flags, 0, sizeof(flags));
    flags.ai_family = AF_INET;

    if (getaddrinfo(0, service, &flags, &info) != 0) {
	std::string error("service name resolution failed for: "); error += service;
	throw NetworkException(error);
    }

    auto_addrinfo ai(info);
    return ntohs(reinterpret_cast<sockaddr_in*>(info->ai_addr)->sin_port);
}
//####################################################################
