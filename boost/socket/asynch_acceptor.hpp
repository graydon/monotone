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
#ifndef BOOST_SOCKET_IMPL_DEFAULT_ASYNCH_SOCKET_ACCEPTOR_HPP
#define BOOST_SOCKET_IMPL_DEFAULT_ASYNCH_SOCKET_ACCEPTOR_HPP

#include "boost/socket/config.hpp"
#include "boost/socket/asynch_data_socket.hpp"
#include "boost/function.hpp"
#include "boost/bind.hpp"

namespace boost
{
    namespace socket
    {
        namespace detail
        {
            void accept_completion(socket_errno error, unsigned int /* not used for accept */,  boost::function1<void, socket_errno> callback)
            {
                callback(error);
            }
        }
        
        template <
            typename Multiplexor,
            typename SocketBase = asynch_socket_base<> >
        class asynch_acceptor
        {
        public:
            typedef Multiplexor multiplexor_t;
            typedef SocketBase socket_base_t;
            typedef typename socket_base_t::socket_t socket_t;
            typedef typename socket_base_t::error_policy error_policy;
            typedef asynch_data_socket<multiplexor_t, socket_base_t> data_connection_t;
            typedef boost::function1<void, socket_errno> completion_callback_t;
            
            asynch_acceptor(multiplexor_t& multiplexor) : m_multiplexor(multiplexor)
            {
            }
            
            template <typename Protocol, class Addr>
            socket_errno open(
                const Protocol& protocol, 
                const Addr& address)
            {
                const socket_errno open_error = base().open(protocol);
                if (open_error != Success)
                    return open_error;

                const socket_errno bind_error = base().bind(address);
                if (bind_error != Success)
                    return bind_error;

                 // set the socket to non-blocking so that listen will succeed
                option::non_blocking non_blocking(true);
                base().ioctl(non_blocking);

                const socket_errno listen_error = base().listen(10);
                if (listen_error != Success && listen_error != WouldBlock)
                    return listen_error;

                m_impl.attach(m_multiplexor); // @todo handle error from this call and return
                
                return Success;
            }
            
            template <class Addr>
            socket_errno asynch_accept(
                data_connection_t& data_socket, 
                Addr& address, 
                completion_callback_t callback)
            {
                return m_impl.do_accept(data_socket, address, callback);
            }
            

        private:
            socket_base_t& base()
            {
                return m_impl.m_listenSocket;
            }

                        
            template<typename SCHEME> // Just handle asynch scheme for now (add implementation for event based scheme (select))
            struct accept_impl 
            {
                template <class Addr>
                socket_errno do_accept(
                    data_connection_t& data_socket, 
                    Addr& address, 
                    completion_callback_t callback)
                {
                    return m_listenSocket.asynch_accept(
                        data_socket.base(),
                        address,
                        boost::bind(detail::accept_completion, _1, _2, callback)
                        );
                }
                
                void attach(Multiplexor& multiplexor) 
                {
                    multiplexor.attach(m_listenSocket.socket());
                }

                socket_base_t m_listenSocket;
            };
            accept_impl<multiplexor_t> m_impl;
            multiplexor_t& m_multiplexor;
            
        };
    } // namespace socket
} // namespace boost

#endif
