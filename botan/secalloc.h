/*************************************************
* Memory Allocator Header File                   *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_ALLOCATOR_H__
#define BOTAN_ALLOCATOR_H__

#include <botan/types.h>

namespace Botan {

/*************************************************
* Allocator                                      *
*************************************************/
class Allocator
   {
   public:
      virtual void* allocate(u32bit) const = 0;
      virtual void deallocate(void*, u32bit) const = 0;

      virtual void init() {}
      virtual void destroy() {}

      virtual ~Allocator() {}
   };

}

#endif
