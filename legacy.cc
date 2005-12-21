// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "legacy.hh"
#include "basic_io.hh"

namespace legacy
{
  void
  read_dot_mt_attrs(data const & dat, dot_mt_attrs_map & attr)
  {
    basic_io::input_source src(dat(), ".mt-attrs");
    basic_io::tokenizer tok(src);
    basic_io::parser parser(tok);
    
    std::string file, name, value;
    
    attr.clear();

    while (parser.symp("file"))
      {
        parser.sym();
        parser.str(file);
        file_path fp = file_path_internal(file);
        
        while (parser.symp() && 
               !parser.symp("file"))
          {
            parser.sym(name);
            parser.str(value);
            attr[fp][name] = value;
          }
      }
  }
}
