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
#ifndef BOOST_SOCKET_IMPL_WIN32_OVERLAPPED_MIAN021216_HPP
#define BOOST_SOCKET_IMPL_WIN32_OVERLAPPED_MIAN021216_HPP

#include "boost/socket/impl/default_asynch_socket_impl.hpp"

namespace boost
{
    namespace socket
    {
        namespace impl
        {
            /// Specialization of Win32 overlapped structure.
            struct overlapped
            {
                typedef default_asynch_socket_impl::completion_callback_t completion_callback_t;

                overlapped(completion_callback_t completion_callback, void* data, unsigned int len) : m_completion_callback(completion_callback)
                {
                    ::ZeroMemory(&m_overlapped, sizeof(OVERLAPPED));
                    m_buffer.len = len;
                    m_buffer.buf = static_cast<char*>(data);
                }

                /// Convert LPOVERLAPPED to overlapped
                static overlapped* from_overlapped(LPOVERLAPPED lpOverlapped)
                {
                    return reinterpret_cast<overlapped*>(lpOverlapped);
                }

                /// Convert overlapped to LPOVERLAPPED
                LPOVERLAPPED os_overlapped() 
                { 
                    return &m_overlapped;
                }

                LPWSABUF buffer() 
                {
                    return &m_buffer;
                }

                void complete(socket_errno err, unsigned int bytesTransferred)
                {
                    m_completion_callback(err, bytesTransferred);
                }

            private:
                OVERLAPPED              m_overlapped;
                WSABUF                  m_buffer;
                completion_callback_t   m_completion_callback;
            };

        }// namespace impl
    }// namespace socket
}// namespace boost

#endif
