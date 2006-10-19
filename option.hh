#ifndef __OPTION_HH__
#define __OPTION_HH__

#include <stdexcept>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>

namespace option {
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
  struct bad_arg_internal
  {
    std::string reason;
    bad_arg_internal(std::string const & str = "");
  };

  void splitname(std::string const & from, std::string & name, std::string & n);
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
  struct concrete_option_set
  {
    std::set<concrete_option> options;
    concrete_option_set();
    concrete_option_set(std::set<concrete_option> const & other);
    concrete_option_set(concrete_option const & opt);
    concrete_option_set &
    operator()(std::string const & names,
	       std::string const & desc,
	       boost::function<void ()> set,
	       boost::function<void ()> reset() = 0);
    concrete_option_set &
    operator()(std::string const & names,
	       std::string const & desc,
	       boost::function<void (std::string)> set,
	       boost::function<void ()> reset() = 0);
    concrete_option_set & operator % (concrete_option_set const & other);
    concrete_option_set & operator % (concrete_option const & opt);
    void reset() const;
    std::string get_usage_str() const;
    void from_command_line(std::vector<std::string> & args, bool allow_xargs = true);
    void from_command_line(int argc, char const * const * argv);
  };
  // because std::bind1st can't handle producing a nullary functor
  template<typename T>
  struct binder_only : boost::function<void()>
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
  template<typename T>
  binder_only<T> bind_only(boost::function<void(T*)> const & f, T * o)
  {
    return binder_only<T>(f, o);
  }
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
	out.resetter = bind_only(resetter, obj);
      return out;
    }

    bool operator<(option const & other) const
    {
      return names < other.names;
    }
  };
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
    option_set<T> & operator % (option_set<T> const & other)
    {
      std::set<option<T> > combined;
      std::set_union(options.begin(), options.end(),
		     other.options.begin(), other.options.end(),
		     std::inserter(combined, combined.begin()));
      options = combined;
      return *this;
    }
    option_set<T> & operator % (option_set<T> const & (*fun)())
    {
      return *this % fun();
    }
    option_set<T> & operator % (option_set<T> & (*fun)())
    {
      return *this % fun();
    }
    bool empty() const {return options.empty();}
  };

}


#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
