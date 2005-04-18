/*************************************************
* Configuration Header File                      *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_CONFIG_H__
#define BOTAN_CONFIG_H__

#define BOTAN_MP_WORD_BITS 32
#define BOTAN_DEFAULT_BUFFER_SIZE 4096
#define BOTAN_VECTOR_OVER_ALLOCATE 4
#define BOTAN_GZIP_OS_CODE 255

#if defined(_MSC_VER)
   #pragma warning(disable: 4250 4290)
#endif

#define BOTAN_VERSION_MAJOR 1
#define BOTAN_VERSION_MINOR 4
#define BOTAN_VERSION_PATCH 3

#define BOTAN_EXT_COMPRESSOR_GZIP
#define BOTAN_EXT_COMPRESSOR_ZLIB

#endif
