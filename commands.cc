// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2007 Julio M. Merino Vidal <jmmv@NetBSD.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <algorithm>
#include <iostream>

#include "transforms.hh"
#include "simplestring_xform.hh"
#include "file_io.hh"
#include "charset.hh"
#include "diff_patch.hh"
#include "inodeprint.hh"
#include "cert.hh"
#include "ui.hh"
#include "cmd.hh"
#include "constants.hh"
#include "app_state.hh"
#include "project.hh"

#ifndef _WIN32
#include "lexical_cast.hh"
#include <signal.h>
#endif

using std::cin;
using std::make_pair;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::string;
using std::strlen;
using std::vector;

CMD_GROUP(__root__, "__root__", "", NULL, "", "");

//
// Definition of top-level commands, used to classify the real commands
// in logical groups.
//
// These top level commands, while part of the final identifiers and defined
// as regular command groups, are handled separately.  The user should not
// see them except through the help command.
//
// XXX This is to easily maintain compatibilty with older versions.  But
// maybe this should be revised, because exposing the top level category
// (being optional, of course), may not be a bad idea.
//
CMD_GROUP_NO_COMPLETE(automation, "automation", "", CMD_REF(__root__),
                      N_("Commands that aid in scripted execution"),
                      "");
CMD_GROUP(database, "database", "", CMD_REF(__root__),
          N_("Commands that manipulate the database"),
          "");
CMD_GROUP(debug, "debug", "", CMD_REF(__root__),
          N_("Commands that aid in program debugging"),
          "");
CMD_GROUP(informative, "informative", "", CMD_REF(__root__),
          N_("Commands for information retrieval"),
          "");
CMD_GROUP(key_and_cert, "key_and_cert", "", CMD_REF(__root__),
          N_("Commands to manage keys and certificates"),
          "");
CMD_GROUP(network, "network", "", CMD_REF(__root__),
          N_("Commands that access the network"),
          "");
CMD_GROUP(packet_io, "packet_io", "", CMD_REF(__root__),
          N_("Commands for packet reading and writing"),
          "");
CMD_GROUP(rcs, "rcs", "", CMD_REF(__root__),
          N_("Commands for interaction with RCS and CVS"),
          "");
CMD_GROUP(review, "review", "", CMD_REF(__root__),
          N_("Commands to review revisions"),
          "");
CMD_GROUP(tree, "tree", "", CMD_REF(__root__),
          N_("Commands to manipulate the tree"),
          "");
CMD_GROUP(variables, "variables", "", CMD_REF(__root__),
          N_("Commands to manage persistent variables"),
          "");
CMD_GROUP(workspace, "workspace", "", CMD_REF(__root__),
          N_("Commands that deal with the workspace"),
          "");
CMD_GROUP(user, "user", "", CMD_REF(__root__),
          N_("Commands defined by the user"),
          "");

// this file defines the task-oriented "top level" commands which can be
// issued as part of a monotone command line. the command line can only
// have one such command on it, followed by a vector of strings which are its
// arguments. all --options will be processed by the main program *before*
// calling a command
//
// we might expose this blunt command interface to scripting someday. but
// not today.

namespace commands
{
  const char * safe_gettext(const char * msgid)
  {
    if (strlen(msgid) == 0)
      return msgid;

    return _(msgid);
  }

  // This must be a pointer.
  // It's used by the constructor of other static objects in different
  // files (cmd_*.cc), and since they're in different files, there's no
  // guarantee about what order they'll be initialized in. So have this
  // be something that doesn't get automatic initialization, and initialize
  // it ourselves the first time we use it.
  typedef map< command *, command * > relation_map;
  static relation_map * cmds_relation_map = NULL;

  static void init_children(void)
  {
    static bool children_inited = false;

    if (!children_inited)
      {
        children_inited = true;

        for (relation_map::iterator iter = cmds_relation_map->begin();
             iter != cmds_relation_map->end(); iter++)
          {
            if ((*iter).second != NULL)
              (*iter).second->children().insert((*iter).first);
          }
      }
  }
}

