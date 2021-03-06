/*************************************************
* Allocator Header File                          *
* (C) 1999-2007 Jack Lloyd                       *
*************************************************/

#ifndef BOTAN_ALLOCATOR_H__
#define BOTAN_ALLOCATOR_H__

#include <botan/types.h>
#include <string>

namespace Botan {

/*************************************************
* Allocator Interface                            *
*************************************************/
class BOTAN_DLL Allocator
   {
   public:
      static Allocator* get(bool);

      virtual void* allocate(u32bit) = 0;
      virtual void deallocate(void*, u32bit) = 0;

      virtual std::string type() const = 0;

      virtual void init() {}
      virtual void destroy() {}

      virtual ~Allocator() {}
   };

}

#endif
