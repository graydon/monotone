
#include "base.hh"
#include "lua.hh"

#include "basic_io.hh"

using std::vector;
using std::string;
using std::pair;
using std::make_pair;

LUAEXT(parse_basic_io, )
{
  vector<pair<string, vector<string> > > res;
  const string str(luaL_checkstring(L, -1), lua_strlen(L, -1));
  basic_io::input_source in(str, "monotone_parse_basic_io_for_lua");
  basic_io::tokenizer tok(in);
  try
    {
      string got;
      basic_io::token_type tt;
      do
        {
          tt = tok.get_token(got);
          switch (tt)
            {
            case basic_io::TOK_SYMBOL:
              res.push_back(make_pair(got, vector<string>()));
              break;
            case basic_io::TOK_STRING:
            case basic_io::TOK_HEX:
              E(!res.empty(), F("bad input to parse_basic_io"));
              res.back().second.push_back(got);
              break;
            default:
              break;
            }
        }
      while (tt != basic_io::TOK_NONE);
    }
  catch (informative_failure & e)
    {// there was a syntax error in our string
      lua_pushnil(L);
      return 1;
    }
  lua_newtable(L);
  int n = 1;
  for (vector<pair<string, vector<string> > >::const_iterator i = res.begin();
        i != res.end(); ++i)
    {
      lua_newtable(L);
      lua_pushstring(L, i->first.c_str());
      lua_setfield(L, -2, "name");
      lua_newtable(L);
      int m = 1;
      for (vector<string>::const_iterator j = i->second.begin();
            j != i->second.end(); ++j)
        {
          lua_pushstring(L, j->c_str());
          lua_rawseti(L, -2, m++);
        }
      lua_setfield(L, -2, "values");
      lua_rawseti(L, -2, n++);
    }
  return 1;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

