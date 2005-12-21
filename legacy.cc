// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "legacy.hh"
#include "basic_io.hh"
#include "app_state.hh"
#include "constants.hh"

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

  namespace 
  {
    namespace syms
    {
      std::string const old_revision("old_revision");
      std::string const new_manifest("new_manifest");
    }
  }
  
  void 
  get_manifest_for_rev(app_state & app,
                       revision_id const & ident,
                       manifest_id & mid)
  {
    revision_data dat;
    app.db.get_revision(ident,dat);
    basic_io::input_source src(dat.inner()(), "revision");
    basic_io::tokenizer tok(src);
    basic_io::parser pars(tok);
    while (pars.symp())
      {
        if (pars.symp(syms::new_manifest))
          {
            std::string tmp;
            pars.sym();
            pars.hex(tmp);
            mid = manifest_id(tmp);
            return;
          }
        else
          pars.sym();
      }
    I(false);
  }


  void 
  read_manifest_map(data const & dat,
                    manifest_map & man)
  {
    std::string::size_type pos = 0;
    while (pos != dat().size())
      {
        // whenever we get here, pos points to the beginning of a manifest
        // line
        // manifest file has 40 characters hash, then 2 characters space, then
        // everything until next \n is filename.
        std::string ident = dat().substr(pos, constants::idlen);
        std::string::size_type file_name_begin = pos + constants::idlen + 2;
        pos = dat().find('\n', file_name_begin);
        std::string file_name;
        if (pos == std::string::npos)
          file_name = dat().substr(file_name_begin);
        else
          file_name = dat().substr(file_name_begin, pos - file_name_begin);
        man.insert(std::make_pair(file_path_internal(file_name),
                                  hexenc<id>(ident)));
        // skip past the '\n'
        ++pos;
      }
    return;
  }
  
  void 
  read_manifest_map(manifest_data const & dat,
                    manifest_map & man)
  {  
    read_manifest_map(dat.inner(), man);
  }
  
}
