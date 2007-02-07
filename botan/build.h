/*************************************************
* Build Config Header File                       *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_BUILD_CONFIG_H__
#define BOTAN_BUILD_CONFIG_H__

#define BOTAN_VERSION_MAJOR 1
#define BOTAN_VERSION_MINOR 5
#define BOTAN_VERSION_PATCH 10

#define BOTAN_MP_WORD_BITS 32
#define BOTAN_DEFAULT_BUFFER_SIZE 4096

#define BOTAN_KARAT_MUL_THRESHOLD 12
#define BOTAN_KARAT_SQR_THRESHOLD 12

#define BOTAN_GZIP_OS_CODE 255

#if defined(_MSC_VER)
   #pragma warning(disable: 4250 4290)
#define BOTAN_EXT_ENTROPY_SRC_WIN32
#define BOTAN_EXT_ENTROPY_SRC_CAPI
#endif

#define BOTAN_EXT_COMPRESSOR_GZIP
#define BOTAN_EXT_COMPRESSOR_ZLIB

#endif
