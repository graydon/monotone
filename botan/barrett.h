/*************************************************
* Barrett Reducer Header File                    *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_BARRETT_H__
#define BOTAN_BARRETT_H__

#include <botan/reducer.h>

namespace Botan {

/*************************************************
* Barrett Reducer                                *
*************************************************/
class BarrettReducer : public ModularReducer
   {
   public:
      BigInt reduce(const BigInt&) const;

      BarrettReducer(const BigInt&);
   private:
      u32bit max_bits, k;
      BigInt mu;
      mutable BigInt t1, t2;
   };

}

#endif