//
// Implementation of the commands::command class.
//
namespace commands {
  command::command(std::string const & primary_name,
                   std::string const & other_names,
                   command * parent,
                   bool is_group,
                   bool hidden,
                   std::string const & params,
                   std::string const & abstract,
                   std::string const & desc,
                   bool use_workspace_options,
                   options::options_type const & opts,
                   bool _allow_completion)
    : m_primary_name(utf8(primary_name)),
      m_parent(parent),
      m_is_group(is_group),
      m_hidden(hidden),
      m_params(utf8(params)),
      m_abstract(utf8(abstract)),
      m_desc(utf8(desc)),
      m_use_workspace_options(use_workspace_options),
      m_opts(opts),
      m_allow_completion(_allow_completion)
  {
    // A warning about the parent pointer: commands are defined as global
    // variables, so they are initialized during program startup.  As they
    // are spread over different compilation units, we have no idea of the
    // order in which they will be initialized.  Therefore, accessing
    // *parent from here is dangerous.
    //
    // This is the reason for the cmds_relation_map.  We cannot set up
    // the m_children set until a late stage during program execution.

    if (cmds_relation_map == NULL)
      cmds_relation_map = new relation_map();
    (*cmds_relation_map)[this] = m_parent;

    m_names.insert(m_primary_name);

    vector< utf8 > onv = split_into_words(utf8(other_names));
    m_names.insert(onv.begin(), onv.end());
  }

  command::~command(void)
  {
  }

  bool
  command::allow_completion() const
  {
    return m_allow_completion &&
      (m_parent?m_parent->allow_completion():true);
  }

  command_id
  command::ident(void) const
  {
    I(this != CMD_REF(__root__));

    command_id i;
    
    if (parent() != CMD_REF(__root__))
      i = parent()->ident();
    i.push_back(primary_name());

    I(!i.empty());
    return i;
  }

  const utf8 &
  command::primary_name(void) const
  {
    return m_primary_name;
  }

  const command::names_set &
  command::names(void) const
  {
    return m_names;
  }

  void
  command::add_alias(const utf8 &new_name)
  {
    m_names.insert(new_name);
  }


  command *
  command::parent(void) const
  {
    return m_parent;
  }

  bool
  command::is_group(void) const
  {
    return m_is_group;
  }

  bool
  command::hidden(void) const
  {
    return m_hidden;
  }

  std::string
  command::params() const
  {
    return safe_gettext(m_params().c_str());
  }

  std::string
  command::abstract() const
  {
    return safe_gettext(m_abstract().c_str());
  }

  std::string
  command::desc() const
  {
    return abstract() + ".\n" + safe_gettext(m_desc().c_str());
  }
  
  command::names_set
  command::subcommands(void) const
  {
    names_set set;
    init_children();
    for (children_set::const_iterator i = m_children.begin();
      i != m_children.end(); i++)
      {
        if ((*i)->hidden())
          continue;
        names_set const & other = (*i)->names();
        set.insert(other.begin(), other.end());
      }
    return set;
  }

  options::options_type const &
  command::opts(void) const
  {
    return m_opts;
  }

  bool
  command::use_workspace_options(void) const
  {
    return m_use_workspace_options;
  }

  command::children_set &
  command::children(void)
  {
    init_children();
    return m_children;
  }

  command::children_set const &
  command::children(void) const
  {
    init_children();
    return m_children;
  }

  bool
  command::is_leaf(void) const
  {
    return children().empty();
  }

  bool
  command::operator<(command const & cmd) const
  {
    // *twitch*
    return (parent()->primary_name() < cmd.parent()->primary_name() ||
            ((parent() == cmd.parent()) &&
             primary_name() < cmd.primary_name()));
  }

  bool
  command::has_name(utf8 const & name) const
  {
    return names().find(name) != names().end();
  }

  command const *
  command::find_command(command_id const & id) const
  {
    command const * cmd;

    if (id.empty())
      cmd = this;
    else
      {
        utf8 component = *(id.begin());
        command const * match = find_child_by_name(component);

        if (match != NULL)
          {
            command_id remaining(id.begin() + 1, id.end());
            I(remaining.size() == id.size() - 1);
            cmd = match->find_command(remaining);
          }
        else
          cmd = NULL;
      }

    return cmd;
  }

