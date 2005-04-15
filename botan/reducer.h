/*************************************************
* Modular Reducer Header File                    *
* (C) 1999-2004 The Botan Project                *
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
      virtual BigInt multiply(const BigInt&, const BigInt&) const;
      virtual BigInt square(const BigInt&) const;
      virtual BigInt reduce(const BigInt&) const = 0;

      virtual bool must_convert() const { return false; }

      virtual BigInt convert_in(const BigInt&) const;
      virtual BigInt convert_out(const BigInt&) const;

      const BigInt& get_modulus() const { return modulus; }

      ModularReducer(const BigInt&);
      virtual ~ModularReducer() {}
   protected:
      const BigInt modulus;
   };

/*************************************************
* Get a modular reducer                          *
*************************************************/
ModularReducer* get_reducer(const BigInt&, bool = false);

}

#endif
