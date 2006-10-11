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
#   define COPTSET(name) \
      name,
#   define GOPTSET(name) \
      name,
#   define COPTVAR(type, name, default_)
#   define GOPTVAR(type, name, default_)
#   define COPTION(optset, name, hasarg, optstring, description) \
      name,
#   define GOPTION(optset, name, hasarg, optstring, description) \
      name,
#   define OPTSET_REL(parent, child)

#   include "option_list.hh"

#   undef COPTSET
#   undef GOPTSET
#   undef COPTVAR
#   undef GOPTVAR
#   undef COPTION
#   undef GOPTION
#   undef OPTSET_REL

    none
  };
  class optset
  {
    std::set<option::optid> items;
  public:
    void add(option::optid item);
    optset & operator%(option::optid item);
    bool contains(option::optid id) const;
    bool empty() const;
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

#   define COPTSET(name) \
      bool name ## _given;
#   define GOPTSET(name) \
      bool name ## _given;
#   define COPTVAR(type, name, default_) \
      type name;
#   define GOPTVAR(type, name, default_) \
      type name;
#   define COPTION(optset, name, hasarg, optstring, description) \
      bool name ## _given;
#   define GOPTION(optset, name, hasarg, optstring, description) \
      bool name ## _given;
#   define OPTSET_REL(parent, child)

#   include "option_list.hh"

#   undef COPTSET
#   undef GOPTSET
#   undef COPTVAR
#   undef GOPTVAR
#   undef COPTION
#   undef GOPTION
#   undef OPTSET_REL

  private:

#   define COPTSET(name)
#   define GOPTSET(name)
#   define COPTVAR(type, name, default_)
#   define GOPTVAR(type, name, default_)
#   define COPTION(optset, name, hasarg, optstring, description) \
      void set_ ## name (std::string const & arg);
#   define GOPTION(optset, name, hasarg, optstring, description) \
      void set_ ## name (std::string const & arg);
#   define OPTSET_REL(parent, child)

#   include "option_list.hh"

#   undef COPTSET
#   undef GOPTSET
#   undef COPTVAR
#   undef GOPTVAR
#   undef COPTION
#   undef GOPTION
#   undef OPTSET_REL

    struct opt
    {
      void (options::*setter)(std::string const &);
      std::string desc;
      bool has_arg;
      option::optid id;
    };
    void note_given(optid id);
    std::map<std::string, opt> opt_map;
    optset global_option, all_cmd_option;
    opt const & getopt(std::string const & name, optset const & allowed);
    void set(std::string const & name, std::string const & given,
             optset const & allowed);
    void set(std::string const & name,
             opt const & o,
             std::string const & given);
    void map_opt(void (options::*setter)(std::string const &),
                 std::string const & optname,
                 optid id,
                 bool has_arg,
                 std::string const & description);
  };
}


#endif
