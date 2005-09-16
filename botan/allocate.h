/*************************************************
* Allocator Header File                          *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_ALLOCATOR_H__
#define BOTAN_ALLOCATOR_H__

#include <botan/types.h>
#include <string>

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

/*************************************************
* Get an allocator                               *
*************************************************/
Allocator* get_allocator(const std::string& = "");

/*************************************************
* Set the default allocator type                 *
*************************************************/
std::string set_default_allocator(const std::string&);

/*************************************************
* Add new allocator type                         *
*************************************************/
bool add_allocator_type(const std::string&, Allocator*);

}

#endif
