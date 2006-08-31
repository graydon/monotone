
#include <string>

#include "config.h"
#include "i18n.h"
#include "options.hh"
#include "sanity.hh"

using std::string;
using std::vector;

namespace option
{
  namespace po = boost::program_options;
  //using boost::program_options::value;
  using boost::program_options::option_description;
  using boost::program_options::options_description;

  options_description global_options;
  options_description specific_options;

  no_option none;

//    {"no-show-c-function", 0, POPT_ARG_NONE, NULL, OPT_NO_SHOW_ENCLOSER, gettext_noop("another name for --no-show-encloser (for compatibility with GNU diff)"), NULL},
//    {"stdio", 0, POPT_ARG_NONE, NULL, OPT_STDIO, gettext_noop("serve netsync on stdio"), NULL},
//    {"no-transport-auth", 0, POPT_ARG_NONE, NULL, OPT_NO_TRANSPORT_AUTH, gettext_noop("disable transport authentication"), NULL},
//    {"automate-stdio-size", 's', POPT_ARG_LONG, &arglong, OPT_AUTOMATE_STDIO_SIZE, gettext_noop("block size in bytes for \"automate stdio\" output"), NULL},

  struct argless_value : public po::value_semantic
  {
    std::string name() const { return ""; }
    unsigned min_tokens() const { return 0; }
    unsigned max_tokens() const { return 0; }
    bool is_composing() const { return false; }
    void parse(boost::any & value_store, 
               const std::vector<std::string> & new_tokens,
               bool utf8) const
    {
      value_store = true;
    }
    bool apply_default(boost::any & value_store) const
    {
      return false;
    }
    void notify(const boost::any& value_store) const {}
  };
  template<typename T>
  struct repeatable_value : public po::value_semantic
  {
    std::string name() const { return ""; }
    unsigned min_tokens() const { return 1; }
    unsigned max_tokens() const { return 1; }
    bool is_composing() const { return false; }
    void parse(boost::any & value_store, 
               const std::vector<std::string> & new_tokens,
               bool utf8) const
    {
      value_store = boost::lexical_cast<T>(idx(new_tokens, 0));
    }
    bool apply_default(boost::any & value_store) const { return false; }
    void notify(const boost::any& value_store) const {}
  };
  template<typename T>
  struct repeated_value : public po::value_semantic
  {
    std::string name() const { return ""; }
    unsigned min_tokens() const { return 1; }
    unsigned max_tokens() const { return 1; }
    bool is_composing() const { return false; }
    void parse(boost::any & value_store, 
               const std::vector<std::string> & new_tokens,
               bool utf8) const
    {
      if (value_store.empty())
        value_store = std::vector<T>();
      std::vector<T> & val(boost::any_cast<std::vector<T>&>(value_store));
      val.push_back(boost::lexical_cast<T>(idx(new_tokens, 0)));
    }
    bool apply_default(boost::any & value_store) const { return false; }
    void notify(const boost::any& value_store) const {}
  };
  
  template<typename T>
  struct value
  {
    po::value_semantic * operator ()()
    {
      return new repeatable_value<T>();
    }
  };
  template<typename T>
  struct value<std::vector<T> >
  {
    po::value_semantic * operator ()()
    {
      return new repeated_value<T>();
    }
  };
  template<>
  struct value<nil>
  {
    po::value_semantic * operator ()()
    {
      return new argless_value();
    }
  };

  // the options below are also declared in options.hh for other users.  the
  // GOPT and COPT defines are just to reduce duplication, maybe there is a
  // cleaner way to do the same thing?

  const char *localize_string(const char *str)
  {
    localize_monotone();
    return gettext(str);
  }

  // global options
#define GOPT(NAME, OPT, TYPE, DESC) global<TYPE > NAME(new option_description(OPT, value<TYPE >()(), localize_string(DESC)))
  // command-specific options
#define COPT(NAME, OPT, TYPE, DESC) specific<TYPE > NAME(new option_description(OPT, value<TYPE >()(), localize_string(DESC)))
#include "options_list.hh"
#undef OPT
#undef COPT
}
