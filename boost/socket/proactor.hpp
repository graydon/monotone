// Copyright (C) 2002 Michel André (michel@andre.net), Hugo	Duncan
// Permission to use, copy,	modify,	distribute and sell	this software
// and its documentation for any purpose is	hereby granted without fee,
// provided	that the above copyright notice	appear in all copies and
// that	both that copyright	notice and this	permission notice appear
// in supporting documentation.	 Hugo Duncan makes no representations
// about the suitability of	this software for any purpose.
// It is provided "as is" without express or implied warranty.

#ifdef _MSC_VER
#pragma	once
#endif

///	include	guard
#ifndef	BOOST_SOCKET_PROACTOR_MIAN030120_HPP
#define	BOOST_SOCKET_PROACTOR_MIAN030120_HPP 1

#include "boost/socket/impl/default_socket_proactor.hpp"

namespace boost
{
    namespace socket
    {
        typedef impl::default_socket_proactor proactor;
    } // namespace socket
} // namespace boost


#endif
