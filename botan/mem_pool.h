/*************************************************
* Pooling Allocator Header File                  *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_POOLING_ALLOCATOR_H__
#define BOTAN_POOLING_ALLOCATOR_H__

#include <botan/allocate.h>
#include <botan/exceptn.h>
#include <botan/mutex.h>
#include <utility>
#include <vector>
#include <set>

namespace Botan {

/*************************************************
* Pooling Allocator                              *
*************************************************/
class Pooling_Allocator : public Allocator
   {
   public:
      void* allocate(u32bit);
      void deallocate(void*, u32bit);

      void init();
      void destroy();

      Pooling_Allocator(u32bit, bool);
      ~Pooling_Allocator();
   private:
      void get_more_core(u32bit);
      byte* allocate_blocks(u32bit);

      virtual void* alloc_block(u32bit) = 0;
      virtual void dealloc_block(void*, u32bit) = 0;

      struct Memory_Block
         {
         u64bit bitmap;
         byte* buffer;
         u32bit block_size, how_empty;

         Memory_Block(void*, u32bit, u32bit);

         bool can_alloc(u32bit&, u32bit) const throw();
         bool contains(void*, u32bit) const throw();

         byte* alloc(u32bit, u32bit) throw();
         void free(void*, u32bit) throw();

         bool operator<(const Memory_Block& other) const
            { return (how_empty > other.how_empty); }
         };

      const u32bit PREF_SIZE, BLOCK_SIZE, BITMAP_SIZE;

      std::multiset<Memory_Block> blocks;
      std::vector<std::pair<void*, u32bit> > allocated;
      Mutex* mutex;
   };

}

#endif
