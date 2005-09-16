/*************************************************
* Pooling Allocator Header File                  *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_POOLING_ALLOCATOR_H__
#define BOTAN_POOLING_ALLOCATOR_H__

#include <botan/allocate.h>
#include <botan/exceptn.h>
#include <botan/mutex.h>
#include <vector>

namespace Botan {

/*************************************************
* Pooling Allocator                              *
*************************************************/
class Pooling_Allocator : public Allocator
   {
   public:
      void* allocate(u32bit) const;
      void deallocate(void*, u32bit) const;

      void init();
      void destroy();

      Pooling_Allocator(u32bit = 0);
      ~Pooling_Allocator();
   private:
      class Buffer
         {
         public:
            void* buf;
            u32bit length;
            bool in_use;

            bool operator<(const Buffer& x) const
               { return ((const byte*)buf < (const byte*)x.buf); }

            Buffer() { buf = 0; length = 0; in_use = false; }
            Buffer(void* b, u32bit l, bool used = false)
               { buf = b; length = l; in_use = used; }
         };

      void* get_block(u32bit) const;
      void free_block(void*, u32bit) const;

      virtual void* alloc_block(u32bit) const = 0;
      virtual void dealloc_block(void*, u32bit) const = 0;
      virtual u32bit prealloc_bytes() const { return 0; }
      virtual u32bit keep_free() const { return 64*1024; }

      void* alloc_hook(void*, u32bit) const;
      void dealloc_hook(void*, u32bit) const;
      void consistency_check() const;

      void* find_free_block(u32bit) const;
      void defrag_free_list() const;

      static bool are_contiguous(const Buffer&, const Buffer&);
      u32bit find_block(void*) const;
      bool same_buffer(Buffer&, Buffer&) const;
      void remove_empty_buffers(std::vector<Buffer>&) const;

      static bool is_empty_buffer(const Buffer&);

      const u32bit PREF_SIZE, ALIGN_TO;
      mutable std::vector<Buffer> real_mem, free_list;
      mutable Mutex* lock;
      mutable u32bit defrag_counter;
      bool initialized, destroyed;
   };

}

#endif
