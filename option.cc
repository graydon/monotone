
#include "file_io.hh"
#include "option.hh"
#include "sanity.hh"
#include "ui.hh"

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace option {

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
bad_arg_internal::bad_arg_internal(string const & str)
 : reason(str)
{}



void splitname(string const & from, string & name, string & n)
{
  // from looks like "foo" or "foo,f"
  string::size_type comma = from.find(',');
  name = from.substr(0, comma);
  if (comma != string::npos)
    n = from.substr(comma+1, 1);
  else
    n = "";
}


concrete_option::concrete_option()
  : has_arg(false)
{}
concrete_option::concrete_option(std::string const & names,
                                 std::string const & desc,
                                 bool arg,
                                 boost::function<void (std::string)> set,
                                 boost::function<void ()> reset)
{
  description = desc;
  splitname(names, longname, shortname);
  has_arg = arg;
  setter = set;
  resetter = reset;
}

bool concrete_option::operator<(concrete_option const & other) const
{
  return longname < other.longname && (shortname.empty() || shortname != other.shortname);
}

concrete_option_set::concrete_option_set()
{}
concrete_option_set::concrete_option_set(std::set<concrete_option> const & other)
  : options(other)
{}
concrete_option_set::concrete_option_set(concrete_option const & opt)
{
  options.insert(opt);
}
class discard_argument : public boost::function<void (std::string const &)>
{
  boost::function<void()> functor;
 public:
  discard_argument(boost::function<void()> const & from)
    : functor(from)
    {}
    void operator()(std::string const &)
    { return functor(); }
};
concrete_option_set &
concrete_option_set::operator()(string const & names,
                                string const & desc,
                                boost::function<void ()> set,
                                boost::function<void ()> reset())
{
  options.insert(concrete_option(names, desc, false, discard_argument(set), reset));
  return *this;
}
concrete_option_set &
concrete_option_set::operator()(string const & names,
                                string const & desc,
                                boost::function<void (string)> set,
                                boost::function<void ()> reset())
{
  options.insert(concrete_option(names, desc, true, set, reset));
  return *this;
}
concrete_option_set &
concrete_option_set::operator % (concrete_option_set const & other)
{
  std::set<concrete_option> combined;
  std::set_union(options.begin(), options.end(),
                 other.options.begin(), other.options.end(),
                 std::inserter(combined, combined.begin()));
  options = combined;
  return *this;
}
concrete_option_set &
concrete_option_set::operator % (concrete_option const & opt)
{
  options.insert(opt);
  return *this;
}
void concrete_option_set::reset() const
{
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      if (i->resetter)
        i->resetter();
    }
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

void concrete_option_set::from_command_line(int argc, char const * const * argv)
{
  vector<string> arguments;
  for (int i = 1; i < argc; ++i)
    arguments.push_back(argv[i]);
  from_command_line(arguments, true);
}
static concrete_option const &
getopt(map<string, concrete_option> const & by_name, string const & name)
{
  map<string, concrete_option>::const_iterator i = by_name.find(name);
  if (i != by_name.end())
    return i->second;
  else
    throw option::unknown_option(name);
}

void concrete_option_set::from_command_line(std::vector<std::string> & args,
                                            bool allow_xargs)
{
  map<string, concrete_option> by_name;
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      if (!i->longname.empty())
        by_name.insert(make_pair(i->longname, *i));
      if (!i->shortname.empty())
        by_name.insert(make_pair(i->shortname, *i));
    }
  bool seen_dashdash = false;
  for (unsigned int i = 0; i < args.size(); ++i)
    {
      concrete_option o;
      string name, arg;
      bool separate_arg(false);
      if (idx(args,i) == "--" || seen_dashdash)
        {
          if (!seen_dashdash)
            {
              seen_dashdash = true;
              allow_xargs = false;
              continue;
            }
          name = "--";
          o = getopt(by_name, name);
          arg = idx(args,i);
        }
      else if (idx(args,i).substr(0,2) == "--")
        {
          string::size_type equals = idx(args,i).find('=');
          if (equals == string::npos)
            name = idx(args,i).substr(2);
          else
            name = idx(args,i).substr(2, equals-2);

          o = getopt(by_name, name);
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
          
          o = getopt(by_name, name);
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
          name = "--";
          o = getopt(by_name, name);
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
          try
            {
              if (o.setter)
                {
                  o.setter(arg);
                }
            }
          catch (boost::bad_lexical_cast)
            {
              throw bad_arg(o.longname, arg);
            }
          catch (bad_arg_internal & e)
            {
              if (e.reason == "")
                throw bad_arg(o.longname, arg);
              else
                throw bad_arg(o.longname, arg, e.reason);
            }
        }
    }
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

static string usagestr(option::concrete_option const & opt)
{
  string out;
  if (opt.longname == "--")
    return "";
  if (!opt.longname.empty() && !opt.shortname.empty())
    out = "--" + opt.longname + " [ -" + opt.shortname + " ]";
  else if (!opt.longname.empty())
    out = "--" + opt.longname;
  else if (!opt.shortname.empty())
    out = "-" + opt.shortname;
  else
    return "";
  if (opt.has_arg)
    return out + " <arg>";
  else
    return out;
}

std::string concrete_option_set::get_usage_str() const
{
  // combine the name strings like "--long [ -s ]"
  map<string, string> to_display;
  unsigned int namelen = 0; // the longest option name string
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      string names = usagestr(*i);
      if (names.size() > namelen)
        namelen = names.size();
    }

  // "    --long [ -s ] <arg>    description goes here"
  //  ^  ^^                 ^^  ^^                          ^
  //  |  | \    namelen    / |  | \        descwidth       /| <- edge of screen
  //  ^^^^                   ^^^^
  // pre_indent              space
  string result;
  int pre_indent = 2; // empty space on the left
  int space = 2; // space after the longest option, before the description
  int termwidth = guess_terminal_width();
  int descindent = pre_indent + namelen + space;
  int descwidth = termwidth - descindent;
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      string names = usagestr(*i);
      if (names.empty())
        continue;
      vector<string> desclines = wordwrap(i->description, descwidth);

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

} // namespace option

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
