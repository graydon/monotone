#include <algorithm>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "charset.hh"
#include "file_io.hh"
#include "option.hh"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"

using std::map;
using std::set;
using std::string;
using std::vector;


option_error::option_error(std::string const & str)
 : std::invalid_argument((F("option error: %s") % str).str())
{}
unknown_option::unknown_option(std::string const & opt)
 : option_error((F("unknown option '%s'") % opt).str())
{}
missing_arg::missing_arg(std::string const & opt)
 : option_error((F("missing argument to option '%s'") % opt).str())
{}
extra_arg::extra_arg(std::string const & opt)
 : option_error((F("option '%s' does not take an argument") % opt).str())
{}
bad_arg::bad_arg(std::string const & opt, std::string const & arg)
 : option_error((F("bad argument '%s' to option '%s'") % arg % opt).str())
{}
bad_arg::bad_arg(std::string const & opt,
                 std::string const & arg,
                 std::string const & reason)
 : option_error((F("bad argument '%s' to option '%s': %s")
                   % arg % opt % reason).str())
{}
struct bad_arg_internal
{
  string reason;
  bad_arg_internal(string const & str = "") : reason(str) {}
};



map<option::optid, set<option::optid> > & option_groups()
{
  static map<option::optid, set<option::optid> > val;
  bool first(true);
  if (first)
    {
#     define COPTSET(name)
#     define GOPTSET(name)
#     define COPTVAR(type, name, default_)
#     define GOPTVAR(type, name, default_)
#     define COPTION(optset, name, hasarg, optstring, description) \
        val[option:: name].insert(option:: name);          \
        val[option:: name].insert(option:: optset);
#     define GOPTION(optset, name, hasarg, optstring, description) \
        val[option:: name].insert(option:: name);          \
        val[option:: name].insert(option:: optset);
#     define OPTSET_REL(parent, child) \
        val[option:: child].insert(option:: parent);

#     include "option_list.hh"

#     undef COPTSET
#     undef GOPTSET
#     undef COPTVAR
#     undef GOPTVAR
#     undef COPTION
#     undef GOPTION
#     undef OPTSET_REL

      first = false;
    }
  return val;
}
void note_relation(option::optid opt, option::optid group)
{
  option_groups()[opt].insert(group);
}

void option::optset::add(option::optid item)
{
  items.insert(item);
}
option::optset & option::optset::operator%(option::optid item)
{
  add(item);
  return *this;
}
bool option::optset::contains(option::optid id) const
{
  set<option::optid> intersect;
  set<option::optid> &groups = option_groups()[id];
  set_intersection(items.begin(), items.end(),
                   groups.begin(), groups.end(),
                   std::inserter(intersect, intersect.begin()));
  return !intersect.empty();
}
bool option::optset::empty() const
{
  return items.empty();
}


void bind_opt::set(string const & arg)
{
  string addr_part, port_part;
  size_t l_colon = arg.find(':');
  size_t r_colon = arg.rfind(':');

  // not an ipv6 address, as that would have at least two colons
  if (l_colon == r_colon)
    {
      addr_part = (r_colon == string::npos ? arg : arg.substr(0, r_colon));
      port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
    }
  else
    {
      // IPv6 addresses have a port specified in the style: [2001:388:0:13::]:80
      size_t squareb = arg.rfind(']');
      if ((arg.find('[') == 0) && (squareb != string::npos))
        {
          if (squareb < r_colon)
            port_part = (r_colon == string::npos ? "" :  arg.substr(r_colon+1, arg.size() - r_colon));
          else
            port_part = "";
          addr_part = (squareb == string::npos ? arg.substr(1, arg.size()) : arg.substr(1, squareb-1));
        }
      else
        {
          addr_part = arg;
          port_part = "";
        }
    }
  stdio = false;
  address = utf8(addr_part);
  port = utf8(port_part);
}

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
bool has_arg() { return true; }
template<>
bool has_arg<bool>() { return false; }


