/*************************************************
* MPI Subtraction Source File                    *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/mp_core.h>
#include <botan/mem_ops.h>

namespace Botan {

extern "C" {

namespace {

/*************************************************
* Word Subtraction                               *
*************************************************/
inline word word_sub(word x, word y, word* carry)
   {
   word t0 = x - y;
   word c1 = (t0 > x);
   word z = t0 - *carry;
   *carry = c1 | (z > t0);
   return z;
   }

}

/*************************************************
* Two Operand Subtraction                        *
*************************************************/
void bigint_sub2(word x[], u32bit x_size, const word y[], u32bit y_size)
   {
   word carry = 0;
   for(u32bit j = 0; j != y_size; ++j)
      x[j] = word_sub(x[j], y[j], &carry);

   if(!carry) return;

   for(u32bit j = y_size; j != x_size; ++j)
      {
      x[j]--;
      if(x[j] != MP_WORD_MAX) return;
      }
   }

/*************************************************
* Three Operand Subtraction                      *
*************************************************/
void bigint_sub3(word z[], const word x[], u32bit x_size,
                           const word y[], u32bit y_size)
   {
   word carry = 0;
   for(u32bit j = 0; j != y_size; ++j)
      {
      const word x_j = x[j], y_j = y[j];

      word c1 = 0, c2 = 0, z_j = 0, t0 = 0;

      t0 = x_j - y_j;
      c1 = (t0 > x_j);

      z_j = t0 - carry;
      c2 = (z_j > t0);

      carry = c1 | c2;
      z[j] = z_j;
      }

   for(u32bit j = y_size; j != x_size; ++j)
      {
      word x_j = x[j] - carry;
      if(carry && x_j != MP_WORD_MAX)
         carry = 0;
      z[j] = x_j;
      }
   }

}

}
