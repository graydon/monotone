/*************************************************
* Modular Exponentiation Header File             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_MODULAR_EXP_H__
#define BOTAN_MODULAR_EXP_H__

#include <botan/bigint.h>
#include <botan/reducer.h>
#include <vector>

namespace Botan {

/*************************************************
* Fixed Exponent Exponentiation                  *
*************************************************/
class FixedExponent_Exp
   {
   public:
      BigInt operator() (const BigInt& n) const { return power_mod(n); }
      BigInt reduce(const BigInt&) const;
      BigInt power_mod(const BigInt&) const;

      const BigInt& get_exponent() const;
      const BigInt& get_modulus() const;

      bool initialized() const { return (reducer != 0); }

      FixedExponent_Exp& operator=(const FixedExponent_Exp&);

      FixedExponent_Exp() { reducer = 0; }
      FixedExponent_Exp(const BigInt&, const BigInt&);
      FixedExponent_Exp(const FixedExponent_Exp&);
      ~FixedExponent_Exp() { delete reducer; }
   private:
      void init_check() const;
      ModularReducer* reducer;
      BigInt exponent;
   };

/*************************************************
* Fixed Base Exponentiation                      *
*************************************************/
class FixedBase_Exp
   {
   public:
      BigInt operator() (const BigInt& n) const { return power_mod(n); }
      BigInt reduce(const BigInt& n) const;
      BigInt power_mod(const BigInt&) const;

      const BigInt& get_base() const;
      const BigInt& get_modulus() const;

      bool initialized() const { return (reducer != 0); }

      FixedBase_Exp& operator=(const FixedBase_Exp&);

      FixedBase_Exp() { reducer = 0; }
      FixedBase_Exp(const BigInt&, const BigInt&);
      FixedBase_Exp(const FixedBase_Exp&);
      ~FixedBase_Exp() { delete reducer; }
   private:
      void init_check() const;
      ModularReducer* reducer;
      std::vector<BigInt> g;
   };

}

#endif
