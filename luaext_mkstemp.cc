
#include "lua.hh"
#include "mkstemp.hh"

#include <errno.h>
#include <cstring>

using std::string;
using std::strerror;

LUAEXT(mkstemp, )
{
  char const *filename = luaL_checkstring (L, -1);
  string dup(filename);

  if (!monotone_mkstemp(dup))
    return 0;

  lua_pushstring(L, dup.c_str());
  return 1;
}

