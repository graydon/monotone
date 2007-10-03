#ifndef __OPTION_HH__
#define __OPTION_HH__

#include <stdexcept>
#include <map>
#include <set>
#include "vector.hh"

#include <boost/function.hpp>
#include "lexical_cast.hh"

#include "sanity.hh"
#include "vocab.hh"

// The types to represent the command line's parameters.
class arg_type : public utf8 {
public:
  explicit arg_type(void) : utf8() {}
  explicit arg_type(std::string const & s) : utf8(s) {}
  explicit arg_type(utf8 const & u) : utf8(u) {}
};
template <>
inline void dump(arg_type const & a, std::string & out) { out = a(); }
typedef std::vector< arg_type > args_vector;

namespace option {
  // Base for errors thrown by this code.
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
  // -ofoo or --opt=foo when the option doesn't take an argument
  struct extra_arg : public option_error
  {
    extra_arg(std::string const & opt);
  };
  // thrown by from_command_line when setting an option fails
  // by either boost::bad_lexical_cast or bad_arg_internal
  struct bad_arg : public option_error
  {
    bad_arg(std::string const & opt, arg_type const & arg);
    bad_arg(std::string const & opt,
	    arg_type const & arg,
	    std::string const & reason);
  };
  // from_command_line() catches this and boost::bad_lexical_cast
  // and converts them to bad_arg exceptions
  struct bad_arg_internal
  {
    std::string reason;
    bad_arg_internal(std::string const & str = "");
  };

  // Split a "long,s" option name into long and short names.
  void splitname(std::string const & from, std::string & name, std::string & n);

  // An option that can be set and reset.
  struct concrete_option
  {
    std::string description;
    std::string longname;
    std::string shortname;
    bool has_arg;
    boost::function<void (std::string)> setter;
    boost::function<void ()> resetter;

    concrete_option();
    concrete_option(std::string const & names,
		    std::string const & desc,
		    bool arg,
		    boost::function<void (std::string)> set,
		    boost::function<void ()> reset);

    bool operator<(concrete_option const & other) const;
  };

  // A group of options, which can be set from a command line
  // and can produce a usage string.
  struct concrete_option_set
  {
    std::set<concrete_option> options;
    concrete_option_set();
    concrete_option_set(std::set<concrete_option> const & other);
    concrete_option_set(concrete_option const & opt);

    // for building a concret_option_set directly (as done in unit_tests.cc),
    // rather than using intermediate machinery like in options*
    concrete_option_set &
    operator()(std::string const & names,
	       std::string const & desc,
	       boost::function<void ()> set,
	       boost::function<void ()> reset = 0);
    concrete_option_set &
    operator()(std::string const & names,
	       std::string const & desc,
	       boost::function<void (std::string)> set,
	       boost::function<void ()> reset = 0);

    concrete_option_set operator | (concrete_option_set const & other) const;
    void reset() const;
    std::string get_usage_str() const;
    void from_command_line(args_vector & args, bool allow_xargs = true);
    void from_command_line(int argc, char const * const * argv);
    void from_key_value_pairs(std::vector<std::pair<std::string, std::string> > const & keyvals);
  };
  concrete_option_set
  operator | (concrete_option const & a, concrete_option const & b);

  // used by the setter() functions below
  template<typename T>
  struct setter_class
  {
    T & item;
    setter_class(T & i)
      : item(i)
    {}
    void operator()(std::string s)
    {
      item = boost::lexical_cast<T>(s);
    }
  };
  template<>
  struct setter_class<bool>
  {
    bool & item;
    setter_class(bool & i)
      : item(i)
    {}
    void operator()()
    {
      item = true;
    }
  };
  template<typename T>
  struct setter_class<std::vector<T> >
  {
    std::vector<T> & items;
    setter_class(std::vector<T> & i)
      : items(i)
    {}
    void operator()(std::string s)
    {
      items.push_back(boost::lexical_cast<T>(s));
    }
  };
  template<typename T>
  struct resetter_class
  {
    T & item;
    T value;
    resetter_class(T & i, T const & v)
      : item(i), value(v)
    {}
    void operator()()
    {
      item = value;
    }
  };

  // convenience functions to generate a setter for a var
  template<typename T> inline
  boost::function<void(std::string)> setter(T & item)
  {
    return setter_class<T>(item);
  }
  inline boost::function<void()> setter(bool & item)
  {
    return setter_class<bool>(item);
  }
  // convenience function to generate a resetter for a var
  template<typename T> inline
  boost::function<void()> resetter(T & item, T const & value = T())
  {
    return resetter_class<T>(item, value);
  }

  // because std::bind1st can't handle producing a nullary functor
  template<typename T>
  struct binder_only
  {
    T * obj;
    boost::function<void(T*)> fun;
    binder_only(boost::function<void(T*)> const & f, T * o)
      : obj(o), fun(f)
      {}
    void operator()()
    {
      fun(obj);
    }
  };

  // Options that need to be attached to some other object
  // in order for set and reset to be meaningful.
  template<typename T>
  struct option
  {
    std::string description;
    std::string names;
    bool has_arg;
    boost::function<void (T*, std::string)> setter;
    boost::function<void (T*)> resetter;

    option(std::string const & name,
	   std::string const & desc,
	   bool arg,
	   void(T::*set)(std::string),
	   void(T::*reset)())
    {
      I(!name.empty() || !desc.empty());
      description = desc;
      names = name;
      has_arg = arg;
      setter = set;
      resetter = reset;
    }

    concrete_option instantiate(T * obj) const
    {
      concrete_option out;
      out.description = description;
      splitname(names, out.longname, out.shortname);
      out.has_arg = has_arg;

      if (setter)
	out.setter = std::bind1st(setter, obj);
      if (resetter)
	out.resetter = binder_only<T>(resetter, obj);
      return out;
    }

    bool operator<(option const & other) const
    {
      if (names != other.names)
        return names < other.names;
      return description < other.description;
    }
  };

  // A group of unattached options, which can be given an object
  // to attach themselves to.
  template<typename T>
  struct option_set
  {
    std::set<option<T> > options;
    option_set(){}
    option_set(option_set<T> const & other)
      : options(other.options)
    {}
    option_set(option<T> const & opt)
    {
      options.insert(opt);
    }

    option_set(std::string const & name,
	       std::string const & desc,
	       bool arg,
	       void(T::*set)(std::string),
	       void(T::*reset)())
    {
      options.insert(option<T>(name, desc, arg, set, reset));
    }
    concrete_option_set instantiate(T * obj) const
    {
      std::set<concrete_option> out;
      for (typename std::set<option<T> >::const_iterator i = options.begin();
	   i != options.end(); ++i)
	out.insert(i->instantiate(obj));
      return out;
    }
    option_set<T> operator | (option_set<T> const & other) const
    {
      option_set<T> combined;
      std::set_union(options.begin(), options.end(),
		     other.options.begin(), other.options.end(),
		     std::inserter(combined.options, combined.options.begin()));
      return combined;
    }
    option_set<T> operator - (option_set<T> const & other) const
    {
      option_set<T> combined;
      std::set_difference(options.begin(), options.end(),
                          other.options.begin(), other.options.end(),
                          std::inserter(combined.options,
                                        combined.options.begin()));
      return combined;
    }
    bool empty() const {return options.empty();}
  };
  template<typename T>
  option_set<T>
  operator | (option<T> const & a, option<T> const & b)
  {
    return option_set<T>(a) | b;
  }

}


#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
