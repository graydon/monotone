#include "lua_hooks.hh"
#include "file_io.hh"
#include "localized_file_io.hh"

#include "botan/botan.h"

#include "transforms.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "paths.hh"
#include "platform-wrapped.hh"

using std::string;

bool
ident_existing_file(file_path const & p, file_id & ident, lua_hooks & lua)
{
  switch (get_path_status(p))
    {
    case path::nonexistent:
      return false;
    case path::file:
      break;
    case path::directory:
      W(F("expected file '%s', but it is a directory.") % p);
      return false;
    }

  hexenc<id> id;
  calculate_ident(p, id, lua);
  ident = file_id(id);

  return true;
}

void
read_localized_data(file_path const & path,
                    data & dat,
                    lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;

  bool do_lineconv = (lua.hook_get_linesep_conv(path, db_linesep, ext_linesep)
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(path, db_charset, ext_charset)
                      && db_charset != ext_charset);

  data tdat;
  read_data(path, tdat);

  string tmp1, tmp2;
  tmp2 = tdat();
  if (do_charconv) {
    tmp1 = tmp2;
    charset_convert(ext_charset, db_charset, tmp1, tmp2);
  }
  if (do_lineconv) {
    tmp1 = tmp2;
    line_end_convert(db_linesep, tmp1, tmp2);
  }
  dat = data(tmp2);
}

void
write_localized_data(file_path const & path,
                     data const & dat,
                     lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;

  bool do_lineconv = (lua.hook_get_linesep_conv(path, db_linesep, ext_linesep)
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(path, db_charset, ext_charset)
                      && db_charset != ext_charset);

  string tmp1, tmp2;
  tmp2 = dat();
  if (do_lineconv) {
    tmp1 = tmp2;
    line_end_convert(ext_linesep, tmp1, tmp2);
  }
  if (do_charconv) {
    tmp1 = tmp2;
    charset_convert(db_charset, ext_charset, tmp1, tmp2);
  }

  write_data(path, data(tmp2));
}

void
calculate_ident(file_path const & file,
                hexenc<id> & ident,
                lua_hooks & lua)
{
  string db_linesep, ext_linesep;
  string db_charset, ext_charset;

  bool do_lineconv = (lua.hook_get_linesep_conv(file, db_linesep, ext_linesep)
                      && db_linesep != ext_linesep);

  bool do_charconv = (lua.hook_get_charset_conv(file, db_charset, ext_charset)
                      && db_charset != ext_charset);

  if (do_charconv || do_lineconv)
    {
      data dat;
      read_localized_data(file, dat, lua);
      calculate_ident(dat, ident);
    }
  else
    {
      // no conversions necessary, use streaming form
      // Best to be safe and check it isn't a dir.
      assert_path_is_file(file);
      Botan::Pipe p(new Botan::Hash_Filter("SHA-160"), new Botan::Hex_Encoder());
      Botan::DataSource_Stream infile(file.as_external(), true);
      p.process_msg(infile);

      ident = hexenc<id>(lowercase(p.read_all_as_string()));
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