  command *
  command::find_command(command_id const & id)
  {
    command * cmd;

    if (id.empty())
      cmd = this;
    else
      {
        utf8 component = *(id.begin());
        command * match = find_child_by_name(component);

        if (match != NULL)
          {
            command_id remaining(id.begin() + 1, id.end());
            I(remaining.size() == id.size() - 1);
            cmd = match->find_command(remaining);
          }
        else
          cmd = NULL;
      }

    return cmd;
  }

  map< command_id, command * >
  command::find_completions(utf8 const & prefix, command_id const & completed,
                            bool completion_ok)
    const
  {
    map< command_id, command * > matches;

    I(!prefix().empty());

    for (children_set::const_iterator iter = children().begin();
         iter != children().end(); iter++)
      {
        command * child = *iter;

        for (names_set::const_iterator iter2 = child->names().begin();
             iter2 != child->names().end(); iter2++)
          {
            command_id caux = completed;
            caux.push_back(*iter2);

            // If one of the command names was an exact match,
            // do not try to find other possible completions.
            // This would  eventually hinder us to ever call a command
            // whose name is also the prefix for another command in the
            // same group (f.e. mtn automate cert and mtn automate certs)
            if (prefix == *iter2)
              {
                // since the command children are not sorted, we
                // need to ensure that no other partial completed
                // commands matched
                matches.clear();
                matches[caux] = child;
                return matches;
              }

            if (!child->hidden() &&
                     prefix().length() < (*iter2)().length() &&
                     allow_completion() && completion_ok)
              {
                string temp((*iter2)(), 0, prefix().length());
                utf8 p(temp);
                if (prefix == p)
                  matches[caux] = child;
              }
          }
      }

    return matches;
  }

  set< command_id >
  command::complete_command(command_id const & id,
                            command_id completed,
                            bool completion_ok) const
  {
    I(this != CMD_REF(__root__) || !id.empty());
    I(!id.empty());

    set< command_id > matches;

    utf8 component = *(id.begin());
    command_id remaining(id.begin() + 1, id.end());

    map< command_id, command * >
      m2 = find_completions(component,
                            completed,
                            allow_completion() && completion_ok);
    for (map< command_id, command * >::const_iterator iter = m2.begin();
         iter != m2.end(); iter++)
      {
        command_id const & i2 = (*iter).first;
        command * child = (*iter).second;

        if (child->is_leaf() || remaining.empty())
          matches.insert(i2);
        else
          {
            I(remaining.size() == id.size() - 1);
            command_id caux = completed;
            caux.push_back(i2[i2.size() - 1]);
            set< command_id > maux = child->complete_command(remaining, caux);
            if (maux.empty())
              matches.insert(i2);
            else
              matches.insert(maux.begin(), maux.end());
          }
      }

    return matches;
  }

  command *
  command::find_child_by_name(utf8 const & name) const
  {
    I(!name().empty());

    command * cmd = NULL;

    for (children_set::const_iterator iter = children().begin();
         iter != children().end() && cmd == NULL; iter++)
      {
        command * child = *iter;

        if (child->has_name(name))
          cmd = child;
      }

    return cmd;
  }
};

namespace std
{
  template <>
  struct greater<commands::command *>
  {
    bool operator()(commands::command const * a, commands::command const * b)
    {
      return *a < *b;
    }
  };
};

