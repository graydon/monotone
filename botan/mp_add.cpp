/*************************************************
* MPI Addition Source File                       *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/mp_core.h>
#include <botan/mem_ops.h>

namespace Botan {

extern "C" {

namespace {

/*************************************************
* Word Addition                                  *
*************************************************/
inline word word_add(word x, word y, word* carry)
   {
   word z = x + y;
   word c1 = (z < x);
   z += *carry;
   *carry = c1 | (z < *carry);
   return z;
   }

}

/*************************************************
* Two Operand Addition, No Carry                 *
*************************************************/
word bigint_add2_nc(word x[], u32bit x_size, const word y[], u32bit y_size)
   {
   word carry = 0;

   const u32bit blocks = y_size - (y_size % 4);

   for(u32bit j = 0; j != blocks; j += 4)
      {
      x[j  ] = word_add(x[j  ], y[j  ], &carry);
      x[j+1] = word_add(x[j+1], y[j+1], &carry);
      x[j+2] = word_add(x[j+2], y[j+2], &carry);
      x[j+3] = word_add(x[j+3], y[j+3], &carry);
      }

   for(u32bit j = blocks; j != y_size; ++j)
      x[j] = word_add(x[j], y[j], &carry);

   if(!carry)
      return 0;

   for(u32bit j = y_size; j != x_size; ++j)
      {
      x[j]++;
      if(x[j])
         return 0;
      }

   return 1;
   }

/*************************************************
* Three Operand Addition, No Carry               *
*************************************************/
word bigint_add3_nc(word z[], const word x[], u32bit x_size,
                              const word y[], u32bit y_size)
   {
   if(x_size < y_size)
      { return bigint_add3_nc(z, y, y_size, x, x_size); }

   word carry = 0;

   const u32bit blocks = y_size - (y_size % 4);

   for(u32bit j = 0; j != blocks; j += 4)
      {
      z[j  ] = word_add(x[j  ], y[j  ], &carry);
      z[j+1] = word_add(x[j+1], y[j+1], &carry);
      z[j+2] = word_add(x[j+2], y[j+2], &carry);
      z[j+3] = word_add(x[j+3], y[j+3], &carry);
      }

   for(u32bit j = blocks; j != y_size; ++j)
      z[j] = word_add(x[j], y[j], &carry);

   for(u32bit j = y_size; j != x_size; ++j)
      {
      word x_j = x[j] + carry;
      if(carry && x_j)
         carry = 0;
      z[j] = x_j;
      }

   return carry;
   }

/*************************************************
* Two Operand Addition                           *
*************************************************/
void bigint_add2(word x[], u32bit x_size, const word y[], u32bit y_size)
   {
   if(bigint_add2_nc(x, x_size, y, y_size))
      x[x_size]++;
   }

/*************************************************
* Three Operand Addition                         *
*************************************************/
void bigint_add3(word z[], const word x[], u32bit x_size,
                           const word y[], u32bit y_size)
   {
   if(bigint_add3_nc(z, x, x_size, y, y_size))
      z[x_size > y_size ? x_size : y_size]++;
   }

}

}
