#ifndef __BASE_HH__
#define __BASE_HH__

// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains a small number of inclusions and declarations that
// should be visible to the entire program.  Include it first.

// Configuration directives
#include "config.h"

#define BOOST_DISABLE_THREADS
#define BOOST_SP_DISABLE_THREADS
#define BOOST_MULTI_INDEX_DISABLE_SERIALIZATION

#include <iosfwd>
#include <string>  // it would be nice if there were a <stringfwd>

// this template must be specialized for each type you want to dump
// (or apply MM() to -- see sanity.hh).  there are a few stock dumpers
// in appropriate places.
template <typename T>
void dump(T const &, std::string &)
{
  // the compiler will evaluate this somewhat odd construct (and issue an
  // error) if and only if this base template is instantiated.  we do not
  // use BOOST_STATIC_ASSERT mainly to avoid dragging it in everywhere;
  // also we get better diagnostics this way (the error tells you what is
  // wrong, not just that there's an assertion failure).
  enum dummy { d = (sizeof(struct dump_must_be_specialized_for_this_type)
                    == sizeof(T)) };
}

// NORETURN(void function()); declares a function that will never return
// in the normal fashion. a function that invariably throws an exception
// counts as NORETURN.
#if defined(__GNUC__)
#define NORETURN(x) x __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NORETURN(x) __declspec(noreturn) x
#else
#define NORETURN(x) x
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
