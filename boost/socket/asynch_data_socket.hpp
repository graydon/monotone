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
#ifndef BOOST_SOCKET_ASYNCH_DATA_SOCKET_HPP
#define BOOST_SOCKET_ASYNCH_DATA_SOCKET_HPP 1

#include "boost/socket/asynch_socket_base.hpp"

namespace boost
{
    namespace socket
    {
        //! restricted interfaces using socket_base, but with reduced functionality
        /** The aim here is to provide a restricted programming interface, built
        on the platform neutral asynch_socket_base. */

        //! asynch_data_socket
        /** interface for a asynchrounus data stream */
        template <
            typename Multiplexor,
            typename SocketBase = asynch_socket_base<>
                 >
        class asynch_data_socket
            : public data_socket<SocketBase>
        {
        public:
            typedef SocketBase socket_base_t;
            typedef Multiplexor multiplexor_t;
            
            typedef typename socket_base_t::socket_t socket_t;
            typedef typename socket_base_t::error_policy error_policy;

            typedef boost::function2<void, socket_errno, unsigned  int> completion_callback_t;
            
            
            //default_asynch_socket_impl();
            asynch_data_socket(multiplexor_t& multiplexor)
                :  data_socket<SocketBase>(), m_multiplexor(multiplexor)
            {}

            explicit asynch_data_socket(socket_t socket, multiplexor_t& multiplexor)
                : data_socket<SocketBase>(socket), m_multiplexor(multiplexor)
            {}
            
            //! Asynchronous receive
            socket_errno asynch_recv(void* data, int len, completion_callback_t completionRoutine)
            {
                return base().asynch_recv(data, len, completionRoutine);
            }
            
            //! Asynchronous receive
            socket_errno asynch_send(const void* data, int len, completion_callback_t completionRoutine)
            {
                return base().asynch_send(data, len, completionRoutine);
            }

            template<typename Protocol>
            socket_errno open(Protocol protocol)
            {
                return base().open(protocol);
            }
            
            
        private:
            multiplexor_t& m_multiplexor;
        }; // class asynch_data_socket        
    } // namespace socket
} // namespace boost

#endif
