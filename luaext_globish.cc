#include "lua.hh"

#include "globish.hh"
#include "sanity.hh"

using std::string;

LUAEXT(match, globish)
{
  const char *re = luaL_checkstring(L, -2);
  const char *str = luaL_checkstring(L, -1);

  bool result = false;
  try {
    string r(re);
    string n;
    string s(str);
    result = globish_matcher(r, n)(s);
  } catch (informative_failure & e) {
    lua_pushstring(L, e.what());
    lua_error(L);
    return 0;
  } catch (boost::bad_pattern & e) {
    lua_pushstring(L, e.what());
    lua_error(L);
    return 0;
  } catch (...) {
    lua_pushstring(L, "Unknown error.");
    lua_error(L);
    return 0;
  }
  lua_pushboolean(L, result);
  return 1;
}
