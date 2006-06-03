#ifndef __CMD_HH__
#define __CMD_HH__

#include "sanity.hh"
#include "options.hh"
#include "constants.hh"
#include "app_state.hh"

#include "commands.hh"

namespace commands
{
  struct no_opts {};
  struct command_opts
  {
    std::set<int> opts;
    command_opts() {}
    command_opts & operator%(int o)
    { opts.insert(o); return *this; }
    command_opts & operator%(no_opts o)
    { return *this; }
    command_opts & operator%(command_opts const &o)
    { opts.insert(o.opts.begin(), o.opts.end()); return *this; }
  };
  extern const no_opts OPT_NONE;

  struct command
  {
    // NB: these strings are stred _un_translated
    // because we cannot translate them until after main starts, by which time
    // the command objects have all been constructed.
    std::string name;
    std::string cmdgroup;
    std::string params;
    std::string desc;
    bool use_workspace_options;
    command_opts options;
    command(std::string const & n,
            std::string const & g,
            std::string const & p,
            std::string const & d,
            bool u,
            command_opts const & o);
    virtual ~command();
    virtual void exec(app_state & app, std::vector<utf8> const & args) = 0;
  };
};

std::string
get_stdin();

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
      std::string err = (F("partial id '%s' has multiple ambiguous expansions:\n") % str).str();
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
maybe_update_inodeprints(app_state & app);

void
notify_if_multiple_heads(app_state & app);

void
process_commit_message_args(bool & given,
                            std::string & log_message,
                            app_state & app);

#define CMD(C, group, params, desc, opts)                            \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, true,             \
                          command_opts() % opts)                     \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)       \

// use this for commands that should specifically _not_ look for an _MTN dir
// and load options from it
#define CMD_NO_WORKSPACE(C, group, params, desc, opts)               \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, false,            \
                          command_opts() % opts)                     \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)       \

#define ALIAS(C, realcommand)                                        \
CMD(C, realcommand##_cmd.cmdgroup, realcommand##_cmd.params,         \
    realcommand##_cmd.desc + "\nAlias for " #realcommand,            \
    realcommand##_cmd.options)                                       \
{                                                                    \
  process(app, std::string(#realcommand), args);                          \
}

#endif
