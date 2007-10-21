#ifndef __CMD_HH__
#define __CMD_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <set>

#include "commands.hh"
#include "options.hh"
#include "sanity.hh"

class app_state;

namespace commands
{
  class command
  {
  public:
    typedef std::set< utf8 > names_set;
    typedef std::set< command * > children_set;

  private:
    // NB: these strings are stored _un_translated, because we cannot
    // translate them until after main starts, by which time the
    // command objects have all been constructed.
    utf8 m_primary_name;
    names_set m_names;
    command * m_parent;
    bool m_is_group;
    bool m_hidden;
    utf8 m_params;
    utf8 m_abstract;
    utf8 m_desc;
    bool m_use_workspace_options;
    options::options_type m_opts;
    children_set m_children;
    bool m_allow_completion;

    std::map< command_id, command * >
    find_completions(utf8 const & prefix, command_id const & completed,
                     bool completion_ok = true) const;
    command * find_child_by_name(utf8 const & name) const;

    bool allow_completion() const;
  public:
    command(std::string const & primary_name,
            std::string const & other_names,
            command * parent,
            bool is_group,
            bool hidden,
            std::string const & params,
            std::string const & abstract,
            std::string const & desc,
            bool use_workspace_options,
            options::options_type const & opts,
            bool allow_completion);

    virtual ~command(void);

    command_id ident(void) const;

    utf8 const & primary_name(void) const;
    names_set const & names(void) const;
    void add_alias(const utf8 &new_name);
    command * parent(void) const;
    bool is_group(void) const;
    bool hidden(void) const;
    virtual std::string params(void) const;
    virtual std::string abstract(void) const;
    virtual std::string desc(void) const;
    virtual names_set subcommands(void) const;
    options::options_type const & opts(void) const;
    bool use_workspace_options(void) const;
    children_set & children(void);
    children_set const & children(void) const;
    bool is_leaf(void) const;

    bool operator<(command const & cmd) const;

    virtual void exec(app_state & app,
                      command_id const & execid,
                      args_vector const & args) const = 0;

    bool has_name(utf8 const & name) const;
    command const * find_command(command_id const & id) const;
    command * find_command(command_id const & id);
    std::set< command_id >
      complete_command(command_id const & id,
                       command_id completed = command_id(),
                       bool completion_ok = true) const;
  };

  class automate : public command
  {
    // This function is supposed to be called only after the requirements
    // for "automate" commands have been fulfilled.  This is done by the
    // "exec" function defined below, which implements code shared among
    // all automation commands.  Also, this is also needed by the "stdio"
    // automation, as it executes multiple of these commands sharing the
    // same initialization, hence the friend declaration.
    virtual void exec_from_automate(args_vector args,
                                    command_id const & execid,
                                    app_state & app,
                                    std::ostream & output) const = 0;
    friend class automate_stdio;

  public:
    automate(std::string const & name,
             std::string const & params,
             std::string const & abstract,
             std::string const & desc,
             options::options_type const & opts);

    void exec(app_state & app,
              command_id const & execid,
              args_vector const & args,
              std::ostream & output) const;

    void exec(app_state & app,
              command_id const & execid,
              args_vector const & args) const;
  };
};

inline std::vector<file_path>
args_to_paths(args_vector const & args)
{
  std::vector<file_path> paths;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
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

void
notify_if_multiple_heads(app_state & app);

void
process_commit_message_args(bool & given,
                            utf8 & log_message,
                            app_state & app,
                            utf8 message_prefix = utf8(""));

#define CMD_FWD_DECL(C) \
namespace commands { \
  class cmd_ ## C; \
  extern cmd_ ## C C ## _cmd; \
}

#define CMD_REF(C) ((commands::command *)&(commands::C ## _cmd))

#define _CMD2(C, name, aliases, parent, hidden, params, abstract, desc, opts) \
namespace commands {                                                 \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, false, hidden,      \
                          params, abstract, desc, true,              \
                          options::options_type() | opts, true)      \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args) const;               \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args) const

#define CMD(C, name, aliases, parent, params, abstract, desc, opts) \
  _CMD2(C, name, aliases, parent, false, params, abstract, desc, opts)

#define CMD_HIDDEN(C, name, aliases, parent, params, abstract, desc, opts) \
  _CMD2(C, name, aliases, parent, true, params, abstract, desc, opts)

#define _CMD_GROUP2(C, name, aliases, parent, abstract, desc, cmpl)  \
  namespace commands {                                               \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, true, false, "",    \
                          abstract, desc, true,                      \
                          options::options_type(), cmpl)             \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args) const;               \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args) const       \
{                                                                    \
  I(false);                                                          \
}

#define CMD_GROUP(C, name, aliases, parent, abstract, desc)     \
  _CMD_GROUP2(C, name, aliases, parent, abstract, desc, true)

#define CMD_GROUP_NO_COMPLETE(C, name, aliases, parent, abstract, desc) \
  _CMD_GROUP2(C, name, aliases, parent, abstract, desc, false)

// Use this for commands that should specifically _not_ look for an
// _MTN dir and load options from it.

#define CMD_NO_WORKSPACE(C, name, aliases, parent, params, abstract, \
                         desc, opts)                                 \
namespace commands {                                                 \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, false, false,       \
                          params, abstract, desc, false,             \
                          options::options_type() | opts, true)      \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args) const;               \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args) const

// TODO: 'abstract' and 'desc' should be refactored so that the
// command definition allows the description of input/output format,
// error conditions, version when added, etc.  'desc' can later be
// automatically built from these.
#define CMD_AUTOMATE(C, params, abstract, desc, opts)                \
namespace commands {                                                 \
  class automate_ ## C : public automate                             \
  {                                                                  \
    void exec_from_automate(args_vector args,                        \
                            command_id const & execid,               \
                            app_state & app,                         \
                            std::ostream & output) const;            \
  public:                                                            \
    automate_ ## C() : automate(#C, params, abstract, desc,          \
                                options::options_type() | opts)      \
    {}                                                               \
  };                                                                 \
  automate_ ## C C ## _automate;                                     \
}                                                                    \
void commands::automate_ ## C :: exec_from_automate                  \
  (args_vector args,                                                 \
   command_id const & execid,                                        \
   app_state & app,                                                  \
   std::ostream & output) const

CMD_FWD_DECL(__root__);

CMD_FWD_DECL(automation);
CMD_FWD_DECL(database);
CMD_FWD_DECL(debug);
CMD_FWD_DECL(informative);
CMD_FWD_DECL(key_and_cert);
CMD_FWD_DECL(network);
CMD_FWD_DECL(packet_io);
CMD_FWD_DECL(rcs);
CMD_FWD_DECL(review);
CMD_FWD_DECL(tree);
CMD_FWD_DECL(variables);
CMD_FWD_DECL(workspace);
CMD_FWD_DECL(user);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
