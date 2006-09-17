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

struct option
{
  struct nothing {};
  static nothing none;
  class optset
  {
  public:
    // This has to convert pointer-to-arbitrary-member-of-option into
    // something uniform that can be put in a set (ie, has '<' and '==').
    typedef void * opt_id;
    template<typename T>
    static opt_id conv(T option::* item)
    {
      return reinterpret_cast<opt_id>(&((*((option*)0)).*item));
    }
  private:
    std::set<opt_id> items;
  public:
    template<typename T>
    void add(T option::* item)
    {
      items.insert(conv(item));
    }
    template<typename T>
    optset & operator%(T option::* item)
    {
      add(item);
      return *this;
    }
    optset & operator%(nothing*)
    {
      return *this;
    }
    bool contains(opt_id id) const
    {
      return items.find(id) != items.end();
    }
    template<typename T>
    bool contains(T option::* item) const
    {
      return contains(conv(item));
    }
    bool empty() const
    {
      return items.empty();
    }
  };
  option();
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

# define GOPT(varname, optname, type, default_, description)  \
    type varname;                                             \
    bool varname ## _given;
# define COPT(varname, optname, type, default_, description)  \
    type varname;                                             \
    bool varname ## _given;
# include "option_list.hh"
# undef GOPT
# undef COPT
private:
# define GOPT(varname, optname, type, default_, description)  \
    void set_ ## varname (std::string const & arg);           \
    void set_ ## varname ## _helper (std::string const & arg);
# define COPT(varname, optname, type, default_, description)  \
    void set_ ## varname (std::string const & arg);           \
    void set_ ## varname ## _helper (std::string const & arg);
# include "option_list.hh"
# undef GOPT
# undef COPT

  struct opt
  {
    void (option::*setter)(std::string const &);
    std::string desc;
    bool has_arg;
    option::optset::opt_id id;
  };
  std::map<std::string, opt> opt_map;
  optset global_option, all_cmd_option;
  opt const & getopt(std::string const & name, optset const & allowed);
  void set(std::string const & name, std::string const & given,
           optset const & allowed);
  template<typename T>
  void map_opt(void (option::*setter)(std::string const &),
               std::string const & optname,
               T option::* ptr,
               std::string const & description);
};


#endif
