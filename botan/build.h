/*************************************************
* Build Configuration Header File                *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_BUILD_CONFIG_H__
#define BOTAN_BUILD_CONFIG_H__

#define BOTAN_VERSION_MAJOR 1
#define BOTAN_VERSION_MINOR 7
#define BOTAN_VERSION_PATCH 4

#define BOTAN_MP_WORD_BITS 32
#define BOTAN_DEFAULT_BUFFER_SIZE 4096

#define BOTAN_KARAT_MUL_THRESHOLD 12
#define BOTAN_KARAT_SQR_THRESHOLD 12

#ifndef WIN32
#define BOTAN_EXT_ENTROPY_SRC_DEVICE
#else
#define BOTAN_EXT_ENTROPY_SRC_CAPI
#define BOTAN_EXT_ENTROPY_SRC_WIN32
#endif

#endif
