#ifndef __OPTIONS_HH__
#define __OPTIONS_HH__

#include <list>

#include "option.hh"
#include "paths.hh"
#include "dates.hh"

struct options
{
  options();

  typedef boost::function<void()> reset_function;
  typedef option::option<options> option_type;
  typedef option::option_set<options> options_type;
  typedef options_type const & (*static_options_fun)();

  static std::map<static_options_fun, std::set<static_options_fun> > &children();
  static std::map<static_options_fun, std::list<void(options::*)()> > &var_membership();

  void reset_optset(static_options_fun opt);

  struct opts
  {
    static options_type const & none ();
    static options_type const & all_options ();
# define OPTSET(name)	     \
    static options_type const & name ();

# define OPTVAR(optset, type, name, default_)

#define OPTION(optset, name, hasarg, optstring, description)	 \
    static options_type const & name ();

# define OPTSET_REL(parent, child)

# include "options_list.hh"

# undef OPTSET
# undef OPTVAR
# undef OPTION
# undef OPTSET_REL
  };

# define OPTSET(name)				\
  private:					\
  void reset_optset_ ## name ();

# define OPTVAR(optset, type, name, default_)	\
  public:					\
  type name;					\
  void reset_ ## name ();

#define OPTION(optset, name, hasarg, optstring, description)	 \
  public:							 \
  bool name ## _given;						 \
private:							 \
  void set_ ## name (std::string arg);				 \
  void real_set_ ## name (std::string arg);			 \
  void reset_opt_ ## name ();

# define OPTSET_REL(parent, child)

# include "options_list.hh"

# undef OPTSET
# undef OPTVAR
# undef OPTION
# undef OPTSET_REL
};

option::option_set<options>
operator | (option::option_set<options> const & opts,
	    option::option_set<options> const & (*fun)());

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
