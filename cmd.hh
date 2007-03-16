#ifndef __CMD_HH__
#define __CMD_HH__

#include <boost/shared_ptr.hpp>

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "app_state.hh"
#include "commands.hh"
#include "constants.hh"
#include "options.hh"
#include "sanity.hh"


namespace commands
{
  std::string const & hidden_group();
  using boost::shared_ptr;


  struct command
  {
    // NB: these strings are stored _un_translated, because we cannot
    // translate them until after main starts, by which time the
    // command objects have all been constructed.
    std::string name;
    std::string cmdgroup;
    std::string params_;
    std::string desc_;
    bool use_workspace_options;
    options::options_type opts;
    command(std::string const & n,
            std::string const & g,
            std::string const & p,
            std::string const & d,
            bool u,
            options::options_type const & o);
    virtual ~command();
    virtual std::string params();
    virtual std::string desc();
    virtual options::options_type get_options(std::vector<utf8> const & args);
    virtual void exec(app_state & app,
                      std::vector<utf8> const & args) = 0;
  };
};

inline std::vector<file_path>
args_to_paths(std::vector<utf8> const & args)
{
  std::vector<file_path> paths;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      if (bookkeeping_path::external_string_is_bookkeeping_path(*i))
        W(F("ignored bookkeeping path '%s'") % *i);
      else 
        paths.push_back(file_path_external(*i));
    }
  // "it should not be the case that args were passed, but our paths set
  // ended up empty".  This test is because some commands have default
  // behavior for empty path sets -- in particular, it is the same as having
  // no restriction at all.  "mtn revert _MTN" turning into "mtn revert"
  // would be bad.  (Or substitute diff, etc.)
  N(!(!args.empty() && paths.empty()),
    F("all arguments given were bookkeeping paths; aborting"));
  return paths;
}

std::string
describe_revision(app_state & app,
                  revision_id const & id);

void
complete(app_state & app,
         std::string const & str,
         revision_id & completion,
         bool must_exist=true);

void
complete(app_state & app,
         std::string const & str,
         std::set<revision_id> & completion,
         bool must_exist=true);

template<typename ID>
static void
complete(app_state & app,
         std::string const & str,
         ID & completion)
{
  N(str.find_first_not_of(constants::legal_id_bytes) == std::string::npos,
    F("non-hex digits in id"));
  if (str.size() == constants::idlen)
    {
      completion = ID(str);
      return;
    }
  std::set<ID> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have an expansion") % str);
  if (completions.size() > 1)
    {
      std::string err =
        (F("partial id '%s' has multiple ambiguous expansions:")
         % str).str();
      for (typename std::set<ID>::const_iterator i = completions.begin();
            i != completions.end(); ++i)
        err += (i->inner()() + "\n");
      N(completions.size() == 1, i18n_format(err));
    }
  completion = *(completions.begin());
  P(F("expanded partial id '%s' to '%s'")
    % str % completion);
}

void
notify_if_multiple_heads(app_state & app);

void
process_commit_message_args(bool & given,
                            utf8 & log_message,
                            app_state & app,
                            utf8 message_prefix = utf8(""));

void
get_content_paths(roster_t const & roster, 
                  std::map<file_id, 
                  file_path> & paths);

#define CMD(C, group, params, desc, opts)                            \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, true,             \
                          options::options_type() | opts)            \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)

// Use this for commands that want to define a params() function
// instead of having a static description. (Good for "automate"
// and possibly "list".)
#define CMD_WITH_SUBCMDS(C, group, desc, opts)                       \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, "", desc, true,                 \
                          options::options_type() | opts)            \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
    std::string params();                                            \
    options::options_type get_options(vector<utf8> const & args);    \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)

// Use this for commands that should specifically _not_ look for an
// _MTN dir and load options from it.

#define CMD_NO_WORKSPACE(C, group, params, desc, opts)               \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, false,            \
                          options::options_type() | opts)            \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)       \

#define ALIAS(C, realcommand)                                        \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, realcommand##_cmd.cmdgroup,            \
                          realcommand##_cmd.params_,                 \
                          realcommand##_cmd.desc_, true,             \
                          realcommand##_cmd.opts)                 \
    {}                                                               \
    virtual std::string desc();                                      \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
std::string commands::cmd_ ## C::desc()                              \
{                                                                    \
  std::string result = _(desc_.c_str());                             \
  result += "\n";                                                    \
  result += (F("Alias for %s") % #realcommand).str();                \
  return result;                                                     \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)       \
{                                                                    \
  process(app, std::string(#realcommand), args);                     \
}

namespace automation {
  struct automate
  {
    std::string name;
    std::string params;
    options::options_type opts;
    automate(std::string const & n, std::string const & p,
             options::options_type const & o);
    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output) const = 0;
    virtual ~automate();
  };
}

#define AUTOMATE(NAME, PARAMS, OPTIONS)                             \
namespace automation {                                              \
  struct auto_ ## NAME : public automate                            \
  {                                                                 \
    auto_ ## NAME ()                                                \
      : automate(#NAME, PARAMS, options::options_type() | OPTIONS)  \
    {}                                                              \
    void run(std::vector<utf8> args, std::string const & help_name, \
                     app_state & app, std::ostream & output) const; \
    virtual ~auto_ ## NAME() {}                                     \
  };                                                                \
  static auto_ ## NAME NAME ## _auto;                               \
}                                                                   \
void automation::auto_ ## NAME :: run(std::vector<utf8> args,       \
                                      std::string const & help_name,\
                                      app_state & app,              \
                                      std::ostream & output) const




// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
