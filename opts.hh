#ifndef __OPTS_HH__
#define __OPTS_HH__

#include <stdexcept>
#include <map>
#include <set>
#include <string>
#include <vector>

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

struct opts
{
  class optset
  {
  public:
    // This has to convert pointer-to-arbitrary-member-of-opts into
    // something uniform that can be put in a set (ie, has '<' and '==').
    typedef void * opt_id;
    template<typename T>
    static opt_id conv(T opts::* item)
    {
      return reinterpret_cast<opt_id>(&((*((opts*)0)).*item));
    }
  private:
    std::set<opt_id> items;
  public:
    template<typename T>
    void add(T opts::* item)
    {
      items.insert(conv(item));
    }
    template<typename T>
    optset & operator&(T opts::* item)
    {
      add(item);
      return *this;
    }
    bool contains(opt_id id) const
    {
      return items.find(id) != items.end();
    }
    template<typename T>
    bool contains(T opts::* item) const
    {
      return contains(conv(item));
    }
  };
  opts();
  void clear_cmd_opts();
  void set(std::string const & name, std::string const & given)
  { set(name, given, all_cmd_opts); }
  void from_cmdline(std::vector<std::string> const &args);
  void from_cmdline_restricted(std::vector<std::string> const & args,
                               optset allowed);


# define GOPT(varname, optname, type, has_arg, description) \
    type varname;                                           \
    void set_ ## varname (std::string const & arg);
# define COPT(varname, optname, type, has_arg, description) \
    type varname;                                           \
    void set_ ## varname (std::string const & arg);
# include "opts_list.hh"
# undef GOPT
# undef COPT


private:
  struct opt
  {
    void (opts::*setter)(std::string const &);
    std::string desc;
    bool has_arg;
    opts::optset::opt_id id;
  };
  std::map<std::string, opt> opt_map;
  optset global_opts, all_cmd_opts;
  opt const & getopt(std::string const & name, optset const & allowed);
  void set(std::string const & name, std::string const & given,
           optset const & allowed);
  template<typename T>
  void map_opt(void (opts::*setter)(std::string const &),
               std::string const & optname,
               bool has_arg,
               T opts::* ptr,
               std::string const & description);
};


#endif
