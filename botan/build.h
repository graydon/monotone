/*************************************************
* Build Configuration Header File                *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_BUILD_CONFIG_H__
#define BOTAN_BUILD_CONFIG_H__

#define BOTAN_VERSION_MAJOR 1
#define BOTAN_VERSION_MINOR 7
#define BOTAN_VERSION_PATCH 9

#define BOTAN_MP_WORD_BITS 32
#define BOTAN_DEFAULT_BUFFER_SIZE 4096
#define BOTAN_MEM_POOL_CHUNK_SIZE 64*1024
#define BOTAN_PRIVATE_KEY_OP_BLINDING_BITS 64

#define BOTAN_KARAT_MUL_THRESHOLD 12
#define BOTAN_KARAT_SQR_THRESHOLD 12

#ifndef WIN32
#define BOTAN_EXT_ENTROPY_SRC_DEVICE
#else
#define BOTAN_EXT_ENTROPY_SRC_CAPI
#define BOTAN_EXT_ENTROPY_SRC_WIN32
#endif

#ifndef BOTAN_DLL
  #define BOTAN_DLL
#endif

#endif