namespace commands
{
  command_id
  complete_command(args_vector const & args)
  {
    // Handle categories early; no completion allowed.
    if (CMD_REF(__root__)->find_command(make_command_id(args[0]())) != NULL)
      return make_command_id(args[0]());

    command_id id;
    for (args_vector::const_iterator iter = args.begin();
         iter != args.end(); iter++)
      id.push_back(utf8((*iter)()));

    set< command_id > matches;

    command::children_set const & cs = CMD_REF(__root__)->children();
    for (command::children_set::const_iterator iter = cs.begin();
         iter != cs.end(); iter++)
      {
        command const * child = *iter;

        set< command_id > m2 = child->complete_command(id, child->ident());
        matches.insert(m2.begin(), m2.end());
      }

    if (matches.size() >= 2)
      {
        // If there is an exact match at the lowest level, pick it.  Needed
        // to automatically resolve ambiguities between, e.g., 'drop' and
        // 'dropkey'.
        command_id tmp;

        for (set< command_id >::const_iterator iter = matches.begin();
             iter != matches.end() && tmp.empty(); iter++)
          {
            command_id const & id = *iter;
            I(id.size() >= 2);
            if (id[id.size() - 1]() == args[id.size() - 2]())
              tmp = id;
          }

        if (!tmp.empty())
          {
            matches.clear();
            matches.insert(tmp);
          }
      }

    if (matches.empty())
      {
        N(false,
          F("unknown command '%s'") % join_words(id)());
      }
    else if (matches.size() == 1)
      {
        id = *matches.begin();
      }
    else
      {
        I(matches.size() > 1);
        string err =
          (F("'%s' is ambiguous; possible completions are:") %
             join_words(id)()).str();
        for (set< command_id >::const_iterator iter = matches.begin();
             iter != matches.end(); iter++)
          err += '\n' + join_words(*iter)();
        N(false, i18n_format(err));
      }

    I(!id.empty());
    return id;
  }

  static command const *
  find_command(command_id const & ident)
  {
    command const * cmd = CMD_REF(__root__)->find_command(ident);

    // This function is only used internally with an identifier returned
    // by complete_command.  Therefore, it must always exist.
    I(cmd != NULL);

    return cmd;
  }

  // Prints the abstract description of the given command or command group
  // properly indented.  The tag starts at column two.  The description has
  // to start, at the very least, two spaces after the tag's end position;
  // this is given by the colabstract parameter.
  static void describe(const string & tag, const string & abstract,
                       const string & subcommands, size_t colabstract,
                       ostream & out)
  {
    I(colabstract > 0);

    size_t col = 0;
    out << "  " << tag << " ";
    col += display_width(utf8(tag + "   "));

    out << string(colabstract - col, ' ');
    col = colabstract;
    string desc(abstract);
    if (subcommands.size() > 0)
      {
        desc += " (" + subcommands + ')';
      }
    out << format_text(desc, colabstract, col) << '\n';
  }

  static void explain_children(command::children_set const & children,
                               ostream & out)
  {
    I(children.size() > 0);

    vector< command const * > sorted;

    size_t colabstract = 0;
    for (command::children_set::const_iterator i = children.begin();
         i != children.end(); i++)
      {
        command const * child = *i;

        if (child->hidden())
          continue;

        size_t len = display_width(join_words(child->names(), ", ")) +
            display_width(utf8("    "));
        if (colabstract < len)
          colabstract = len;

        sorted.push_back(child);
      }

    sort(sorted.begin(), sorted.end(), std::greater< command * >());

    for (vector< command const * >::const_iterator i = sorted.begin();
         i != sorted.end(); i++)
      {
        command const * child = *i;
        describe(join_words(child->names(), ", ")(), child->abstract(),
                 join_words(child->subcommands(), ", ")(),
                 colabstract, out);
      }
  }

  static void explain_cmd_usage(command_id const & ident, ostream & out)
  {
    I(ident.size() >= 1);

    vector< string > lines;
    command const * cmd = find_command(ident);

    string visibleid = join_words(vector< utf8 >(ident.begin() + 1,
                                                 ident.end()))();

    if (visibleid.empty())
      out << format_text(F("Commands in group '%s':") %
                         join_words(ident)())
          << "\n\n";
    else
      {
        if (cmd->children().size() > 0)
          out << format_text(F("Subcommands of '%s %s':") %
                             ui.prog_name % visibleid)
              << "\n\n";
        else
          out << format_text(F("Syntax specific to '%s %s':") %
                             ui.prog_name % visibleid)
              << "\n\n";
      }

    // Print command parameters.
    string params = cmd->params();
    split_into_lines(params, lines);
    if (lines.size() > 0)
      {
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "  " << visibleid << ' ' << *j << '\n';
        out << '\n';
      }

    // Explain children, if any.
    if (!cmd->is_leaf())
      {
        explain_children(cmd->children(), out);
        out << '\n';
      }

    // Print command description.
    if (visibleid.empty())
      out << format_text(F("Purpose of group '%s':") %
                         join_words(ident)())
          << "\n\n";
    else
      out << format_text(F("Description for '%s %s':") %
                         ui.prog_name % visibleid)
          << "\n\n";
    out << format_text(cmd->desc(), 2) << "\n\n";

    // Print all available aliases.
    if (cmd->names().size() > 1)
      {
        command::names_set othernames = cmd->names();
        othernames.erase(ident[ident.size() - 1]);
        out << format_text(F("Aliases: %s.") %
                           join_words(othernames, ", ")(), 2)
            << '\n';
      }
  }

