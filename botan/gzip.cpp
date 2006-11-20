/*************************************************
* Gzip Compressor Source File                    *
* (C) 1999-2004 The Botan Project                *
*                                                *
* Based on the comp_zlib module, modified        *
* by Matt Johnston. This is not a complete       *
* gzip implementation (it only handles basic     *
* headers).                                      *
*************************************************/

/* This could be implemented a lot more cleanly if we rely on zlib >= 1.2
 * being available. the Gzip Compressor would just be a subclass of
 * Zlib Compressor, with window_bits+=16 for deflateInit2(), etc */

#include <botan/gzip.h>
#include <botan/filters.h>
#include <botan/bit_ops.h>
#include <cstring>
#include <map>
#include <zlib.h>

namespace Botan {

namespace {

/*************************************************
* Allocation Information for Zlib                *
*************************************************/
class Zlib_Alloc_Info
   {
   public:
      std::map<void*, u32bit> current_allocs;
      Allocator* alloc;

      Zlib_Alloc_Info() { alloc = Allocator::get(false); }
   };

/*************************************************
* Allocation Function for Zlib                   *
*************************************************/
void* zlib_malloc(void* info_ptr, unsigned int n, unsigned int size)
   {
   Zlib_Alloc_Info* info = static_cast<Zlib_Alloc_Info*>(info_ptr);
   void* ptr = info->alloc->allocate(n * size);
   info->current_allocs[ptr] = n * size;
   return ptr;
   }

/*************************************************
* Allocation Function for Zlib                   *
*************************************************/
void zlib_free(void* info_ptr, void* ptr)
   {
   Zlib_Alloc_Info* info = static_cast<Zlib_Alloc_Info*>(info_ptr);
   std::map<void*, u32bit>::const_iterator i = info->current_allocs.find(ptr);
   if(i == info->current_allocs.end())
      throw Invalid_Argument("zlib_free: Got pointer not allocated by us");
   info->alloc->deallocate(ptr, i->second);
   }
}

/*************************************************
* Wrapper Type for Zlib z_stream                 *
*************************************************/
class Zlib_Stream
   {
   public:
      z_stream stream;

