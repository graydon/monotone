// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef CURRENT_EXCEPTION_HH
#define CURRENT_EXCEPTION_HH

#include <typeinfo>

// Add #ifdeffage here as appropriate for other compiler-specific ways to
// get this information.  Windows note: as best I can determine from poking
// around on MSDN, MSVC type_info.name() is already demangled, and there is
// no documented equivalent of __cxa_current_exception_type().
#ifdef HAVE_CXXABI_H
 #include <cxxabi.h>
 #ifdef HAVE___CXA_DEMANGLE
  inline char const * demangle_typename(char const * name)
  {
    int status = -1;
    char * dem = abi::__cxa_demangle(name, 0, 0, &status);
    if (status == 0)
      return dem;
    else
      return 0;
  }
 #else
  #define demangle_typename(x) 0
 #endif
 #ifdef HAVE___CXA_CURRENT_EXCEPTION_TYPE
  #define get_current_exception_type() abi::__cxa_current_exception_type()
 #else
  #define get_current_exception_type() 0
 #endif
#else
 #define demangle_typename(x) 0
 #define get_current_exception_type() 0
#endif

#endif
