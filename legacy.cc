// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "legacy.hh"
#include "basic_io.hh"
#include "constants.hh"
#include "database.hh"

using std::make_pair;
using std::string;

namespace legacy
{
  namespace
  {
    namespace syms
    {
      symbol const file("file");
    }
  };

  // cf. work.cc:read_attr_map in the pre-roster code.
  void
  read_dot_mt_attrs(data const & dat, dot_mt_attrs_map & attr)
  {
    basic_io::input_source src(dat(), ".mt-attrs");
    basic_io::tokenizer tok(src);
    basic_io::parser parser(tok);

    string file, name, value;

    attr.clear();

    while (parser.symp(syms::file))
      {
        parser.sym();
        parser.str(file);
        file_path fp = file_path_internal(file);

        while (parser.symp() &&
               !parser.symp(syms::file))
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
      symbol const new_manifest("new_manifest");
      symbol const old_revision("old_revision");
      symbol const old_manifest("old_manifest");
      symbol const patch("patch");
      symbol const from("from");
      symbol const to("to");
      symbol const add_file("add_file");
      symbol const delete_file("delete_file");
      symbol const delete_dir("delete_dir");
      symbol const rename_file("rename_file");
      symbol const rename_dir("rename_dir");
    }
  }

  // cf. revision.cc:parse_edge and change_set.cc:parse_change_set and
  // change_set.cc:parse_path_rearrangement in the pre-roster code.
  static void
  extract_renames(basic_io::parser & parser, renames_map & renames)
  {
    revision_id old_rev;
    string tmp;
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
            string from_str, to_str;
            parser.sym();
            parser.str(from_str);
            parser.esym(syms::to);
            parser.str(to_str);
            renames[old_rev][file_path_internal(to_str)]
              = file_path_internal(from_str);
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
  get_manifest_and_renames_for_rev(database & db,
                                   revision_id const & ident,
                                   manifest_id & mid,
                                   renames_map & renames)
  {
    revision_data dat;
    db.get_revision(ident, dat);
    basic_io::input_source src(dat.inner()(), "revision");
    basic_io::tokenizer tok(src);
    basic_io::parser pars(tok);

    pars.esym(syms::new_manifest);
    string tmp;
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
    string::size_type pos = 0;
    while (pos != dat().size())
      {
        // whenever we get here, pos points to the beginning of a manifest
        // line
        // manifest file has 40 characters hash, then 2 characters space, then
        // everything until next \n is filename.
        string ident = dat().substr(pos, constants::idlen);
        string::size_type file_name_begin = pos + constants::idlen + 2;
        pos = dat().find('\n', file_name_begin);
        string file_name;
        if (pos == string::npos)
          file_name = dat().substr(file_name_begin);
        else
          file_name = dat().substr(file_name_begin, pos - file_name_begin);
        man.insert(make_pair(file_path_internal(file_name),
                             hexenc<id>(ident)));
        // skip past the '\n'
        ++pos;
      }
    return;
  }

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
