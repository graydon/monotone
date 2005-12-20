/*************************************************
* File EntropySource Source File                 *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/es_file.h>
#include <botan/conf.h>
#include <botan/parsing.h>
#include <fstream>

namespace Botan {

/*************************************************
* File_EntropySource Constructor                 *
*************************************************/
File_EntropySource::File_EntropySource(const std::string& sources)
   {
   std::vector<std::string> source_list = split_on(sources, ':');
   std::vector<std::string> defaults = Config::get_list("rng/es_files");

   for(u32bit j = 0; j != source_list.size(); j++)
      add_source(source_list[j]);
   for(u32bit j = 0; j != defaults.size(); j++)
      add_source(defaults[j]);
   }

/*************************************************
* Add another file to the list                   *
*************************************************/
void File_EntropySource::add_source(const std::string& source)
   {
   sources.push_back(source);
   }

/*************************************************
* Gather Entropy from Randomness Source          *
*************************************************/
u32bit File_EntropySource::slow_poll(byte output[], u32bit length)
   {
   u32bit read = 0;
   for(u32bit j = 0; j != sources.size(); j++)
      {
      std::ifstream random_source(sources[j].c_str(), std::ios::binary);
      if(!random_source) continue;
      random_source.read((char*)output + read, length);
      read += random_source.gcount();
      length -= random_source.gcount();
      if(length == 0)
         break;
      }
   return read;
   }

}
