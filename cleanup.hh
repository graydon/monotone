#ifndef __CLEANUP_HH__
#define __CLEANUP_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this is a little "auto-cleanup" container, used to ensure things
// from librsync, sqlite, etc. are deallocated when we leave a scope.

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
