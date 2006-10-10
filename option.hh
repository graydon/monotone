#ifndef __OPTS_HH__
#define __OPTS_HH__

#include <stdexcept>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>

#include "i18n.h"
#include "paths.hh"
#include "vocab.hh"

struct option_error : public std::invalid_argument
{
  option_error(std::string const & str);
};
struct unknown_option : public option_error
{
  unknown_option(std::string const & opt);
};
struct missing_arg : public option_error
{
  missing_arg(std::string const & opt);
};
struct extra_arg : public option_error
{
  extra_arg(std::string const & opt);
};
struct bad_arg : public option_error
{
  bad_arg(std::string const & opt, std::string const & arg);
  bad_arg(std::string const & opt,
          std::string const & arg,
          std::string const & reason);
};

struct bind_opt
{
  utf8 address;
  utf8 port;
  bool stdio;
  bind_opt() : stdio(false) {}
  void set(std::string const & arg);
};

namespace option {
  enum optid {
#   define GOPT(varname, optname, type, default_, description)  \
      varname,
#   define COPT(varname, optname, type, default_, description)  \
      GOPT(varname, optname, type, default_, description)
#   include "option_list.hh"
#   undef GOPT
#   undef COPT
    none
  };
  class optset
  {
    std::set<option::optid> items;
  public:
    void add(option::optid item)
    {
      items.insert(item);
    }
    optset & operator%(option::optid item)
    {
      add(item);
      return *this;
    }
    bool contains(option::optid id) const
    {
      return items.find(id) != items.end();
    }
    bool empty() const
    {
      return items.empty();
    }
  };

  struct options
  {
    options();
    void clear_cmd_options();
    void clear_global_options();
    void set(std::string const & name, std::string const & given)
    { set(name, given, all_cmd_option); }
    // if allow_xargs is true, then any instances of --xargs (or -@) in
    // args will be replaced with the parsed contents of the referenced file
    void from_cmdline(std::vector<std::string> & args, bool allow_xargs = true);
    void from_cmdline_restricted(std::vector<std::string> & args,
                                 optset allowed,
                                 bool allow_xargs = true);
    optset const & global_opts() const { return global_option; }
    optset const & specific_opts() const { return all_cmd_option; }

    std::string get_usage_str(optset const & opts) const;

#   define GOPT(varname, optname, type, default_, description)  \
      type varname;                                             \
      bool varname ## _given;
#   define COPT(varname, optname, type, default_, description)  \
      GOPT(varname, optname, type, default_, description)
#   include "option_list.hh"
#   undef GOPT
#   undef COPT
  private:
#   define GOPT(varname, optname, type, default_, description)  \
      void set_ ## varname (std::string const & arg);           \
      void set_ ## varname ## _helper (std::string const & arg);
#   define COPT(varname, optname, type, default_, description)  \
      void set_ ## varname (std::string const & arg);           \
      void set_ ## varname ## _helper (std::string const & arg);
#   include "option_list.hh"
#   undef GOPT
#   undef COPT

    struct opt
    {
      void (options::*setter)(std::string const &);
      std::string desc;
      bool has_arg;
      option::optid id;
    };
    std::map<std::string, opt> opt_map;
    optset global_option, all_cmd_option;
    opt const & getopt(std::string const & name, optset const & allowed);
    void set(std::string const & name, std::string const & given,
             optset const & allowed);
    void map_opt(void (options::*setter)(std::string const &),
                 std::string const & optname,
                 optid id,
                 bool has_arg,
                 std::string const & description);
  };
}


#endif