  command_id make_command_id(std::string const & path)
  {
    return split_into_words(utf8(path));
  }

  void explain_usage(command_id const & ident, ostream & out)
  {
    command const * cmd = find_command(ident);

    if (ident.empty())
      {
        out << format_text(F("Command groups:")) << "\n\n";
        explain_children(CMD_REF(__root__)->children(), out);
        out << '\n'
            << format_text(F("For information on a specific command, type "
                           "'mtn help <command_name> [subcommand_name ...]'."))
            << "\n\n"
            << format_text(F("To see more details about the commands of a "
                           "particular group, type 'mtn help <group_name>'."))
            << "\n\n"
            << format_text(F("Note that you can always abbreviate a command "
                           "name as long as it does not conflict with other "
                           "names."))
            << "\n";
      }
    else
      explain_cmd_usage(ident, out);
  }

  void process(app_state & app, command_id const & ident,
               args_vector const & args)
  {
    command const * cmd = CMD_REF(__root__)->find_command(ident);

    string visibleid = join_words(vector< utf8 >(ident.begin() + 1,
                                                 ident.end()))();

    I(cmd->is_leaf() || cmd->is_group());
    N(!(cmd->is_group() && cmd->parent() == CMD_REF(__root__)),
      F("command '%s' is invalid; it is a group") % join_words(ident));

    N(!(!cmd->is_leaf() && args.empty()),
      F("no subcommand specified for '%s'") % visibleid);

    N(!(!cmd->is_leaf() && !args.empty()),
      F("could not match '%s' to a subcommand of '%s'") %
      join_words(args) % visibleid);

    L(FL("executing command '%s'") % visibleid);

    // at this point we process the data from _MTN/options if
    // the command needs it.
    if (cmd->use_workspace_options())
      app.process_options();

    cmd->exec(app, ident, args);
  }

  options::options_type command_options(command_id const & ident)
  {
    command const * cmd = find_command(ident);
    return cmd->opts();
  }
}
////////////////////////////////////////////////////////////////////////

CMD(help, "help", "", CMD_REF(informative), N_("command [ARGS...]"),
    N_("Displays help about commands and options"),
    "",
    options::opts::none)
{
  if (args.size() < 1)
    {
      app.opts.help = true;
      throw usage(command_id());
    }

  command_id id = commands::complete_command(args);
  app.opts.help = true;
  throw usage(id);
}

CMD_HIDDEN(crash, "crash", "", CMD_REF(debug),
           "{ N | E | I | exception | signal }",
           N_("Triggers the specified kind of crash"),
           "",
           options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);
  bool spoon_exists(false);
  if (idx(args,0)() == "N")
    N(spoon_exists, i18n_format("There is no spoon."));
  else if (idx(args,0)() == "E")
    E(spoon_exists, i18n_format("There is no spoon."));
  else if (idx(args,0)() == "I")
    {
      I(spoon_exists);
    }
