#ifndef __OPTIONS_HH__
#define __OPTIONS_HH__

// Copyright (C) 2005 Richard Levitte <richard@levitte.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

#include <string>
#include <vector>

namespace option
{
  using boost::program_options::option_description;
  using boost::program_options::options_description;
  using boost::shared_ptr;
  using std::string;
  using std::vector;

  extern options_description global_options;
  extern options_description specific_options;

  struct option_base
  {
    char const * operator()() { return o->long_name().c_str(); }
    shared_ptr<option_description> ptr() const { return o; }
  protected:
    option_base(option_description * p) : o(p) {}
    shared_ptr<option_description> o;
  };

  template<typename T>
  struct option : public option_base
  {
    T const & get(boost::program_options::variables_map const & vm)
    {
      boost::program_options::variable_value const & vv(vm[(*this)()]);
      // Workaround for gcc < 3.4. was return vv.as<T>();
      return boost::any_cast<const T&>(vv.value());
    }
    bool given(boost::program_options::variables_map const & vm)
    {
      return vm.count((*this)()) > 0;
    }
  protected:
    option(option_description * p) : option_base(p) {}
  };

  template<typename T>
  struct global : public option<T>
  {
    global(option_description * p) : option<T>(p)
    {
      global_options.add(this->o);
    }
  };

  template<typename T>
  struct specific : public option<T>
  {
    specific(option_description * p) : option<T>(p)
    {
      specific_options.add(this->o);
    }
  };

  struct no_option
  {
  };
  struct nil {};

  extern no_option none;

  // global options
#define GOPT(NAME, OPT, TYPE, DESC) extern global<TYPE > NAME;
  // command-specific options
#define COPT(NAME, OPT, TYPE, DESC) extern specific<TYPE > NAME;
#include "options_list.hh"
#undef GOPT
#undef COPT
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __OPTIONS_HH__
