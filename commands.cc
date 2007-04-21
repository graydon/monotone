// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2007 Julio M. Merino Vidal <jmmv@NetBSD.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cassert>
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

#ifndef _WIN32
#include <boost/lexical_cast.hpp>
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

CMD_GROUP(__root__, "", NULL, N_(""), N_(""), options::opts::none);

//
// Definition of top-level commands, used to classify the real commands
// in logical groups.
//
CMD_GROUP(automation, "", CMD_REF(__root__),
          N_("Commands that aid in scripted execution"),
          N_(""),
          options::opts::none);
CMD_GROUP(database, "", CMD_REF(__root__),
          N_("Commands that manipulate the database"),
          N_(""),
          options::opts::none);
CMD_GROUP(debug, "", CMD_REF(__root__),
          N_("Commands that aid in program debugging"),
          N_(""),
          options::opts::none);
CMD_GROUP(informative, "", CMD_REF(__root__),
          N_("Commands for information retrieval"),
          N_(""),
          options::opts::none);
CMD_GROUP(key_and_cert, "", CMD_REF(__root__),
          N_("Commands to manage keys and certificates"),
          N_(""),
          options::opts::none);
CMD_GROUP(network, "", CMD_REF(__root__),
          N_("Commands that access the network"),
          N_(""),
          options::opts::none);
CMD_GROUP(packet_io, "", CMD_REF(__root__),
          N_("Commands for packet reading and writing"),
          N_(""),
          options::opts::none);
CMD_GROUP(rcs, "", CMD_REF(__root__),
          N_("Commands for interaction with RCS and CVS"),
          N_(""),
          options::opts::none);
CMD_GROUP(review, "", CMD_REF(__root__),
          N_("Commands to review revisions"),
          N_(""),
          options::opts::none);
CMD_GROUP(tree, "", CMD_REF(__root__),
          N_("Commands to manipulate the tree"),
          N_(""),
          options::opts::none);
CMD_GROUP(variables, "", CMD_REF(__root__),
          N_("Commands to manage persistent variables"),
          N_(""),
          options::opts::none);
