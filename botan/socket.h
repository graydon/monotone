/*************************************************
* Socket Interface Header File                   *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_SOCKET_H__
#define BOTAN_SOCKET_H__

#include <botan/types.h>

namespace Botan {

/*************************************************
* Socket Base Class                              *
*************************************************/
class Socket
   {
   public:
      virtual u32bit read(byte[], u32bit) = 0;
      virtual void write(const byte[], u32bit) = 0;

      virtual bool can_read() = 0;

      virtual void close() = 0;

      virtual ~Socket() {}
   };

}

#endif