void option::options::map_opt(void (option::options::*setter)(string const &),
                              string const & optname,
                              option::optid id,
                              bool has_arg,
                              string const & description)
{
  opt o;
  o.setter = setter;
  o.has_arg = has_arg;
  o.desc = gettext(description.c_str());
  o.id = id;
  string name, n;
  splitname(/*gettext*/(optname.c_str()), name, n);
  opt_map.insert(make_pair(name, o));
  if (n != "")
    opt_map.insert(make_pair(n, o));
}

option::options::options()
{
# define COPTSET(name) \
    name ## _given = false;
# define GOPTSET(name) \
    name ## _given = false;
# define COPTVAR(type, name, default_) \
    name = type ( default_ );
# define GOPTVAR(type, name, default_) \
    name = type ( default_ );
# define COPTION(optset, name, hasarg, optstring, description)     \
    map_opt(&options::set_ ## name, optstring,             \
            option:: name, hasarg,  description); \
    all_cmd_option.add( option:: name );          \
    name ## _given = false;
# define GOPTION(optset, name, hasarg, optstring, description)     \
    map_opt(&options::set_ ## name, optstring,             \
            option:: name, hasarg,  description); \
    global_option.add( option:: name );          \
    name ## _given = false;
# define OPTSET_REL(parent, child)

# include "option_list.hh"

# undef COPTSET
# undef GOPTSET
# undef COPTVAR
# undef GOPTVAR
# undef COPTION
# undef GOPTION
# undef OPTSET_REL
}

void option::options::clear_cmd_options()
{
# define COPTSET(name) \
    name ## _given = false;
# define GOPTSET(name)
# define COPTVAR(type, name, default_) \
    name = type ( default_ );
# define GOPTVAR(type, name, default_)
# define COPTION(optset, name, hasarg, optstring, description) \
    name ## _given = false;
# define GOPTION(optset, name, hasarg, optstring, description)
# define OPTSET_REL(parent, child)

# include "option_list.hh"

# undef COPTSET
# undef GOPTSET
# undef COPTVAR
# undef GOPTVAR
# undef COPTION
# undef GOPTION
# undef OPTSET_REL
}

void option::options::clear_global_options()
{
# define COPTSET(name)
# define GOPTSET(name) \
    name ## _given = false;
# define COPTVAR(type, name, default_)
# define GOPTVAR(type, name, default_) \
    name = type ( default_ );
# define COPTION(optset, name, hasarg, optstring, description)
# define GOPTION(optset, name, hasarg, optstring, description) \
    name ## _given = false;
# define OPTSET_REL(parent, child)

# include "option_list.hh"

# undef COPTSET
# undef GOPTSET
# undef COPTVAR
# undef GOPTVAR
# undef COPTION
# undef GOPTION
# undef OPTSET_REL
}

# define COPTSET(name)
# define GOPTSET(name)
# define COPTVAR(type, name, default_)
# define GOPTVAR(type, name, default_)
# define COPTION(optset, name, hasarg, optstring, description) \
  void option::options::set_ ## name (std::string const & arg)
# define GOPTION(optset, name, hasarg, optstring, description) \
  void option::options::set_ ## name (std::string const & arg)
# define OPTSET_REL(parent, child)

#define option_bodies
# include "option_list.hh"
#undef option_bodies

# undef COPTSET
# undef GOPTSET
# undef COPTVAR
# undef GOPTVAR
# undef COPTION
# undef GOPTION
# undef OPTSET_REL

option::options::opt const &
option::options::getopt(string const & name, optset const & allowed)
{
  map<string, opt>::iterator i = opt_map.find(name);
  if (i == opt_map.end())
    throw unknown_option(name);
  else if (!global_option.contains(i->second.id)
           && !allowed.contains(i->second.id))
    throw unknown_option(name);
  else
    return i->second;
}