CMD_GROUP(workspace, "", CMD_REF(__root__),
          N_("Commands that deal with the workspace"),
          N_(""),
          options::opts::none);

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
                   bool hidden,
                   std::string const & params,
                   std::string const & abstract,
                   std::string const & desc,
                   bool use_workspace_options,
                   options::options_type const & opts)
    : m_primary_name(utf8(primary_name)),
      m_parent(parent),
      m_hidden(hidden),
      m_params(utf8(params)),
      m_abstract(utf8(abstract)),
      m_desc(utf8(desc)),
      m_use_workspace_options(use_workspace_options),
      m_opts(opts)
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

  command::~command()
  {
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

  command *
  command::parent(void) const
  {
    return m_parent;
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

  std::set< command * >
  command::find_completions(utf8 const & prefix)
  {
    std::set< command * > matches;

    I(!prefix().empty());

    for (children_set::iterator iter = children().begin();
         iter != children().end(); iter++)
      {
        command * child = *iter;
        if (child->hidden())
          continue;

        for (names_set::const_iterator iter2 = child->names().begin();
             iter2 != child->names().end(); iter2++)
          {
            if (prefix == *iter2)
              matches.insert(child);
            else if (prefix().length() < (*iter2)().length())
              {
                utf8 p(string((*iter2)(), 0, prefix().length()));
                if (prefix == p)
                  matches.insert(child);
              }
          }
      }

    return matches;
  }

  set< command_id >
  command::complete_command(command_id const & id)
  {
    I(this != CMD_REF(__root__) || !id.empty());

    set< command_id > matches;

    if (id.empty())
      matches.insert(ident());
    else
      {
        utf8 component = *(id.begin());
        command_id remaining(id.begin() + 1, id.end());

        set< command * > m2 = find_completions(component);
        for (set< command * >::const_iterator iter = m2.begin();
             iter != m2.end(); iter++)
          {
            if ((*iter)->is_leaf())
              matches.insert((*iter)->ident());
            else
              {
                I(remaining.size() == id.size() - 1);
                set< command_id > maux = (*iter)->complete_command(remaining);
                if (maux.empty())
                  matches.insert((*iter)->ident());
                else
                  matches.insert(maux.begin(), maux.end());
              }
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
    command_id id;
    for (args_vector::const_iterator iter = args.begin();
         iter != args.end(); iter++)
      id.push_back(utf8((*iter)()));

    set< command_id > matches = CMD_REF(__root__)->complete_command(id);

    if (matches.empty())
      {
        N(false,
          F("could not match '%s' to any command") % join_words(id)());
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

  static command *
  find_command(command_id const & ident)
  {
    command * cmd = CMD_REF(__root__)->find_command(ident);

    // This function is only used internally with an identifier returned
    // by complete_command.  Therefore, it must always exist.
    I(cmd != NULL);

    return cmd;
  }

  static string format_command_path(command const * cmd)
  {
    string path;
    /*

    if (cmd->parent() == NULL)
      path = cmd->primary_name();
    else
      {
        command const * cmdparent = cmd->parent();
        I(cmdparent != NULL);

        string const & name = cmd->primary_name();
        path = format_command_path(cmdparent) + " " + name;
      }
    */

    return path;
  }

  // Prints the abstract description of the given command or command group
  // properly indented.  The tag starts at column two.  The description has
  // to start, at the very least, two spaces after the tag's end position;
  // this is given by the colabstract parameter.
  static void describe(const string & tag, const string & abstract,
                       size_t colabstract, ostream & out)
  {
    // The algorithm below avoids printing an space on entry (note that
    // there are two before the tag but just one after it) and considers
    // that the colabstract is always one unit less than that given on
    // entry because it always prints a single space before each word.
    assert(colabstract > 0);

    size_t col = 0;
    out << "  " << tag << " ";
    col += display_width(utf8(tag + "   "));

    while (col++ < colabstract - 1)
      out << ' ';
    col = colabstract - 1;

    vector< utf8 > words = split_into_words(utf8(abstract));

    const size_t maxcol = terminal_width();
    vector< utf8 >::const_iterator i = words.begin();
    while (i != words.end())
      {
        string const & word = (*i)();

        if (col + word.length() + 1 >= maxcol)
          {
            out << '\n';
            col = 0;

            // Skip empty words at the beginning of the line so that they do
            // not mess with indentation.  These "words" appear because we
            // put two spaces between sentences, and one of these is
            // transformed into a word.
            //
            // Another approach could be to simply omit these words (by
            // modifying split_into_words) and then add this formatting (two
            // spaces between sentences) from this algorithm.  But then,
            // other kinds of formatting could not be allowed in the original
            // strings... (i.e., they'd disappear after we mangled them).
            if (word == "")
              {
                do
                  i++;
                while (i != words.end() && (*i)().empty());
                if (i == words.end())
                  break;
                else
                  {
                    while (col++ < colabstract - 1)
                      out << ' ';
                    continue;
                  }
              }

            while (col++ < colabstract - 1)
              out << ' ';
          }

        out << ' ' << word;
        col += word.length() + 1;
        i++;
      }
    out << '\n';
  }

  static void explain_children(command::children_set const & children,
                               ostream & out)
  {
    I(children.size() > 0);

    vector< command * > sorted;

    size_t colabstract = 0;
    for (command::children_set::const_iterator i = children.begin();
         i != children.end(); i++)
      {
        size_t len = display_width(join_words((*i)->names())) +
            display_width(utf8("    "));
        if (colabstract < len)
          colabstract = len;

        sorted.push_back(*i);
      }

    sort(sorted.begin(), sorted.end(), std::greater< command * >());

    for (vector< command * >::const_iterator i = sorted.begin();
         i != sorted.end(); i++)
      describe(join_words((*i)->names())(), (*i)->abstract(), colabstract,
               out);
  }

  static void explain_cmd_usage(string const & name, ostream & out)
  {
  /*
    command * cmd = find_command(name); // XXX Should be const.
    assert(cmd != NULL);

    vector< string > lines;

    // XXX Use ui.prog_name instead of hardcoding 'mtn'.
    if (cmd->children().size() > 0)
      out << F(safe_gettext("Subcommands for 'mtn %s':")) %
             format_command_path(cmd) << "\n\n";
    else
      out << F(safe_gettext("Syntax specific to 'mtn %s':")) %
             format_command_path(cmd) << "\n\n";

    // Print command parameters.
    string params = cmd->params();
    split_into_lines(params, lines);
    if (lines.size() > 0)
      {
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "  " << name << ' ' << *j << '\n';
        out << '\n';
      }

    if (cmd->children().size() > 0)
      {
        explain_children(cmd->children(), out);
        out << '\n';
      }

    split_into_lines(cmd->desc(), lines);
    for (vector<string>::const_iterator j = lines.begin();
         j != lines.end(); ++j)
      {
        describe("", *j, 4, out);
        out << '\n';
      }

    if (cmd->names().size() > 1)
      {
        command::names_set othernames = cmd->names();
        othernames.erase(name);
        describe("", "Aliases: " + format_names(othernames) + ".", 4, out);
        out << '\n';
      }
    */
  }

  void explain_usage(command_id const & ident, ostream & out)
  {
    if (CMD_REF(__root__)->find_command(ident) != CMD_REF(__root__))
      explain_cmd_usage("", out); // XXX
    else
      {
        I(ident.empty());

        // TODO Wrap long lines in these messages.
        out << "Top-level commands:\n\n";
        explain_children(CMD_REF(__root__)->children(), out);
        out << '\n';
        out << "For information on a specific command, type "
               "'mtn help <command_name>'.\n";
        out << "Note that you can always abbreviate a command name as "
               "long as it does not conflict with other names.\n";
        out << '\n';
      }
  }

  // XXX Must go away.
  int process(app_state & app, string const & cmd,
              args_vector const & args)
  {
    args_vector a1;
    a1.push_back(arg_type("merge_into_dir"));
    command_id ident;
    ident = commands::complete_command(a1);
    return process(app, ident, args);
  }

  int process(app_state & app, command_id const & ident,
              args_vector const & args)
  {
    command * cmd = CMD_REF(__root__)->find_command(ident);
    I(cmd->is_leaf());
    if (cmd != NULL)
      {
        L(FL("executing command '%s'") % join_words(ident));

        // at this point we process the data from _MTN/options if
        // the command needs it.
        if (cmd->use_workspace_options())
          app.process_options();

        cmd->exec(app, join_words(ident)(), args); // XXX
        return 0;
      }
    else
      {
        P(F("unknown command '%s'") % join_words(ident));
        return 1;
      }
  }

  options::options_type command_options(args_vector const & cmdline)
  {
/*
    if (cmdline.empty())
      return options::options_type();
    string name = complete_command(idx(cmdline,0)());
    if (!name.empty())
      {
        return find_command(name)->opts();
      }
    else
      {
        N(!name.empty(),
          F("unknown command '%s'") % idx(cmdline, 0));
        return options::options_type();
      }
*/
    return options::options_type();
  }

  options::options_type toplevel_command_options(string const & name)
  {
    command * cmd = NULL; // XXX find_command(name);
    if (cmd != NULL)
      {
        return cmd->opts();
      }
    else
      {
        return options::options_type();
      }
  }
}
////////////////////////////////////////////////////////////////////////

CMD(help, "", CMD_REF(informative), N_("command [ARGS...]"),
    N_("Displays help about commands and options"),
    N_(""),
    options::opts::none)
{
  if (args.size() < 1)
    {
      app.opts.help = true;
      throw usage(command_id());
    }

/*
  vector< string > fooargs,rest;
  for (vector< utf8 >::const_iterator i = args.begin(); i != args.end(); i++)
    fooargs.push_back((*i)());
  command & cmd = commands::find_command(fooargs, rest);

  N(!rest.empty(),
    F("could not match any command given '%s'; failed after '%s'") %
      join_words(fooargs)() % join_words(rest)());

  app.opts.help = true;
  throw usage(fooargs);
*/

/*
  if (find_command(full_cmd) != NULL)
    {
      app.opts.help = true;
      throw usage(full_cmd);
    }
  else
    {
      // No matched commands or command groups
      N(!full_cmd.empty(),
        F("unknown command '%s'") % idx(args, 0)());
      throw usage("");
    }
*/
}

CMD_HIDDEN(crash, "", CMD_REF(__root__), "{ N | E | I | exception | signal }",
           N_("Triggers the specified kind of crash"),
           N_(""),
           options::opts::none)
{
  if (args.size() != 1)
    throw usage(ident());
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
      throw usage(ident());
    }
#undef maybe_throw
#undef maybe_throw_bare
}

string
describe_revision(app_state & app,
                  revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += id.inner()();

  // append authors and date of this revision
  vector< revision<cert> > tmp;
  app.get_project().get_revision_certs_by_name(id, author_name, tmp);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }
  app.get_project().get_revision_certs_by_name(id, date_name, tmp);
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
complete(app_state & app,
         string const & str,
         set<revision_id> & completion,
         bool must_exist)
{
  // This copies the start of selectors::parse_selector().to avoid
  // getting a log when there's no expansion happening...:
  //
  // this rule should always be enabled, even if the user specifies
  // --norc: if you provide a revision id, you get a revision id.
  if (str.find_first_not_of(constants::legal_id_bytes) == string::npos
      && str.size() == constants::idlen)
    {
      completion.insert(revision_id(hexenc<id>(id(str))));
      if (must_exist)
        N(app.db.revision_exists(*completion.begin()),
          F("no such revision '%s'") % *completion.begin());
      return;
    }

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(str, app));

  P(F("expanding selection '%s'") % str);

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  N(completions.size() != 0,
    F("no match for selection '%s'") % str);

  for (set<string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    {
      pair<set<revision_id>::const_iterator, bool> p =
        completion.insert(revision_id(hexenc<id>(id(*i))));
      P(F("expanded to '%s'") % *(p.first));
    }
}


void
complete(app_state & app,
         string const & str,
         revision_id & completion,
         bool must_exist)
{
  set<revision_id> completions;

  complete(app, str, completions, must_exist);

  if (completions.size() > 1)
    {
      string err = (F("selection '%s' has multiple ambiguous expansions:") % str).str();
      for (set<revision_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += ("\n" + describe_revision(app, *i));
      N(completions.size() == 1, i18n_format(err));
    }

  completion = *completions.begin();
}

void
notify_if_multiple_heads(app_state & app)
{
  set<revision_id> heads;
  app.get_project().get_branch_heads(app.opts.branchname, heads);
  if (heads.size() > 1) {
    string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider '%s merge'"),
                      prefixedline);
    P(i18n_format(prefixedline) % app.opts.branchname % ui.prog_name);
  }
}

void
process_commit_message_args(bool & given,
                            utf8 & log_message,
                            app_state & app,
                            utf8 message_prefix)
{
  // can't have both a --message and a --message-file ...
  N(!app.opts.message_given || !app.opts.msgfile_given,
    F("--message and --message-file are mutually exclusive"));

  if (app.opts.message_given)
    {
      string msg;
      join_lines(app.opts.message, msg);
      log_message = utf8(msg);
      if (message_prefix().length() != 0)
        log_message = utf8(message_prefix() + "\n\n" + log_message());
      given = true;
    }
  else if (app.opts.msgfile_given)
    {
      data dat;
      read_data_for_command_line(app.opts.msgfile, dat);
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

CMD(test1, "", CMD_REF(__root__), "", "", "", options::opts::none) {}
CMD(test2, "", CMD_REF(__root__), "", "", "", options::opts::none) {}
CMD_HIDDEN(test3, "", CMD_REF(__root__), "", "", "", options::opts::none) {}

CMD_GROUP(testg, "", CMD_REF(__root__), "", "", options::opts::none);
CMD(testg1, "", CMD_REF(testg), "", "", "", options::opts::none) {}
CMD(testg2, "", CMD_REF(testg), "", "", "", options::opts::none) {}
CMD_HIDDEN(testg3, "", CMD_REF(testg), "", "", "", options::opts::none) {}

static commands::command_id
mkid(const char *path)
{
  return split_into_words(utf8(path));
}

UNIT_TEST(commands, command_complete_command)
{
  using commands::command_id;

  // Non-existent single-word identifier.
  {
    command_id id = mkid("foo");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 0);
  }

  // Non-existent multi-word identifier.
  {
    command_id id = mkid("foo bar");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 0);
  }

  // Single-word identifier with one match.
  {
    command_id id = mkid("test1");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 1);
    BOOST_CHECK(*matches.begin() == mkid("test1"));
  }

  // Single-word identifier with multiple matches.
  {
    command_id id = mkid("test");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 3);

    set< command_id > expected;
    expected.insert(mkid("test1"));
    expected.insert(mkid("test2"));
    expected.insert(mkid("testg"));
    BOOST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match.
  {
    command_id id = mkid("testg testg1");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(mkid("testg testg1"));
    BOOST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches.
  {
    command_id id = mkid("testg testg");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 2);

    set< command_id > expected;
    expected.insert(mkid("testg testg1"));
    expected.insert(mkid("testg testg2"));
    BOOST_CHECK(matches == expected);
  }

  // Multi-word identifier with multiple matches at different levels.
  {
    command_id id = mkid("test testg1");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 3);

    set< command_id > expected;
    expected.insert(mkid("test1"));
    expected.insert(mkid("test2"));
    expected.insert(mkid("testg testg1"));
    BOOST_CHECK(matches == expected);
  }

  // Multi-word identifier with one match and extra words.
  {
    command_id id = mkid("testg testg1 foo");
    set< command_id > matches = CMD_REF(__root__)->complete_command(id);
    BOOST_REQUIRE(matches.size() == 1);

    set< command_id > expected;
    expected.insert(mkid("testg testg1"));
    BOOST_CHECK(matches == expected);
  }
}

UNIT_TEST(commands, command_find_command)
{
  using commands::command;
  using commands::command_id;

  // Non-existent single-word identifier.
  {
    command_id id = mkid("foo");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == NULL);
  }

  // Non-existent multi-word identifier.
  {
    command_id id = mkid("foo bar");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == NULL);
  }

  // Single-word identifier that could be completed.
  {
    command_id id = mkid("test");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == NULL);
  }

  // Single-word identifier.
  {
    command_id id = mkid("test1");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == CMD_REF(test1));
  }

  // Hidden single-word identifier.
  {
    command_id id = mkid("test3");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == CMD_REF(test3));
  }

  // Multi-word identifier that could be completed.
  {
    command_id id = mkid("testg testg");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == NULL);
  }

  // Multi-word identifier.
  {
    command_id id = mkid("testg testg1");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == CMD_REF(testg1));
  }

  // Hidden multi-word identifier.
  {
    command_id id = mkid("testg testg3");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == CMD_REF(testg3));
  }

  // Multi-word identifier with extra words.
  {
    command_id id = mkid("testg testg1 foo");
    command * cmd = CMD_REF(__root__)->find_command(id);
    BOOST_CHECK(cmd == NULL);
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
