/*************************************************
* Modular Reducer Source File                    *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/reducer.h>

namespace Botan {

/*************************************************
* Construct a ModularReducer                     *
*************************************************/
ModularReducer::ModularReducer(const BigInt& n) : modulus(n)
   {
   if(modulus <= 0)
      throw Invalid_Argument("ModularReducer: modulus must be positive");
   if(modulus.size() > 8 && !power_of_2(modulus.size()))
      modulus.grow_reg((1 << high_bit(modulus.size())) - modulus.size());
   }

/*************************************************
* Convert to the modular form                    *
*************************************************/
BigInt ModularReducer::convert_in(const BigInt& i) const
   {
   return i;
   }

/*************************************************
* Convert from the modular form                  *
*************************************************/
BigInt ModularReducer::convert_out(const BigInt& i) const
   {
   return i;
   }

/*************************************************
* Modular Multiplication                         *
*************************************************/
BigInt ModularReducer::multiply(const BigInt& x, const BigInt& y) const
   {
   return reduce(x * y);
   }

/*************************************************
* Modular Squaring                               *
*************************************************/
BigInt ModularReducer::square(const BigInt& x) const
   {
   return multiply(x, x);
   }

}