void
option::options::note_given(optid id)
{
  static map<optid, bool options::*> givens;
  static bool first(true);
  if (first)
    {
#     define COPTSET(name) \
        givens[option:: name] = &options:: name ## _given;
#     define GOPTSET(name) \
        givens[option:: name] = &options:: name ## _given;
#     define COPTVAR(type, name, default_)
#     define GOPTVAR(type, name, default_)
#     define COPTION(optset, name, hasarg, optstring, description) \
        givens[option:: name] = &options:: name ## _given;
#     define GOPTION(optset, name, hasarg, optstring, description) \
        givens[option:: name] = &options:: name ## _given;
#     define OPTSET_REL(parent, child)

#     include "option_list.hh"

#     undef COPTSET
#     undef GOPTSET
#     undef COPTVAR
#     undef GOPTVAR
#     undef COPTION
#     undef GOPTION
#     undef OPTSET_REL
      first = false;
    }

  // keep the std:: ; we have a member function called 'set'
  std::set<optid> const &which = option_groups()[id];
  for (std::set<optid>::const_iterator i = which.begin();
       i != which.end(); ++i)
    this->*givens[*i] = true;
}

void option::options::set(string const & name,
                          opt const & o,
                          string const & given)
{
  note_given(o.id);
  try
    {
      (this->*o.setter)(given);
    }
  catch (boost::bad_lexical_cast)
    {
      throw bad_arg(name, given);
    }
  catch (bad_arg_internal & e)
    {
      if (e.reason == "")
        throw bad_arg(name, given);
      else
        throw bad_arg(name, given, e.reason);
    }
}

void option::options::set(string const & name, string const & given,
                          optset const & allowed)
{
  set(name, getopt(name, allowed), given);
}

static void
tokenize_for_command_line(string const & from, vector<string> & to)
{
  // Unfortunately, the tokenizer in basic_io is too format-specific
  to.clear();
  enum quote_type {none, one, two};
  string cur;
  quote_type type = none;
  bool have_tok(false);
  
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      if (*i == '\'')
        {
          if (type == none)
            type = one;
          else if (type == one)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '"')
        {
          if (type == none)
            type = two;
          else if (type == two)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '\\')
        {
          if (type != one)
            ++i;
          N(i != from.end(), F("Invalid escape in --xargs file"));
          cur += *i;
          have_tok = true;
        }
      else if (string(" \n\t").find(*i) != string::npos)
        {
          if (type == none)
            {
              if (have_tok)
                to.push_back(cur);
              cur.clear();
              have_tok = false;
            }
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else
        {
          cur += *i;
          have_tok = true;
        }
    }
  if (have_tok)
    to.push_back(cur);
}

void option::options::from_cmdline_restricted(std::vector<std::string> & args,
                                              optset allowed,
                                              bool allow_xargs)
{
  for (unsigned int i = 0; i < args.size(); ++i)
    {
      opt o;
      string name, arg;
      bool separate_arg(false);
      if (idx(args,i).substr(0,2) == "--")
        {
          string::size_type equals = idx(args,i).find('=');
          if (equals == string::npos)
            name = idx(args,i).substr(2);
          else
            name = idx(args,i).substr(2, equals-2);

          o = getopt(name, allowed);
          if (!o.has_arg && equals != string::npos)
            throw extra_arg(name);

          if (o.has_arg)
            {
              if (equals == string::npos)
                {
                  separate_arg = true;
                  if (i+1 == args.size())
                    throw missing_arg(name);
                  arg = idx(args,i+1);
                }
              else
                arg = idx(args,i).substr(equals+1);
            }
        }
      else if (idx(args,i).substr(0,1) == "-")
        {
          name = idx(args,i).substr(1,1);
          
          o = getopt(name, allowed);
          if (!o.has_arg && idx(args,i).size() != 2)
            throw extra_arg(name);
          
          if (o.has_arg)
            {
              if (idx(args,i).size() == 2)
                {
                  separate_arg = true;
                  if (i+1 == args.size())
                    throw missing_arg(name);
                  arg = idx(args,i+1);
                }
              else
                arg = idx(args,i).substr(2);
            }
        }
      else
        {
          name = "";
          o = getopt(name, allowed);
          arg = idx(args,i);
        }
      if (allow_xargs && (name == "xargs" || name == "@"))
        {
          // expand the --xargs in place
          data dat;
          read_data_for_command_line(arg, dat);
          vector<string> fargs;
          tokenize_for_command_line(dat(), fargs);
          
          args.erase(args.begin() + i);
          if (separate_arg)
            args.erase(args.begin() + i);
          args.insert(args.begin()+i, fargs.begin(), fargs.end());
          --i;
        }
      else
        {
          if (separate_arg)
            ++i;
          set(name, o, arg);
        }
    }
}

