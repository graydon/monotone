#ifndef __LOCALIZED_FILE_IO_HH__
#define __LOCALIZED_FILE_IO_HH__
// This gets it's own file because it depends on lua_hooks and
// therefore app_state. Otherwise it would be in file_io.{cc,hh}
// This separation is based entirely on dependencies, not functionality.

#include "paths.hh"
#include "vocab.hh"

class lua_hooks;

void read_localized_data(file_path const & path,
                         data & dat,
                         lua_hooks & lua);
bool ident_existing_file(file_path const & p, file_id & ident, lua_hooks & lua);
void calculate_ident(file_path const & file,
                     hexenc<id> & ident,
                     lua_hooks & lua);

void write_localized_data(file_path const & path,
                          data const & dat,
                          lua_hooks & lua);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
