
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
