/*************************************************
* Basic Filters Source File                      *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/basefilt.h>

namespace Botan {

/*************************************************
* Chain Constructor                              *
*************************************************/
Chain::Chain(Filter* f1, Filter* f2, Filter* f3, Filter* f4)
   {
   if(f1) { attach(f1); incr_owns(); }
   if(f2) { attach(f2); incr_owns(); }
   if(f3) { attach(f3); incr_owns(); }
   if(f4) { attach(f4); incr_owns(); }
   }

/*************************************************
* Chain Constructor                              *
*************************************************/
Chain::Chain(Filter* filters[], u32bit count)
   {
   for(u32bit j = 0; j != count; j++)
      if(filters[j])
         {
         attach(filters[j]);
         incr_owns();
         }
   }

/*************************************************
* Fork Constructor                               *
*************************************************/
Fork::Fork(Filter* f1, Filter* f2, Filter* f3, Filter* f4)
   {
   u32bit used = 0;
   if(f1) used = 1;
   if(f2) used = 2;
   if(f3) used = 3;
   if(f4) used = 4;
   set_port_count(used);
   if(f1) next[0] = f1;
   if(f2) next[1] = f2;
   if(f3) next[2] = f3;
   if(f4) next[3] = f4;
   }

/*************************************************
* Fork Constructor                               *
*************************************************/
Fork::Fork(Filter* filters[], u32bit count) : Filter(count)
   {
   for(u32bit j = 0; j != count; j++)
      next[j] = filters[j];
   }

/*************************************************
* Set the algorithm key                          *
*************************************************/
void Keyed_Filter::set_key(const SymmetricKey& key)
   {
   if(base_ptr)
      base_ptr->set_key(key);
   else
      throw Invalid_State("Keyed_Filter::set_key: No base algorithm set");
   }

/*************************************************
* Check if a keylength is valid                  *
*************************************************/
bool Keyed_Filter::valid_keylength(u32bit n) const
   {
   if(base_ptr)
      return base_ptr->valid_keylength(n);
   throw Invalid_State("Keyed_Filter::valid_keylength: No base algorithm set");
   }

}
