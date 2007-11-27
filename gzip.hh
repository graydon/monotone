/*************************************************
* Gzip Compressor Header File                    *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_EXT_GZIP_H__
#define BOTAN_EXT_GZIP_H__

#include <botan/filter.h>
#include <botan/pipe.h>

namespace Botan {

namespace GZIP {

   /* A basic header - we only need to set the IDs and compression method */
   const byte GZIP_HEADER[] = {
      0x1f, 0x8b, /* Magic ID bytes */
      0x08, /* Compression method of 'deflate' */
      0x00, /* Flags all empty */
      0x00, 0x00, 0x00, 0x00, /* MTIME */
      0x00, /* Extra flags */
      0xff, /* Operating system (unknown) */
   };

   const unsigned int HEADER_POS_OS = 9;

   const unsigned int FOOTER_LENGTH = 8;

}

/*************************************************
* Gzip Compression Filter                        *
*************************************************/
class Gzip_Compression : public Filter
   {
   public:
      void write(const byte input[], u32bit length);
      void start_msg();
      void end_msg();

      Gzip_Compression(u32bit = 1);
      ~Gzip_Compression();
   private:
      void clear();
      void put_header();
      void put_footer();
      const u32bit level;
      SecureVector<byte> buffer;
      class Zlib_Stream* zlib;
      Pipe pipe; /* A pipe for the crc32 processing */
      u32bit count;
   };

/*************************************************
* Gzip Decompression Filter                      *
*************************************************/
class Gzip_Decompression : public Filter
   {
   public:
      void write(const byte input[], u32bit length);
      void start_msg();
      void end_msg();

      Gzip_Decompression();
      ~Gzip_Decompression();
   private:
      u32bit eat_footer(const byte input[], u32bit length);
      void check_footer();
      void clear();
      SecureVector<byte> buffer;
      class Zlib_Stream* zlib;
      bool no_writes;
      u32bit pos; /* Current position in the message */
      Pipe pipe; /* A pipe for the crc32 processing */
      u32bit datacount; /* Amount of uncompressed output */
      SecureVector<byte> footer;
      bool in_footer;
   };

}

#endif
