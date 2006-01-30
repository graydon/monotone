/*************************************************
* Pooling Allocator Source File                  *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/mem_pool.h>
#include <botan/libstate.h>
#include <botan/conf.h>
#include <botan/bit_ops.h>
#include <botan/util.h>
#include <algorithm>

#include <assert.h> // testing

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

}

/*************************************************
* Memory_Block Constructor                       *
*************************************************/
Pooling_Allocator::Memory_Block::Memory_Block(void* buf, u32bit map_size,
                                              u32bit block_size)
   {
   buffer = static_cast<byte*>(buf);
   bitmap = 0;
   this->block_size = block_size;

   clear_mem(buffer, block_size * 64);

   if(map_size != 64)
      throw Invalid_Argument("Memory_Block: Bad bitmap size, must be 64");
   }

/*************************************************
* Compare a Memory_Block with a void pointer     *
*************************************************/
bool Pooling_Allocator::Memory_Block::operator<(const void* other) const
   {
   if(buffer <= other && other < (buffer + 64 * block_size))
      return false;
   return (buffer < other);
   }

/*************************************************
* Compare two Memory_Block objects               *
*************************************************/
bool Pooling_Allocator::Memory_Block::operator<(const Memory_Block& blk) const
   {
   return (buffer < blk.buffer);
   }

/*************************************************
* See if ptr is contained by this block          *
*************************************************/
bool Pooling_Allocator::Memory_Block::contains(void* ptr,
                                               u32bit length) const throw()
   {
   return (buffer <= ptr &&
          ((byte*)ptr + length * block_size) <= (buffer + 64 * block_size));
   }

/*************************************************
* Allocate some memory, if possible              *
*************************************************/
byte* Pooling_Allocator::Memory_Block::alloc(u32bit n) throw()
   {
   if(n == 0 || n > 64)
      return 0;

   if(n == 64)
      {
      if(bitmap)
         return 0;
      else
         {
         bitmap = ~bitmap;
         return buffer;
         }
      }

   u64bit mask = ((u64bit)1 << n) - 1;
   u32bit offset = 0;

   while(bitmap & mask)
      {
      mask <<= 1;
      ++offset;

      if((bitmap & mask) == 0)
         break;
      if(mask >> 63)
         break;
      }

   if(bitmap & mask)
      return 0;

   bitmap |= mask;
   return buffer + offset * block_size;
   }

/*************************************************
* Mark this memory as free, if we own it         *
*************************************************/
void Pooling_Allocator::Memory_Block::free(void* ptr, u32bit blocks) throw()
   {
   clear_mem((byte*)ptr, blocks * block_size);

   const u32bit offset = ((byte*)ptr - buffer) / block_size;

   if(offset == 0 && blocks == 64)
      bitmap = ~bitmap;
   else
      {
      for(u32bit j = 0; j != blocks; ++j)
         bitmap &= ~((u64bit)1 << (j+offset));
      }
   }

/*************************************************
* Pooling_Allocator Constructor                  *
*************************************************/
Pooling_Allocator::Pooling_Allocator(u32bit p_size, bool) :
   PREF_SIZE(choose_pref_size(p_size)), BLOCK_SIZE(64), BITMAP_SIZE(64)
   {
   mutex = global_state().get_mutex();
   last_used = blocks.begin();
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

   if(n > BITMAP_SIZE * BLOCK_SIZE)
      dealloc_block(ptr, n);
   else
      {
      const u32bit block_no = round_up(n, BLOCK_SIZE) / BLOCK_SIZE;

      std::vector<Memory_Block>::iterator i =
         std::lower_bound(blocks.begin(), blocks.end(), ptr);

      if(i != blocks.end() && i->contains(ptr, block_no))
         i->free(ptr, block_no);
      else
         throw Invalid_State("Pointer released to the wrong allocator");
      }
   }

/*************************************************
* Allocate more memory for the pool              *
*************************************************/
byte* Pooling_Allocator::allocate_blocks(u32bit n)
   {
   if(blocks.size() == 0)
      return 0;

   assert(last_used != blocks.end());

   std::vector<Memory_Block>::iterator i = last_used;

   do
      {
      i++;
      if(i == blocks.end())
         i = blocks.begin();

      byte* mem = i->alloc(n);
      if(mem)
         {
         last_used = i;
         return mem;
         }

      }
   while(i != last_used);

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

   void* ptr = alloc_block(to_allocate);
   if(ptr == 0)
      throw Memory_Exhaustion();

   allocated.push_back(std::make_pair(ptr, to_allocate));

   for(u32bit j = 0; j != in_blocks; ++j)
      {
      byte* byte_ptr = static_cast<byte*>(ptr);

      blocks.push_back(
         Memory_Block(byte_ptr + j * TOTAL_BLOCK_SIZE, BITMAP_SIZE, BLOCK_SIZE)
         );
      }
   std::sort(blocks.begin(), blocks.end());
   last_used = std::lower_bound(blocks.begin(), blocks.end(), ptr);
   }

}
