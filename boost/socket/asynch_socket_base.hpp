// Copyright (C) 2002 Michel André (michel@andre.net) Hugo Duncan
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
#ifndef BOOST_ASYNCH_SOCKET_SOCKET_BASE_HPP
#define BOOST_ASYNCH_SOCKET_SOCKET_BASE_HPP 1

#include "boost/socket/config.hpp"
#include "boost/socket/impl/default_asynch_socket_impl.hpp"
#include "boost/socket/socket_base.hpp"
#include "boost/assert.hpp"

namespace boost
{
    namespace socket
    {
        //! platform independent, low level interface
        template <
            typename ErrorPolicy=default_error_policy,
            typename SocketImpl=impl::default_asynch_socket_impl>
        class asynch_socket_base
            : public socket_base<ErrorPolicy, SocketImpl>
        {
        public:
            typedef asynch_socket_base<ErrorPolicy, SocketImpl> self_t;
            typedef socket_base<ErrorPolicy ,SocketImpl> base_t;
            //typedef MultiPlexor multiplexor_t;
            typedef boost::function2<void, socket_errno, unsigned  int> completion_callback_t;
            
            asynch_socket_base()
            {}
            
            explicit asynch_socket_base(typename self_t::socket_t socket)
                : socket_base<ErrorPolicy, SocketImpl>(socket)
            {}
            
            ~asynch_socket_base()
            {}

            //! accept a connection
            template <class Addr>
            socket_errno asynch_accept(self_t& socket, Addr& address, completion_callback_t completionCallback)
            {
                boost::function_requires< AddressConcept<Addr> >();
                std::pair<void*,size_t> rep=address.representation();

                socket_errno ret = m_socket_impl.async_accept(socket.m_socket_impl, rep, completionCallback);
                
                if (ret != Success)
                    return m_error_policy.handle_error(function::accept, ret);
                
                return Success;
            }

            //! receive data
            socket_errno asynch_recv(void* data, size_t len, completion_callback_t completionCallback)
            {
                socket_errno ret=m_socket_impl.async_recv(data, len, completionCallback);
                if (ret != Success)
                    return m_error_policy.handle_error(function::recv, ret);
                return ret;
            }

            //! send data
            /** Returns the number of bytes sent */
            socket_errno asynch_send(const void* data, size_t len, completion_callback_t completionCallback)
            {
                socket_errno ret=m_socket_impl.async_send(data, len, completionCallback);
                if (ret != Success)
                    return m_error_policy.handle_error(function::send, ret);
                return ret;
            }  
        };
    } // namespace socket
} //namespace boost

#endif
