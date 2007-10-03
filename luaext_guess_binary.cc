
#include "base.hh"
#include "lua.hh"

#include <fstream>

#include "file_io.hh"

using std::ifstream;
using std::ios_base;
using std::string;

LUAEXT(guess_binary_file_contents, )
{
  const char *path = luaL_checkstring(L, 1);

  ifstream file(path, ios_base::binary);
  if (!file)
    {
      lua_pushnil(L);
      return 1;
    }
  const int bufsize = 8192;
  char tmpbuf[bufsize];
  string buf;
  while (file.read(tmpbuf, sizeof tmpbuf))
    {
      I(file.gcount() <= static_cast<int>(sizeof tmpbuf));
      buf.assign(tmpbuf, file.gcount());
      if (guess_binary(buf))
        {
          lua_pushboolean(L, true);
          return 1;
        }
    }
  lua_pushboolean(L, false);
  return 1;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

