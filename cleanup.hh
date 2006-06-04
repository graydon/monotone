#ifndef __CLEANUP_HH__
#define __CLEANUP_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This is a little "auto-cleanup" container, used to ensure things
// from our helper C libraries are deallocated when we leave a scope.

template <typename T, typename R>
struct cleanup_ptr {
  T ptr;
  R (* cleanup)(T);
  explicit cleanup_ptr(T p, R (*c)(T)) : ptr(p), cleanup(c) {}
  ~cleanup_ptr()
  {
    if (cleanup && ptr)
      cleanup(ptr);
  }

  T operator()()
  {
    return ptr;
  }

  T * paddr()
  {
    return &ptr;
  }
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CLEANUP_HH__
