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
  // cf. work.cc:read_attr_map in the pre-roster code.
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
      std::string const new_manifest("new_manifest");
      std::string const old_revision("old_revision");
      std::string const old_manifest("old_manifest");
      std::string const patch("patch");
      std::string const from("from");
      std::string const to("to");
      std::string const add_file("add_file");
      std::string const delete_file("delete_file");
      std::string const delete_dir("delete_dir");
      std::string const rename_file("rename_file");
      std::string const rename_dir("rename_dir");
    }
  }
  
  // cf. revision.cc:parse_edge and change_set.cc:parse_change_set and
  // change_set.cc:parse_path_rearrangement in the pre-roster code.
  static void
  extract_renames(basic_io::parser & parser, renames_map & renames)
  {
    revision_id old_rev;
    std::string tmp;
    parser.esym(syms::old_revision);
    parser.hex(tmp);
    old_rev = revision_id(tmp);
    parser.esym(syms::old_manifest);
    parser.hex();

    while (parser.symp())
      {
        // things that take a single string argument
        if (parser.symp(syms::add_file)
            || parser.symp(syms::delete_file)
            || parser.symp(syms::delete_dir))
          {
            parser.sym();
            parser.str();
          }
        else if (parser.symp(syms::rename_file)
                 || parser.symp(syms::rename_dir))
          {
            std::string from_str, to_str;
            parser.sym();
            parser.str(from_str);
            parser.esym(syms::to);
            parser.str(to_str);
            split_path from, to;
            file_path_internal(from_str).split(from);
            file_path_internal(to_str).split(to);
            renames[old_rev][to] = from;
          }
        else if (parser.symp(syms::patch))
          {
            parser.sym();
            parser.str();
            parser.esym(syms::from);
            parser.hex();
            parser.esym(syms::to);
            parser.hex();
          }
        else
          break;
      }
  }

  // cf. revision.cc:parse_revision in the pre-roster code.
  void 
  get_manifest_and_renames_for_rev(app_state & app,
                                   revision_id const & ident,
                                   manifest_id & mid,
                                   renames_map & renames)
  {
    revision_data dat;
    app.db.get_revision(ident, dat);
    basic_io::input_source src(dat.inner()(), "revision");
    basic_io::tokenizer tok(src);
    basic_io::parser pars(tok);

    pars.esym(syms::new_manifest);
    std::string tmp;
    pars.hex(tmp);
    mid = manifest_id(tmp);
    while (pars.symp(syms::old_revision))
      extract_renames(pars, renames);
  }

  // cf. manifest.cc:read_manifest_map in the pre-roster code.
  void 
  read_manifest_map(manifest_data const & mdat,
                    manifest_map & man)
  {
    data const & dat = mdat.inner();
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
  
}
