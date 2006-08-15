
#include "lua.hh"
#include "mkstemp.hh"

#include <errno.h>
#include <cstring>

using std::string;
using std::strerror;

LUAEXT(mkstemp, )
{
  int fd = -1;
  FILE **pf = NULL;
  char const *filename = luaL_checkstring (L, -1);
  string dup(filename);

  fd = monotone_mkstemp(dup);

  if (fd == -1)
    return 0;

  // this magic constructs a lua object which the lua io library
  // will enjoy working with
  pf = static_cast<FILE **>(lua_newuserdata(L, sizeof(FILE *)));
  *pf = fdopen(fd, "r+");
  lua_pushstring(L, "FILE*");
  lua_rawget(L, LUA_REGISTRYINDEX);
  lua_setmetatable(L, -2);

  lua_pushstring(L, dup.c_str());

  if (*pf == NULL)
    {
      lua_pushnil(L);
      lua_pushfstring(L, "%s", strerror(errno));
      lua_pushnumber(L, errno);
      return 3;
    }
  else
    return 2;
}
