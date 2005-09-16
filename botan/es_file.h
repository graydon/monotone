/*************************************************
* File EntropySource Header File                 *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_ENTROPY_SRC_FILE_H__
#define BOTAN_ENTROPY_SRC_FILE_H__

#include <botan/base.h>
#include <string>
#include <vector>

namespace Botan {

/*************************************************
* File Based Entropy Source                      *
*************************************************/
class File_EntropySource : public EntropySource
   {
   public:
      u32bit slow_poll(byte[], u32bit);
      void add_source(const std::string&);
      File_EntropySource(const std::string& = "");
   private:
      std::vector<std::string> sources;
   };

}

#endif
