/*************************************************
* Barrett Reducer Source File                    *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/barrett.h>
#include <botan/numthry.h>
#include <botan/mp_core.h>

namespace Botan {

/*************************************************
* Precompute values                              *
*************************************************/
BarrettReducer::BarrettReducer(const BigInt& mod) : ModularReducer(mod)
   {
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
BigInt BarrettReducer::reduce(const BigInt& x) const
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

}
