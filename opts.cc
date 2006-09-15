#include "opts.hh"

using std::map;
using std::set;
using std::string;
using std::vector;

option_error::option_error(std::string const & str)
 : std::invalid_argument(str)
{}
unknown_option::unknown_option(std::string const & opt)
 : option_error(opt)
{}
missing_arg::missing_arg(std::string const & opt)
 : option_error(opt)
{}
extra_arg::extra_arg(std::string const & opt)
 : option_error(opt)
{}

static void splitname(string const & from, string & name, string & n)
{
  // from looks like "foo" or "foo,f"
  string::size_type comma = from.find(',');
  name = from.substr(0, comma);
  if (comma != string::npos)
    n = from.substr(comma+1, 1);
  else
    n = "";
}

template<typename T>
void opts::map_opt(void (opts::*setter)(string const &),
                   string const & optname,
                   bool has_arg,
                   T opts::* ptr,
                   string const & description)
{
  opts::opt o;
  o.setter = setter;
  o.has_arg = has_arg;
  o.desc = gettext(description.c_str());
  o.id = optset::conv(ptr);
  string name, n;
  splitname(/*gettext*/(optname.c_str()), name, n);
  opt_map.insert(make_pair(name, o));
  if (n != "")
    opt_map.insert(make_pair(n, o));
}

opts::opts()
{
# define GOPT(varname, optname, type, has_arg, description)         \
    map_opt(&opts::set_ ## varname, optname, has_arg,               \
            &opts:: varname, description);                          \
    global_opts.add( &opts:: varname );
# define COPT(varname, optname, type, has_arg, description)         \
    map_opt(&opts::set_ ## varname, optname, has_arg,               \
            &opts:: varname, description);                          \
    all_cmd_opts.add( &opts:: varname );
# include "opts_list.hh"
# undef GOPT
# undef COPT
}

void opts::clear_cmd_opts()
{
# define GOPT(varname, optname, type, has_arg, description)
# define COPT(varname, optname, type, has_arg, description) \
    varname = type ();
# include "opts_list.hh"
# undef GOPT
# undef COPT
}

#define GOPT(varname, optname, type, has_arg, description) \
  void opts::set_ ## varname (string const & arg)
#define COPT(varname, optname, type, has_arg, description) \
  void opts::set_ ## varname (string const & arg)
#define option_bodies
#include "opts_list.hh"
#undef GOPT
#undef COPT
#undef option_bodies

opts::opt const & opts::getopt(string const & name, optset const & allowed)
{
  map<string, opt>::iterator i = opt_map.find(name);
  if (i == opt_map.end())
    throw unknown_option(name);
  else if (!global_opts.contains(i->second.id)
           && !allowed.contains(i->second.id))
    throw unknown_option(name);
  else
    return i->second;
}

void opts::set(string const & name, string const & given,
               optset const & allowed)
{
  (this->*getopt(name, allowed).setter)(given);
}

void opts::from_cmdline_restricted(std::vector<std::string> const & args,
                                   optset allowed)
{
  for (vector<string>::const_iterator iter = args.begin();
       iter != args.end(); ++iter)
    {
      if (iter->substr(0,2) == "--")
        {
          string name;
          string::size_type equals = iter->find('=');
          if (equals == string::npos)
            name = iter->substr(2);
          else
            name = iter->substr(2, equals-2);

          opt const & o = getopt(name, allowed);
          if (!o.has_arg && equals != string::npos)
            throw extra_arg(name);

          string arg;
          if (o.has_arg)
            {
              if (equals == string::npos)
                {
                  ++iter;
                  if (iter == args.end())
                    throw missing_arg(name);
                  arg = *iter;
                }
              else
                arg = iter->substr(equals+1);
            }
          (this->*o.setter)(arg);
        }
      else if (iter->substr(0,1) == "-")
        {
          string name = iter->substr(1,1);
          
          opt const & o = getopt(name, allowed);
          if (!o.has_arg && iter->size() != 2)
            throw extra_arg(name);
          
          string arg;
          if (o.has_arg)
            {
              if (iter->size() == 2)
                {
                  ++iter;
                  if (iter == args.end())
                    throw missing_arg(name);
                  arg = *iter;
                }
              else
                arg = iter->substr(2);
            }
          (this->*o.setter)(arg);
        }
      else
        {
          set("", *iter, allowed);
        }
    }
}

void opts::from_cmdline(vector<string> const & args)
{
  from_cmdline_restricted(args, all_cmd_opts);
}
