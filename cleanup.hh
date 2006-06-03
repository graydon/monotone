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

#endif // __CLEANUP_HH__