      Zlib_Stream()
         {
         std::memset(&stream, 0, sizeof(z_stream));
         stream.zalloc = zlib_malloc;
         stream.zfree = zlib_free;
         stream.opaque = new Zlib_Alloc_Info;
         }
      ~Zlib_Stream()
         {
         Zlib_Alloc_Info* info = static_cast<Zlib_Alloc_Info*>(stream.opaque);
         delete info;
         std::memset(&stream, 0, sizeof(z_stream));
         }
   };

/*************************************************
* Gzip_Compression Constructor                   *
*************************************************/
Gzip_Compression::Gzip_Compression(u32bit l) :
   level((l >= 9) ? 9 : l), buffer(DEFAULT_BUFFERSIZE),
   pipe(new Hash_Filter("CRC32")), count( 0 )
   {

   zlib = new Zlib_Stream;
   // window_bits == -15 relies on an undocumented feature of zlib, which
   // supresses the zlib header on the message. We need that since gzip doesn't
   // use this header.  The feature been confirmed to exist in 1.1.4, which
   // everyone should be using due to security fixes. In later versions this
   // feature is documented, along with the ability to do proper gzip output
   // (that would be a nicer way to do things, but will have to wait until 1.2
   // becomes more widespread).
   // The other settings are the defaults that deflateInit() gives
   if(deflateInit2(&(zlib->stream), level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
      {
      delete zlib; zlib = 0;
      throw Exception("Gzip_Compression: Memory allocation error");
      }
   }

/*************************************************
* Gzip_Compression Destructor                    *
*************************************************/
Gzip_Compression::~Gzip_Compression()
   {
   deflateEnd(&(zlib->stream));
   delete zlib; zlib = 0;
   }

/*************************************************
* Start Compressing with Gzip                    *
*************************************************/
void Gzip_Compression::start_msg()
   {
   clear();
   put_header();
   pipe.start_msg();
   count = 0;
   }

/*************************************************
* Compress Input with Gzip                       *
*************************************************/
void Gzip_Compression::write(const byte input[], u32bit length)
   {

   count += length;
   pipe.write(input, length);

   zlib->stream.next_in = (Bytef*)input;
   zlib->stream.avail_in = length;

   while(zlib->stream.avail_in != 0)
      {
      zlib->stream.next_out = (Bytef*)buffer.begin();
      zlib->stream.avail_out = buffer.size();
      int rc = deflate(&(zlib->stream), Z_NO_FLUSH);
      if (rc != Z_OK && rc != Z_STREAM_END)
         throw Exception("Internal error in Gzip_Compression deflate.");
      send(buffer.begin(), buffer.size() - zlib->stream.avail_out);
      }
   }

/*************************************************
* Finish Compressing with Gzip                   *
*************************************************/
void Gzip_Compression::end_msg()
   {
   zlib->stream.next_in = 0;
   zlib->stream.avail_in = 0;

   int rc = Z_OK;
   while(rc != Z_STREAM_END)
      {
      zlib->stream.next_out = (Bytef*)buffer.begin();
      zlib->stream.avail_out = buffer.size();
      rc = deflate(&(zlib->stream), Z_FINISH);
      if (rc != Z_OK && rc != Z_STREAM_END)
         throw Exception("Internal error in Gzip_Compression finishing deflate.");
      send(buffer.begin(), buffer.size() - zlib->stream.avail_out);
      }

   pipe.end_msg();
   put_footer();
   clear();
   }

/*************************************************
* Clean up Compression Context                   *
*************************************************/
void Gzip_Compression::clear()
   {
   deflateReset(&(zlib->stream));
   }

/*************************************************
* Put a basic gzip header at the beginning       *
*************************************************/
void Gzip_Compression::put_header()
   {
   send(GZIP::GZIP_HEADER, sizeof(GZIP::GZIP_HEADER));
   }

/*************************************************
* Put a gzip footer at the end                   *
*************************************************/
void Gzip_Compression::put_footer()
   {
   // 4 byte CRC32, and 4 byte length field
   SecureVector<byte> buf(4);
   SecureVector<byte> tmpbuf(4);

   pipe.read(tmpbuf.begin(), tmpbuf.size());

   // CRC32 is the reverse order to what gzip expects.
   for (int i = 0; i < 4; i++)
      buf[3-i] = tmpbuf[i];

   send(buf.begin(), buf.size());

   // Length - LSB first
   for (int i = 0; i < 4; i++)
      buf[3-i] = get_byte(i, count);

   send(buf.begin(), buf.size());
   }

/*************************************************
* Gzip_Decompression Constructor                 *
*************************************************/
Gzip_Decompression::Gzip_Decompression() : buffer(DEFAULT_BUFFERSIZE),
   no_writes(true), pipe(new Hash_Filter("CRC32")), footer(0)
   {
   if (DEFAULT_BUFFERSIZE < sizeof(GZIP::GZIP_HEADER))
      throw Exception("DEFAULT_BUFFERSIZE is too small");

   zlib = new Zlib_Stream;

   // window_bits == -15 is raw zlib (no header) - see comment
   // above about deflateInit2
   if(inflateInit2(&(zlib->stream), -15) != Z_OK)
      {
      delete zlib; zlib = 0;
      throw Exception("Gzip_Decompression: Memory allocation error");
      }
   }

/*************************************************
* Gzip_Decompression Destructor                  *
*************************************************/
Gzip_Decompression::~Gzip_Decompression()
   {
      inflateEnd(&(zlib->stream));
      delete zlib; zlib = 0;
   }

/*************************************************
* Start Decompressing with Gzip                  *
*************************************************/
void Gzip_Decompression::start_msg()
   {
   if (!no_writes)
      throw Exception("Gzip_Decompression: start_msg after already writing");

   pipe.start_msg();
   datacount = 0;
   pos = 0;
   in_footer = false;
   }

/*************************************************
* Decompress Input with Gzip                     *
*************************************************/
void Gzip_Decompression::write(const byte input[], u32bit length)
   {
   if(length) no_writes = false;

   // If we're in the footer, take what we need, then go to the next block
   if (in_footer)
      {
         u32bit eat_len = eat_footer(input, length);
         input += eat_len;
         length -= eat_len;
         if (length == 0)
            return;
      }

   // Check the gzip header
   if (pos < sizeof(GZIP::GZIP_HEADER))
      {
      u32bit len = std::min((u32bit)sizeof(GZIP::GZIP_HEADER)-pos, length);
      u32bit cmplen = len;
      // The last byte is the OS flag - we don't care about that
      if (pos + len - 1 >= GZIP::HEADER_POS_OS)
         cmplen--;

      if (std::memcmp(input, &GZIP::GZIP_HEADER[pos], cmplen) != 0)
         {
         throw Decoding_Error("Gzip_Decompression: Data integrity error in header");
         }
      input += len;
      length -= len;
      pos += len;
      }

   pos += length;

   zlib->stream.next_in = (Bytef*)input;
   zlib->stream.avail_in = length;

   while(zlib->stream.avail_in != 0)
      {
      zlib->stream.next_out = (Bytef*)buffer.begin();
      zlib->stream.avail_out = buffer.size();

      int rc = inflate(&(zlib->stream), Z_SYNC_FLUSH);
      if(rc != Z_OK && rc != Z_STREAM_END)
         {
         if(rc == Z_DATA_ERROR)
            throw Decoding_Error("Gzip_Decompression: Data integrity error");
         if(rc == Z_NEED_DICT)
            throw Decoding_Error("Gzip_Decompression: Need preset dictionary");
         if(rc == Z_MEM_ERROR)
            throw Exception("Gzip_Decompression: Memory allocation error");
         throw Exception("Gzip_Decompression: Unknown decompress error");
         }
      send(buffer.begin(), buffer.size() - zlib->stream.avail_out);
      pipe.write(buffer.begin(), buffer.size() - zlib->stream.avail_out);
      datacount += buffer.size() - zlib->stream.avail_out;

      // Reached the end - we now need to check the footer
      if(rc == Z_STREAM_END)
         {
         u32bit read_from_block = length - zlib->stream.avail_in;
         u32bit eat_len = eat_footer((Bytef*)input + read_from_block, zlib->stream.avail_in);
         read_from_block += eat_len;
         input += read_from_block;
         length -= read_from_block;
         zlib->stream.next_in = (Bytef*)input;
         zlib->stream.avail_in = length;
         }
      }
   }

/*************************************************
* Store the footer bytes                         *
*************************************************/
u32bit Gzip_Decompression::eat_footer(const byte input[], u32bit length)
   {
      if (footer.size() >= GZIP::FOOTER_LENGTH)
         throw Decoding_Error("Gzip_Decompression: Data integrity error in footer");

      u32bit eat_len = std::min(GZIP::FOOTER_LENGTH-footer.size(), length);
      footer.append(input, eat_len);

      if (footer.size() == GZIP::FOOTER_LENGTH)
         {
         check_footer();
         clear();
         }

         return eat_len;
   }

/*************************************************
* Check the gzip footer                          *
*************************************************/
void Gzip_Decompression::check_footer()
   {
   if (footer.size() != GZIP::FOOTER_LENGTH)
      throw Exception("Gzip_Decompression: Error finalizing decompression");

   pipe.end_msg();
   
   // 4 byte CRC32, and 4 byte length field
   SecureVector<byte> buf(4);
   SecureVector<byte> tmpbuf(4);
   pipe.read(tmpbuf.begin(), tmpbuf.size());

  // CRC32 is the reverse order to what gzip expects.
  for (int i = 0; i < 4; i++)
     buf[3-i] = tmpbuf[i];

  tmpbuf.set(footer.begin(), 4);
  if (buf != tmpbuf)
      throw Exception("Gzip_Decompression: Data integrity error - CRC32 error");

   // Check the length matches - it is encoded LSB-first
   for (int i = 0; i < 4; i++)
      {
      if (footer.begin()[GZIP::FOOTER_LENGTH-1-i] != get_byte(i, datacount))
         throw Exception("Gzip_Decompression: Data integrity error - incorrect length");
      }

   }

/*************************************************
* Finish Decompressing with Gzip                 *
*************************************************/
void Gzip_Decompression::end_msg()
   {

   // All messages should end with a footer, and when a footer is successfully
   // read, clear() will reset no_writes
   if(no_writes) return;

   throw Exception("Gzip_Decompression: didn't find footer");

   }

/*************************************************
* Clean up Decompression Context                 *
*************************************************/
void Gzip_Decompression::clear()
   {
   no_writes = true;
   inflateReset(&(zlib->stream));

   footer.clear();
   pos = 0;
   datacount = 0;
   }

}
