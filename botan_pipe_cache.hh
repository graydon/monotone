#ifndef __BOTAN_PIPE_CACHE_HH__
#define __BOTAN_PIPE_CACHE_HH__

// Copyright (C) 2008 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <botan/pipe.h>
#include <boost/scoped_ptr.hpp>
#include "sanity.hh"

// This file defines a simple lifetime-of-the-program caching strategy for
// Botan::Pipe objects.  Instead of writing
//
//   Botan::Pipe p(...);
//
// you do
//
//   static cached_botan_pipe p(new Botan::Pipe(...));
//
// and then use p like a Botan::Pipe object (except with -> instead of .).
//
// The global pipe_cache_cleanup object takes care of destroying all such
// cached pipes before the Botan::LibraryInitializer object is destroyed.

class pipe_cache_cleanup;

class cached_botan_pipe
{
  friend class pipe_cache_cleanup;
  cached_botan_pipe * next_tbd;
  boost::scoped_ptr<Botan::Pipe> pipe;

public:
  cached_botan_pipe(Botan::Pipe * p);
  ~cached_botan_pipe() { I(!pipe); }
  Botan::Pipe & operator*()
  { I(pipe); return *pipe; }
  Botan::Pipe * operator->()
  { I(pipe); return pipe.get(); }

  // ??? operator bool, operator! a la boost::scoped_ptr
  // (what's with the bizarro unspecified_bool_type thing?)
};

extern Botan::Pipe * unfiltered_pipe;
extern pipe_cache_cleanup * global_pipe_cleanup_object;

class pipe_cache_cleanup
{
  friend class cached_botan_pipe;
  struct cached_botan_pipe * to_be_destroyed;

public:
  pipe_cache_cleanup() : to_be_destroyed(0)
  {
    I(!global_pipe_cleanup_object);
    global_pipe_cleanup_object = this;
  }
  ~pipe_cache_cleanup()
  {
    for (cached_botan_pipe * p = to_be_destroyed; p; p = p->next_tbd)
      p->pipe.reset(0);
    global_pipe_cleanup_object = 0;
  }
};

// must be defined after class pipe_cache_cleanup
inline cached_botan_pipe::cached_botan_pipe(Botan::Pipe * p)
  : pipe(p)
{
  I(global_pipe_cleanup_object);
  this->next_tbd = global_pipe_cleanup_object->to_be_destroyed;
  global_pipe_cleanup_object->to_be_destroyed = this;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __BOTAN_PIPE_CACHE_HH__
