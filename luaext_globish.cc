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
    return luaL_error(L, e.what());
  } catch (pcre::compile_error e) {
    return luaL_error(L, (string("error parsing regex: ") + e.what()).c_str());
  } catch (pcre::match_error e) {
    return luaL_error(L, (string("error during match: ") + e.what()).c_str());
  } catch (...) {
    return luaL_error(L, "Unknown error.");
  }
  lua_pushboolean(L, result);
  return 1;
}

