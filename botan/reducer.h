/*************************************************
* Modular Reducer Header File                    *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_MODARITH_H__
#define BOTAN_MODARITH_H__

#include <botan/bigint.h>

namespace Botan {

/*************************************************
* Modular Reducer Base Class                     *
*************************************************/
class ModularReducer
   {
   public:
      virtual BigInt multiply(const BigInt&, const BigInt&) const = 0;
      virtual BigInt square(const BigInt&) const = 0;
      virtual BigInt reduce(const BigInt&) const = 0;

      virtual const BigInt& get_modulus() const = 0;

      virtual ~ModularReducer() {}
   };

/*************************************************
* Get a modular reducer                          *
*************************************************/
ModularReducer* get_reducer(const BigInt&);

}

#endif
