/* Copyright (c) 2003 Michel André (michel@andre.net)
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Dr John Maddock makes no representations
 * about the suitability of this software for any purpose.  
 * It is provided "as is" without express or implied warranty.
 */

// This filed includes implementation files for different implementations
// based on platform/compiler settings


#include "boost/socket/config.hpp"

// include library source files:
#if defined(BOOST_HAS_WINSOCKETS) && defined(BOOST_HAS_THREADS)


#include "libs/socket/src/impl/win32/default_asynch_socket_impl.cpp"
#include "libs/socket/src/impl/win32/default_socket_proactor.cpp"

#endif