void option::options::from_cmdline(vector<string> & args, bool allow_xargs)
{
  from_cmdline_restricted(args, all_cmd_option, allow_xargs);
}

static vector<string> wordwrap(string str, unsigned int width)
{
  vector<string> out;
  while (str.size() > width)
    {
      string::size_type pos = str.find_last_of(" ", width);
      if (pos == string::npos)
        { // bah. we have to break a word
          out.push_back(str.substr(0, width));
          str = str.substr(width);
        }
      else
        {
          out.push_back(str.substr(0, pos));
          str = str.substr(pos+1);
        }
    }
  out.push_back(str);
  return out;
}

struct optstrings
{
  string longname;
  string shortname;
  string desc;
};
std::string option::options::get_usage_str(optset const & opts) const
{
  // collect the options we want to show (opt_map has separate
  // entries for the short and long versions of each option)
  map<optid, optstrings> option_strings;
  for (map<string, opt>::const_iterator i = opt_map.begin();
       i != opt_map.end(); ++i)
    {
      if (opts.contains(i->second.id) && !i->first.empty())
        { // i->first.empty() indicates that this is the entry for
          // positional args; we don't want to include that here
          optstrings & strs = option_strings[i->second.id];
          strs.desc = i->second.desc;
          if (i->first.size() == 1)
            strs.shortname = i->first;
          else
            strs.longname = i->first;
        }
    }

  // combind the name strings like "--long [ -s ]"
  map<string, string> to_display;
  unsigned int namelen = 0; // the longest option name string
  for (map<optid, optstrings>::iterator i = option_strings.begin();
       i != option_strings.end(); ++i)
    {
      optstrings const & strings = i->second;
      string names;
      if (!strings.longname.empty() && !strings.shortname.empty())
        {
          names = "--" + strings.longname + " [ -" + strings.shortname + " ]";
        }
      else if (!strings.longname.empty())
        {
          names = "--" + strings.longname;
        }
      else // short name only
        {
          names = "-" + strings.shortname;
        }
      to_display.insert(make_pair(names, strings.desc));
      if (names.size() > namelen)
        namelen = names.size();
    }

  // "    --long [ -s ]    description goes here"
  //  ^  ^^           ^^  ^^                          ^
  //  |  | \ namelen / |  | \        descwidth       /| <- edge of screen
  //  ^^^^             ^^^^
  // pre_indent        space
  string result;
  int pre_indent = 2; // empty space on the left
  int space = 2; // space after the longest option, before the description
  for (map<string, string>::const_iterator i = to_display.begin();
       i != to_display.end(); ++i)
    {
      string const & names = i->first;
      int descindent = pre_indent + namelen + space;
      int descwidth = guess_terminal_width() - descindent;
      vector<string> desclines = wordwrap(i->second, descwidth);

      result += string(pre_indent, ' ')
              + names + string(namelen - names.size(), ' ')
              + string(space, ' ');
      vector<string>::const_iterator line = desclines.begin();
      if (line != desclines.end())
        {
          result += *line + "\n";
          ++line;
        }
      else // no description
        result += "\n";
      for (; line != desclines.end(); ++line)
        result += string(descindent, ' ') + *line + "\n";
    }
  return result;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
