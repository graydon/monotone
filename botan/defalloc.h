/*************************************************
* Basic Allocators Header File                   *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_BASIC_ALLOC_H__
#define BOTAN_BASIC_ALLOC_H__

#include <botan/mem_pool.h>

namespace Botan {

/*************************************************
* Malloc Allocator                              *
*************************************************/
class Malloc_Allocator : public Pooling_Allocator
   {
   private:
      void* alloc_block(u32bit) const;
      void dealloc_block(void*, u32bit) const;
   };

/*************************************************
* Locking Allocator                              *
*************************************************/
class Locking_Allocator : public Pooling_Allocator
   {
   private:
      void* alloc_block(u32bit) const;
      void dealloc_block(void*, u32bit) const;
      u32bit prealloc_bytes() const { return 128*1024; }
      bool should_not_free() const { return true; }
   };

}

#endif
