/*************************************************
* Division Algorithm Source File                 *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/numthry.h>
#include <botan/mp_core.h>

namespace Botan {

namespace {

/*************************************************
* Handle signed operands, if necessary           *
*************************************************/
void sign_fixup(const BigInt& x, const BigInt& y, BigInt& q, BigInt& r)
   {
   if(x.sign() == BigInt::Negative)
      {
      q.flip_sign();
      if(r.is_nonzero()) { --q; r = y.abs() - r; }
      }
   if(y.sign() == BigInt::Negative)
      q.flip_sign();
   }

}

/*************************************************
* Solve x = q * y + r                            *
*************************************************/
void divide(const BigInt& x, const BigInt& y_arg, BigInt& q, BigInt& r)
   {
   if(y_arg.is_zero())
      throw BigInt::DivideByZero();

   BigInt y = y_arg;
   r = x;

   r.set_sign(BigInt::Positive);
   y.set_sign(BigInt::Positive);

   s32bit compare = r.cmp(y);
   if(compare == -1) { q = 0; sign_fixup(x, y_arg, q, r); return; }
   if(compare ==  0) { q = 1; r = 0; sign_fixup(x, y_arg, q, r); return; }

   u32bit shifts = 0;
   while(y[y.sig_words()-1] < MP_WORD_TOP_BIT)
      { r <<= 1; y <<= 1; ++shifts; }

   u32bit n = r.sig_words() - 1, t = y.sig_words() - 1;
   q.get_reg().create(n - t + 1);
   if(n <= t)
      {
      while(r > y) { r -= y; q++; }
      r >>= shifts;
      sign_fixup(x, y_arg, q, r);
      return;
      }

   BigInt temp = y << (MP_WORD_BITS * (n-t));

   while(r >= temp) { r -= temp; ++q[n-t]; }

   for(u32bit j = n; j != t; --j)
      {
      const word x_j0  = r.word_at(j);
      const word x_j1 = r.word_at(j-1);
      const word y_t  = y.word_at(t);

      if(x_j0 == y_t)
         q[j-t-1] = MP_WORD_MAX;
      else
         q[j-t-1] = bigint_divop(x_j0, x_j1, y_t);

      while(bigint_divcore(q[j-t-1], y_t, y.word_at(t-1),
                           x_j0, x_j1, r.word_at(j-2)))
         --q[j-t-1];

      r -= (q[j-t-1] * y) << (MP_WORD_BITS * (j-t-1));
      if(r.is_negative())
         {
         r += y << (MP_WORD_BITS * (j-t-1));
         --q[j-t-1];
         }
      }
   r >>= shifts;

   sign_fixup(x, y_arg, q, r);
   }

}
