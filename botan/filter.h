/*************************************************
* Filter Header File                             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_FILTER_H__
#define BOTAN_FILTER_H__

#include <botan/base.h>
#include <vector>

namespace Botan {

/*************************************************
* Filter Base Class                              *
*************************************************/
class Filter
   {
   public:
      virtual void write(const byte[], u32bit) = 0;
      virtual void start_msg() {}
      virtual void end_msg() {}
      virtual bool attachable() { return true; }
      void new_msg();
      void finish_msg();
      virtual ~Filter() {}
   protected:
      virtual void send(const byte[], u32bit);
      void send(byte input) { send(&input, 1); }
      void send(const MemoryRegion<byte>& in) { send(in.begin(), in.size()); }

      void attach(Filter*);
      u32bit total_ports() const;
      u32bit current_port() const { return port_num; }
      void set_port_count(u32bit);
      void set_port(u32bit);
      u32bit owns() const { return filter_owns; }
      void incr_owns() { filter_owns++; }
      Filter(u32bit = 1);
   private:
      friend class Pipe;
      friend class Fork;
      Filter(const Filter&) {}
      Filter& operator=(const Filter&) { return (*this); }
      Filter* get_next() const;
      SecureVector<byte> write_queue;
      std::vector<Filter*> next;
      u32bit port_num, filter_owns;
   };

}

#endif
