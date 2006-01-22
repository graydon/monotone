/*************************************************
* Modular Reducer Source File                    *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/reducer.h>
#include <botan/bit_ops.h>
#include <botan/numthry.h>
#include <botan/mp_core.h>

namespace Botan {

namespace {

/*************************************************
* Barrett Reducer                                *
*************************************************/
class Barrett_Reducer : public ModularReducer
   {
   public:
      BigInt multiply(const BigInt&, const BigInt&) const;
      BigInt square(const BigInt& x) const
         { return reduce(Botan::square(x)); }

      BigInt reduce(const BigInt&) const;
      const BigInt& get_modulus() const { return modulus; }

      Barrett_Reducer(const BigInt&);
   private:
      BigInt modulus, mu;
      mutable BigInt t1, t2;
      u32bit max_bits, k;
   };

/*************************************************
* Barrett_Reducer Constructor                    *
*************************************************/
Barrett_Reducer::Barrett_Reducer(const BigInt& mod) : modulus(mod)
   {
   if(modulus <= 0)
      throw Invalid_Argument("Barrett_Reducer: modulus must be positive");

   if(modulus.size() > 8 && !power_of_2(modulus.size()))
      modulus.grow_reg((1 << high_bit(modulus.size())) - modulus.size());

   k = modulus.sig_words();
   mu.set_bit(MP_WORD_BITS * 2 * k);
   mu /= modulus;
   max_bits = MP_WORD_BITS * 2 * k;

   if(mu.size() > 8 && !power_of_2(mu.size()))
      mu.grow_reg((1 << high_bit(mu.size())) - mu.size());
   }

/*************************************************
* Barrett Reduction                              *
*************************************************/
BigInt Barrett_Reducer::reduce(const BigInt& x) const
   {
   if(x.is_positive() && x < modulus)
      return x;
   if(x.bits() > max_bits)
      return (x % modulus);

   t1 = x;
   t1.set_sign(BigInt::Positive);

   t1 >>= (MP_WORD_BITS * (k - 1));
   t1 *= mu;
   t1 >>= (MP_WORD_BITS * (k + 1));

   t1 *= modulus;
   t1.mask_bits(MP_WORD_BITS * (k+1));

   t2 = x;
   t2.set_sign(BigInt::Positive);
   t2.mask_bits(MP_WORD_BITS * (k+1));

   t2 -= t1;

   if(t2.is_negative())
      {
      BigInt b_to_k1(BigInt::Power2, MP_WORD_BITS * (k+1));
      t2 += b_to_k1;
      }
   while(t2 >= modulus)
      t2 -= modulus;

   if(x.is_negative() && t2.is_nonzero())
      t2 = modulus - t2;

   return t2;
   }

/*************************************************
* Multiply, followed by a reduction              *
*************************************************/
BigInt Barrett_Reducer::multiply(const BigInt& x, const BigInt& y) const
   {
   return reduce(x * y);
   }

}

/*************************************************
* Acquire a modular reducer                      *
*************************************************/
ModularReducer* get_reducer(const BigInt& n)
   {
   return new Barrett_Reducer(n);
   }
}