#define maybe_throw(ex) if(idx(args,0)()==#ex) throw ex("There is no spoon.")
#define maybe_throw_bare(ex) if(idx(args,0)()==#ex) throw ex()
  else maybe_throw_bare(std::bad_alloc);
  else maybe_throw_bare(std::bad_cast);
  else maybe_throw_bare(std::bad_typeid);
  else maybe_throw_bare(std::bad_exception);
  else maybe_throw_bare(std::exception);
  else maybe_throw(std::domain_error);
  else maybe_throw(std::invalid_argument);
  else maybe_throw(std::length_error);
  else maybe_throw(std::out_of_range);
  else maybe_throw(std::range_error);
  else maybe_throw(std::overflow_error);
  else maybe_throw(std::underflow_error);
  else maybe_throw(std::logic_error);
  else maybe_throw(std::runtime_error);
  else
    {
#ifndef _WIN32
      try
        {
          int signo = boost::lexical_cast<int>(idx(args,0)());
          if (0 < signo && signo <= 15)
            {
              raise(signo);
              // control should not get here...
              I(!"crash: raise returned");
            }
        }
      catch (boost::bad_lexical_cast&)
        { // fall through and throw usage
        }
#endif
      throw usage(execid);
    }
#undef maybe_throw
#undef maybe_throw_bare
}

string
describe_revision(project_t & project, revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += id.inner()();

  // append authors and date of this revision
  vector< revision<cert> > tmp;
  project.get_revision_certs_by_name(id, author_name, tmp);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }
  project.get_revision_certs_by_name(id, date_name, tmp);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }

  return description;
}

void
notify_if_multiple_heads(project_t & project,
                         branch_name const & branchname,
                         bool ignore_suspend_certs)
{
  set<revision_id> heads;
  project.get_branch_heads(branchname, heads, ignore_suspend_certs);
  if (heads.size() > 1) {
    string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider '%s merge'"),
                      prefixedline);
    P(i18n_format(prefixedline) % branchname % ui.prog_name);
  }
}

