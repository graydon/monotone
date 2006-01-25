/*************************************************
* Pooling Allocator Source File                  *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/mem_pool.h>
#include <botan/libstate.h>
#include <botan/conf.h>
#include <botan/bit_ops.h>
#include <botan/util.h>

namespace Botan {

namespace {

/*************************************************
* Decide how much memory to allocate at once     *
*************************************************/
u32bit choose_pref_size(u32bit provided)
   {
   if(provided)
      return provided;

   u32bit result = Config::get_u32bit("base/memory_chunk");
   if(result)
      return result;

   return 16*1024;
   }

/*************************************************
* Return a metric of how 'empty' this bitmap is  *
*************************************************/
u32bit emptiness_metric(u64bit bitmap)
   {
   u32bit metric = 0;

#if 0
   for(u32bit j = 0; j != 8; ++j)
      if(get_byte(j, bitmap) == 0)
         metric++;
#else
   

#endif
   return metric;
   }

}

/*************************************************
* Memory_Block Constructor                       *
*************************************************/
Pooling_Allocator::Memory_Block::Memory_Block(void* buf, u32bit map_size,
                                              u32bit block_size)
   {
   buffer = static_cast<byte*>(buf);
   bitmap = 0;
   how_empty = emptiness_metric(bitmap);
   this->block_size = block_size;

   clear_mem(buffer, block_size * 64);

   if(map_size != 64)
      throw Invalid_Argument("Memory_Block: Bad bitmap size, must be 64");
   }

/*************************************************
* See if ptr is contained by this block          *
*************************************************/
bool Pooling_Allocator::Memory_Block::contains(void* ptr,
                                               u32bit length) const throw()
   {
   return (buffer <= ptr &&
          ((byte*)ptr+length*block_size) <= (buffer + 64 * block_size));
   }

/*************************************************
* Find a spot for a new allocation               *
*************************************************/
bool Pooling_Allocator::Memory_Block::can_alloc(u32bit& start,
                                                u32bit n) const throw()
   {
   start = 0;

   if(n == 0 || n > 64 || (n == 64 && bitmap))
      return false;

   if(n == 64)
      return true;

   const u64bit mask = ((u64bit)1 << n) - 1;

   for(u32bit j = 0; j != 64 - n; ++j)
      {
      if((bitmap >> j) & mask)
         continue;

      start = j;
      return true;
      }

   return false;
   }

/*************************************************
* Allocate some memory, if possible              *
*************************************************/
byte* Pooling_Allocator::Memory_Block::alloc(u32bit start,
                                             u32bit n) throw()
   {
   if(start == 0 && n == 64)
      {
      bitmap = ~bitmap;
      return buffer;
      }

   u64bit mask = (((u64bit)1 << n) - 1) << start;

   bitmap |= mask;
   how_empty = emptiness_metric(bitmap);
   return buffer + start * block_size;
   }

/*************************************************
* Mark this memory as free, if we own it         *
*************************************************/
void Pooling_Allocator::Memory_Block::free(void* ptr, u32bit blocks) throw()
   {
   clear_mem((byte*)ptr, blocks * block_size);

   const u32bit start = ((byte*)ptr - buffer) / block_size;

   if(start == 0 && blocks == 64)
      bitmap = ~bitmap;
   else
      {
      for(u32bit j = 0; j != blocks; ++j)
         bitmap &= ~((u64bit)1 << (j+start));
      }

   how_empty = emptiness_metric(bitmap);
   }

/*************************************************
* Pooling_Allocator Constructor                  *
*************************************************/
Pooling_Allocator::Pooling_Allocator(u32bit p_size, bool) :
   PREF_SIZE(choose_pref_size(p_size)), BLOCK_SIZE(64), BITMAP_SIZE(64)
   {
   mutex = global_state().get_mutex();
   }

/*************************************************
* Pooling_Allocator Destructor                   *
*************************************************/
Pooling_Allocator::~Pooling_Allocator()
   {
   delete mutex;
   if(blocks.size())
      throw Invalid_State("Pooling_Allocator: Never released memory");
   }

/*************************************************
* Allocate some initial buffers                  *
*************************************************/
void Pooling_Allocator::init()
   {
   Mutex_Holder lock(mutex);

   get_more_core(PREF_SIZE);
   }

/*************************************************
* Free all remaining memory                      *
*************************************************/
void Pooling_Allocator::destroy()
   {
   Mutex_Holder lock(mutex);

   blocks.clear();

   for(u32bit j = 0; j != allocated.size(); ++j)
      dealloc_block(allocated[j].first, allocated[j].second);
   allocated.clear();
   }

/*************************************************
* Allocation                                     *
*************************************************/
void* Pooling_Allocator::allocate(u32bit n)
   {
   Mutex_Holder lock(mutex);

   if(n <= BITMAP_SIZE * BLOCK_SIZE)
      {
      const u32bit block_no = round_up(n, BLOCK_SIZE) / BLOCK_SIZE;

      byte* mem = allocate_blocks(block_no);
      if(mem)
         return mem;

      get_more_core(PREF_SIZE);

      mem = allocate_blocks(block_no);
      if(mem)
         return mem;

      throw Memory_Exhaustion();
      }

   void* new_buf = alloc_block(n);
   if(new_buf)
      return new_buf;

   throw Memory_Exhaustion();
   }

/*************************************************
* Deallocation                                   *
*************************************************/
void Pooling_Allocator::deallocate(void* ptr, u32bit n)
   {
   if(ptr == 0 && n == 0)
      return;

   Mutex_Holder lock(mutex);

   if(n <= BITMAP_SIZE * BLOCK_SIZE)
      {
      const u32bit block_no = round_up(n, BLOCK_SIZE) / BLOCK_SIZE;

      std::multiset<Memory_Block>::iterator i = blocks.begin();
      while(i != blocks.end())
         {
         if(i->contains(ptr, block_no))
            {
            Memory_Block block = *i;
            blocks.erase(i);

            block.free(ptr, block_no);
            blocks.insert(block);
            return;
            }
         ++i;
         }

      throw Invalid_State("Pointer released to the wrong allocator");
      }

   dealloc_block(ptr, n);
   }

/*************************************************
* Allocate more memory for the pool              *
*************************************************/
byte* Pooling_Allocator::allocate_blocks(u32bit n)
   {
   std::multiset<Memory_Block>::iterator i = blocks.begin();

   while(i != blocks.end())
      {
      u32bit start = 0;

      if(i->can_alloc(start, n))
         {
         Memory_Block block = *i;
         blocks.erase(i);

         byte* mem = block.alloc(start, n);
         blocks.insert(block);

         return mem;
         }

      ++i;
      }

   return 0;
   }

/*************************************************
* Allocate more memory for the pool              *
*************************************************/
void Pooling_Allocator::get_more_core(u32bit in_bytes)
   {
   const u32bit TOTAL_BLOCK_SIZE = BLOCK_SIZE * BITMAP_SIZE;

   const u32bit in_blocks = round_up(in_bytes, BLOCK_SIZE) / TOTAL_BLOCK_SIZE;
   const u32bit to_allocate = in_blocks * TOTAL_BLOCK_SIZE;

   byte* ptr = static_cast<byte*>(alloc_block(to_allocate));

   if(ptr == 0)
      throw Memory_Exhaustion();

   allocated.push_back(std::make_pair(ptr, to_allocate));

   for(u32bit j = 0; j != in_blocks; ++j)
      blocks.insert(
         Memory_Block(ptr + j * TOTAL_BLOCK_SIZE, BITMAP_SIZE, BLOCK_SIZE)
         );
   }
}
