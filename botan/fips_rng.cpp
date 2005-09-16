/*************************************************
* FIPS 186-2 RNG Source File                     *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/fips_rng.h>
#include <botan/randpool.h>

namespace Botan {

/*************************************************
* Generate a buffer of random bytes              *
*************************************************/
void FIPS_186_RNG::randomize(byte out[], u32bit length) throw(PRNG_Unseeded)
   {
   if(!is_seeded())
      throw PRNG_Unseeded(name());

   xkey = gen_xval();
   while(length)
      {
      const u32bit copied = std::min(length, buffer.size() - position);
      copy_mem(out, buffer + position, copied);
      out += copied;
      length -= copied;
      update_buffer();
      }
   }

/*************************************************
* Compute the next buffer                        *
*************************************************/
void FIPS_186_RNG::update_buffer() throw()
   {
   SecureVector<byte> xval = gen_xval();
   do_add(xval, xkey);

   buffer = do_hash(xval);

   for(u32bit j = xkey.size(); j > 0; j--)
      if(++xkey[j-1])
         break;

   do_add(xkey, buffer);
   }

/*************************************************
* Add entropy to internal state                  *
*************************************************/
void FIPS_186_RNG::add_randomness(const byte data[], u32bit length) throw()
   {
   randpool->add_entropy(data, length);
   if(is_seeded())
      xkey = gen_xval();
   }

/*************************************************
* Check if the the PRNG is seeded                *
*************************************************/
bool FIPS_186_RNG::is_seeded() const
   {
   return randpool->is_seeded();
   }

/*************************************************
* Add x to y, modulo 2**160                      *
*************************************************/
void FIPS_186_RNG::do_add(MemoryRegion<byte>& x, const MemoryRegion<byte>& y)
   {
   if(x.size() != y.size())
      throw Invalid_Argument("FIPS_186_RNG::do_add: x and y are unequal size");

   byte carry = 0;
   for(u32bit j = x.size(); j > 0; j--)
      {
      u16bit sum = (u16bit)x[j-1] + y[j-1] + carry;
      carry = get_byte(0, sum);
      x[j-1] = get_byte(1, sum);
      }
   }

/*************************************************
* Generate the XKEY/XSEED parameter              *
*************************************************/
SecureVector<byte> FIPS_186_RNG::gen_xval()
   {
   SecureVector<byte> xval(20);
   randpool->randomize(xval, xval.size());
   return xval;
   }

/*************************************************
* Calculate the FIPS-186 G function              *
*************************************************/
SecureVector<byte> FIPS_186_RNG::do_hash(const MemoryRegion<byte>& xval)
   {
   SecureVector<byte> M(64), output(20);
   M.copy(xval, xval.size());

   sha1.clear();
   sha1.hash(M);
   for(u32bit j = 0; j != 20; j++)
      output[j] = get_byte(j % 4, sha1.digest[j/4]);
   sha1.clear();

   return output;
   }

/*************************************************
* Clear memory of sensitive data                 *
*************************************************/
void FIPS_186_RNG::clear() throw()
   {
   sha1.clear();
   xkey.clear();
   buffer.clear();
   entropy = position = 0;
   }

/*************************************************
* Return the name of this type                   *
*************************************************/
std::string FIPS_186_RNG::name() const
   {
   return "FIPS-186";
   }

/*************************************************
* FIPS 186-2 RNG Constructor                     *
*************************************************/
FIPS_186_RNG::FIPS_186_RNG()
   {
   xkey.create(sha1.OUTPUT_LENGTH);
   buffer.create(sha1.OUTPUT_LENGTH);
   randpool = new Randpool;
   entropy = position = 0;
   }

/*************************************************
* FIPS 186-2 RNG Destructor                      *
*************************************************/
FIPS_186_RNG::~FIPS_186_RNG()
   {
   delete randpool;
   }

}