void
process_commit_message_args(options const & opts,
                            bool & given,
                            utf8 & log_message,
                            utf8 const & message_prefix)
{
  // can't have both a --message and a --message-file ...
  N(!opts.message_given || !opts.msgfile_given,
    F("--message and --message-file are mutually exclusive"));

  if (opts.message_given)
    {
      string msg;
      join_lines(opts.message, msg);
      log_message = utf8(msg);
      if (message_prefix().length() != 0)
        log_message = utf8(message_prefix() + "\n\n" + log_message());
      given = true;
    }
  else if (opts.msgfile_given)
    {
      data dat;
      read_data_for_command_line(opts.msgfile, dat);
      external dat2 = external(dat());
      system_to_utf8(dat2, log_message);
      if (message_prefix().length() != 0)
        log_message = utf8(message_prefix() + "\n\n" + log_message());
      given = true;
    }
  else if (message_prefix().length() != 0)
    {
      log_message = message_prefix;
      given = true;
    }
  else
    given = false;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

CMD_GROUP(top, "top", "", CMD_REF(__root__),
          "", "");
CMD(test, "test", "", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD(test1, "test1", "alias1", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD(test2, "test2", "alias2", CMD_REF(top),
    "", "", "", options::opts::none) {}
CMD_HIDDEN(test3, "test3", "", CMD_REF(top),
           "", "", "", options::opts::none) {}

CMD_GROUP(testg, "testg", "aliasg", CMD_REF(top),
          "", "");
CMD(testg1, "testg1", "", CMD_REF(testg),
    "", "", "", options::opts::none) {}
CMD(testg2, "testg2", "", CMD_REF(testg),
    "", "", "", options::opts::none) {}
CMD_HIDDEN(testg3, "testg3", "", CMD_REF(testg),
           "", "", "", options::opts::none) {}

static args_vector
mkargs(const char *words)
{
  return split_into_words(arg_type(words));
}

UNIT_TEST(commands, make_command_id)
{
  using commands::command_id;
  using commands::make_command_id;

  {
    command_id id = make_command_id("foo");
    UNIT_TEST_CHECK(id.size() == 1);
    UNIT_TEST_CHECK(id[0]() == "foo");
  }

  {
    command_id id = make_command_id("foo bar");
    UNIT_TEST_CHECK(id.size() == 2);
    UNIT_TEST_CHECK(id[0]() == "foo");
    UNIT_TEST_CHECK(id[1]() == "bar");
  }
}

UNIT_TEST(commands, complete_command)
{
  using commands::command_id;
  using commands::complete_command;
  using commands::make_command_id;

  // Single-word identifier, top-level category.
  {
    command_id id = complete_command(mkargs("top"));
    UNIT_TEST_CHECK(id == make_command_id("top"));
  }

  // Single-word identifier.
  {
    command_id id = complete_command(mkargs("testg"));
    UNIT_TEST_CHECK(id == make_command_id("top testg"));
  }

  // Single-word identifier, non-primary name.
  {
    command_id id = complete_command(mkargs("alias1"));
    UNIT_TEST_CHECK(id == make_command_id("top alias1"));
  }

  // Multi-word identifier.
  {
    command_id id = complete_command(mkargs("testg testg1"));
    UNIT_TEST_CHECK(id == make_command_id("top testg testg1"));
  }

  // Multi-word identifier, non-primary names.
  {
    command_id id = complete_command(mkargs("al testg1"));
    UNIT_TEST_CHECK(id == make_command_id("top aliasg testg1"));
  }
}

UNIT_TEST(commands, command_complete_command)
{
  using commands::command_id;
  using commands::make_command_id;

  // Non-existent single-word identifier.
  {
    command_id id = make_command_id("foo");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 0);
  }

  // Non-existent multi-word identifier.
  {
    command_id id = make_command_id("foo bar");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 0);
  }

  // Single-word identifier with one match. Exact matches are found
  // before any possible completions.
  {
    command_id id = make_command_id("test");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);
    UNIT_TEST_CHECK(*matches.begin() == make_command_id("test"));
  }

  // Single-word identifier with one match, non-primary name.
  {
    command_id id = make_command_id("alias1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);
    UNIT_TEST_CHECK(*matches.begin() == make_command_id("alias1"));
  }

  // Single-word identifier with multiple matches. 
  {
    command_id id = make_command_id("tes");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 4);

    set< command_id > expected;
    expected.insert(make_command_id("test"));
    expected.insert(make_command_id("test1"));
    expected.insert(make_command_id("test2"));
    expected.insert(make_command_id("testg"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Single-word identifier with multiple matches, non-primary name.
  {
    command_id id = make_command_id("alias");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 3);

    set< command_id > expected;
    expected.insert(make_command_id("alias1"));
    expected.insert(make_command_id("alias2"));
    expected.insert(make_command_id("aliasg"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match.
  {
    command_id id = make_command_id("testg testg1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches.
  {
    command_id id = make_command_id("testg testg");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 2);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    expected.insert(make_command_id("testg testg2"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches at different levels.
  {
    command_id id = make_command_id("tes testg1");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 4);

    set< command_id > expected;
    expected.insert(make_command_id("test"));
    expected.insert(make_command_id("test1"));
    expected.insert(make_command_id("test2"));
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match and extra words.
  {
    command_id id = make_command_id("testg testg1 foo");
    set< command_id > matches = CMD_REF(top)->complete_command(id);
    UNIT_TEST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(make_command_id("testg testg1"));
    UNIT_TEST_CHECK(matches == expected);
  }
}

UNIT_TEST(commands, command_find_command)
{
  using commands::command;
  using commands::command_id;
  using commands::make_command_id;

  // Non-existent single-word identifier.
  {
    command_id id = make_command_id("foo");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Non-existent multi-word identifier.
  {
    command_id id = make_command_id("foo bar");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Single-word identifier that could be completed.
  {
    command_id id = make_command_id("tes");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Single-word identifier.
  {
    command_id id = make_command_id("test1");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(test1));
  }

  // Hidden single-word identifier.
  {
    command_id id = make_command_id("test3");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(test3));
  }

  // Multi-word identifier that could be completed.
  {
    command_id id = make_command_id("testg testg");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }

  // Multi-word identifier.
  {
    command_id id = make_command_id("testg testg1");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(testg1));
  }

  // Hidden multi-word identifier.
  {
    command_id id = make_command_id("testg testg3");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == CMD_REF(testg3));
  }

  // Multi-word identifier with extra words.
  {
    command_id id = make_command_id("testg testg1 foo");
    command const * cmd = CMD_REF(top)->find_command(id);
    UNIT_TEST_CHECK(cmd == NULL);
  }
}
#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
