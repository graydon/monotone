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
#ifndef	BOOST_SOCKET_IMPL_DEFAULT_SOCKET_PROACTOR_IMPL_MIAN021218_HPP
#define	BOOST_SOCKET_IMPL_DEFAULT_SOCKET_PROACTOR_IMPL_MIAN021218_HPP 1


#include "boost/socket/config.hpp"
#include "boost/socket/socket_errors.hpp"

#include "boost/config.hpp"
#include "boost/utility.hpp"
#include "boost/function.hpp"
#include "boost/thread/xtime.hpp" 

namespace boost
{
    namespace	socket
    {
        namespace impl
        {
            //! Provides an implementation of	SocketProactor concept
            /** This provides	a platform neutral set of calls,
            with no attempt at error handling.
            */
            class default_socket_proactor
                : boost::noncopyable
            {
            public:

                typedef	boost::xtime time;
                typedef boost::function0<time> timer_callback_t;
                
                default_socket_proactor();
                ~default_socket_proactor();

                ///	attach AsynchSocket	to this	(socket	will not be	removed	from the proactor 
                ///	until close	of socket or queue).
                /// @return true if successfully added, false if proactor is 'full'
                bool attach(socket_t socket);

                /// add a timer callback	(returning a new duration when to fire again or	ptime(0) 
                /// if not to fire again)
                /// @arg fireTime ptime given in UTC
                bool set_timer(time fireTime, timer_callback_t callback);

                /// dispatch an event from "queue" returns true if event	is dispatched and false
                /// on timeout.
                /// @arg timeout ptime given in UTC
                bool dispatch(time timeout);

            private:
                class pimpl;
                pimpl* m_impl;
            };
        }//	namespace
    }// namespace
}//	namespace


#endif
