#include "base.hh"
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
    globish g(re);
    result = g.matches(str);
  } catch (informative_failure & e) {
    return luaL_error(L, e.what());
  } catch (...) {
    return luaL_error(L, "Unknown error.");
  }
  lua_pushboolean(L, result);
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

