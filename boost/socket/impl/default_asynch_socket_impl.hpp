// Copyright (C) 2002 Michel André (michel@andre.net), Hugo Duncan
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
#ifndef BOOST_SOCKET_IMPL_DEFAULT_ASYNCH_SOCKET_IMPL_HPP
#define BOOST_SOCKET_IMPL_DEFAULT_ASYNCH_SOCKET_IMPL_HPP

#include "boost/socket/config.hpp"
#include "boost/socket/socket_errors.hpp"
#include "boost/config.hpp"
#include "boost/socket/impl/default_socket_impl.hpp"
#include "boost/function.hpp"

namespace boost
{
    namespace socket
    {
        namespace impl
        {

            //! Provides an implementation of AsynchSocketImplConcept
            /** This provides a platform neutral set of calls,
            with no attempt at error handling.
            It also tries to hide platform implementation details.

            The async_ family of functions returns a socket_errno for
            immediate errors otherwise socket_errno is supplied with the
            completionroutine. The default_asynch_socket_impl must live
            until all queued asynch notifications is called.

            @todo should this class inherit from default_socket_impl
            since its an refinment (or should we use forwarding)?
            */
            class default_asynch_socket_impl : public default_socket_impl
            {
            public:
                /// callback function used for asynchrounous completion notification
                /// @param socket_errno error for operation
                /// @param unsigned int bytes transferred in either direction for operation 
                /// (only relevant for send and receive otherwise 0)
                typedef boost::function2<void, socket_errno, unsigned  int> completion_callback_t;


                default_asynch_socket_impl();
                explicit default_asynch_socket_impl(socket_t socket);

                //! Asynchronous accpetance of connection
                /**
                @arg socket lifetime of default_asynch_socket_impl& must be until
                completionRoutine is triggered.
                @arg remoteAddress lifetime of std::pair<void *,size_t>& must be until
                completionRoutine is triggered.
                @arg completionRoutine a function to be called when accept completes.
                */
                socket_errno async_accept(
                    default_asynch_socket_impl& newSocket,
                    std::pair<void *,size_t>& remoteAddress,
                    completion_callback_t completionRoutine
                    );

                //! Asynchronous connect
                /**
                @arg address lifetime of std::pair<void *,size_t>& must be until
                completionRoutine is triggered.
                @arg completionRoutine a function to be called when connect completes.
                */
                socket_errno async_connect(
                    std::pair<void *,size_t>& address,
                    completion_callback_t completionRoutine
                    );

                //! Asynchronous receive
                /**
                @arg data pointer to buffer that receives data.
                lifetime of buffer must be until completionRoutine is triggered.
                @arg data length in bytes of receive buffer
                @arg completionRoutine a function to be called when recv completes.
                */
                socket_errno async_recv(
                    void* data,
                    int len,
                    completion_callback_t completionRoutine
                    );


                //! Asynchronous receive
                /**
                @arg data pointer to buffer that receives data.
                lifetime of buffer must be until completionRoutine is triggered.
                @arg data length in bytes of receive buffer
                @arg completionRoutine a function to be called when recv completes.
                */
                socket_errno async_send(
                    const void* data,
                    int len,
                    completion_callback_t completionRoutine
                    );
                
            };
        }// namespace impl
    }// namespace socket
}// namespace boost

#endif
