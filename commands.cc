// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
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

  using std::map;
  // This must be a pointer.
  // It's used by the constructor of other static objects in different
  // files (cmd_*.cc), and since they're in different files, there's no
  // guarantee about what order they'll be initialized in. So have this
  // be something that doesn't get automatic initialization, and initialize
  // it ourselves the first time we use it.
  static map<string, command *> * cmds;
  command::command(string const & n,
                   string const & g,
                   string const & p,
                   string const & a,
                   string const & d,
                   bool u,
                   options::options_type const & o)
    : name(n), cmdgroup(g), params_(p), abstract_(a), desc_(d),
      use_workspace_options(u), opts(o)
  {
    if (cmds == NULL)
      cmds = new map<string, command *>;
    (*cmds)[n] = this;
  }
  command::~command() {}
  std::string command::params() {return safe_gettext(params_.c_str());}
  std::string command::abstract() {return safe_gettext(abstract_.c_str());}
  std::string command::desc() {return safe_gettext(desc_.c_str());}
  options::options_type command::get_options(vector<utf8> const & args)
  {
    return opts;
  }
  bool operator<(command const & self, command const & other);
  std::string const & hidden_group()
  {
    static const std::string the_hidden_group("");
    return the_hidden_group;
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
  using std::greater;
  using std::ostream;

  static map<string, string> cmdgroups;

  static void init_cmdgroups(void)
  {
    if (cmdgroups.empty())
      {
#define insert(id, abstract) \
        cmdgroups.insert(map<string, string>::value_type(id, abstract));

        insert("automation", N_("Commands that aid in scripted execution"));
        insert("database", N_("Commands that manipulate the database"));
        insert("debug", N_("Commands that aid in program debugging"));
        insert("informative", N_("Commands for information retrieval"));
        insert("key and cert", N_("Commands to manage keys and certificates"));
        insert("network", N_("Commands that access the network"));
        insert("packet i/o", N_("Commands for packet reading and writing"));
        insert("rcs", N_("Commands for interaction with RCS and CVS"));
        insert("review", N_("Commands to review revisions"));
        insert("tree", N_("Commands to manipulate the tree"));
        insert("vars", N_("Commands to manage persistent variables"));
        insert("workspace", N_("Commands that deal with the workspace"));

#undef insert
      }

    assert(!cmdgroups.empty());
  }

  bool operator<(command const & self, command const & other)
  {
    // *twitch*
    return ((string(_(self.cmdgroup.c_str())) < string(_(other.cmdgroup.c_str())))
            || ((self.cmdgroup == other.cmdgroup)
                && (string(_(self.name.c_str())) < (string(_(other.name.c_str()))))));
  }


  string complete_command(string const & cmd)
  {
    if (cmd.length() == 0 || (*cmds).find(cmd) != (*cmds).end()) return cmd;

    L(FL("expanding command '%s'") % cmd);

    vector<string> matched;

    for (map<string,command *>::const_iterator i = (*cmds).begin();
         i != (*cmds).end(); ++i)
      {
        if (cmd.length() < i->first.length())
          {
            string prefix(i->first, 0, cmd.length());
            if (cmd == prefix) matched.push_back(i->first);
          }
      }

    // no matched commands
    if (matched.size() == 0)
      return "";

    // one matched command
    if (matched.size() == 1)
      {
        string completed = *matched.begin();
        L(FL("expanded command to '%s'") %  completed);
        return completed;
      }

    // more than one matched command
    string err = (F("command '%s' has multiple ambiguous expansions:") % cmd).str();
    for (vector<string>::iterator i = matched.begin();
         i != matched.end(); ++i)
      err += ('\n' + *i);
    W(i18n_format(err));
    return cmd;
  }

  string complete_command_group(string const & cmdgroup)
  {
    init_cmdgroups();

    if (cmdgroup.length() == 0 || cmdgroups.find(cmdgroup) != cmdgroups.end())
      return cmdgroup;

    L(FL("expanding command group '%s'") % cmdgroup);

    vector<string> matched;

    for (map<string, string>::const_iterator i = cmdgroups.begin();
         i != cmdgroups.end(); ++i)
      {
        if (cmdgroup.length() < i->first.length())
          {
            string prefix(i->first, 0, cmdgroup.length());
            if (cmdgroup == prefix)
              matched.push_back(i->first);
          }
      }

    // no matched commands
    if (matched.size() == 0)
      return "";

    // one matched command
    if (matched.size() == 1)
      {
        string completed = *matched.begin();
        L(FL("expanded command group to '%s'") % completed);
        return completed;
      }

    // more than one matched command
    string err = (F("command group '%s' has multiple ambiguous "
                    "expansions:") % cmdgroup).str();
    for (vector<string>::iterator i = matched.begin();
         i != matched.end(); ++i)
      err += ('\n' + *i);
    W(i18n_format(err));

    return cmdgroup;
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

    vector<string> words;
    split_into_words(abstract, words);

    const size_t maxcol = terminal_width();
    for (vector<string>::const_iterator i = words.begin();
         i != words.end(); i++) {
        string const & word = *i;

        if (col + word.length() + 1 >= maxcol) {
          out << std::endl;
          col = 0;
          while (col++ < colabstract - 1)
            out << ' ';
        }

        out << ' ' << word;
        col += word.length() + 1;
    }
    out << std::endl;
  }

  static void explain_cmdgroups(ostream & out )
  {
    size_t colabstract = 0;
    for (map<string, string>::const_iterator i = cmdgroups.begin();
         i != cmdgroups.end(); i++)
      {
        string const & name = (*i).first;

        size_t len = display_width(utf8(name + "    "));
        if (colabstract < len)
          colabstract = len;
      }

    out << "Command groups:" << std::endl << std::endl;
    for (map<string, string>::const_iterator i = cmdgroups.begin();
         i != cmdgroups.end(); i++)
      {
        string const & name = (*i).first;
        string const & abstract = (*i).second;

        describe(name, abstract, colabstract, out);
      }
  }

  static void explain_cmdgroup_usage(string const & cmdgroup, ostream & out)
  {
    init_cmdgroups();

    map<string, string>::const_iterator grpi = cmdgroups.find(cmdgroup);
    assert(grpi != cmdgroups.end());

    size_t colabstract = 0;
    vector<command *> sorted;
    for (map<string, command *>::const_iterator i = (*cmds).begin();
         i != (*cmds).end(); ++i)
      {
        if (i->second->cmdgroup == cmdgroup)
          {
            sorted.push_back(i->second);

            size_t len = display_width(utf8(i->second->name + "    "));
            if (colabstract < len)
              colabstract = len;
          }
      }

    sort(sorted.begin(), sorted.end(), greater<command *>());

    out << (*grpi).second << ":" << std::endl;
    for (size_t i = 0; i < sorted.size(); ++i)
      {
        string const & name = idx(sorted, i)->name;
        string const & abstract = idx(sorted, i)->abstract();

        describe(name, abstract, colabstract, out);
      }
  }

  static void explain_cmd_usage(string const & cmd, ostream & out)
  {
    map<string, command *>::const_iterator i;

    i = (*cmds).find(cmd);
    assert(i != (*cmds).end());

    out << F(safe_gettext("Syntax specific to 'mtn %s':")) % cmd
        << std::endl << std::endl;
    string params = i->second->params();
    vector<string> lines;
    split_into_lines(params, lines);
    for (vector<string>::const_iterator j = lines.begin();
         j != lines.end(); ++j)
      out << "  " << i->second->name << ' ' << *j << std::endl;
    split_into_lines(i->second->desc(), lines);
    for (vector<string>::const_iterator j = lines.begin();
         j != lines.end(); ++j)
      {
        describe("", *j, 4, out);
        out << std::endl;
      }
  }

  void explain_usage(string const & cmd, ostream & out)
  {
    init_cmdgroups();

    map<string, command *>::const_iterator cmditer;
    map<string, string>::const_iterator cmdgroupiter;

    cmditer = (*cmds).find(cmd);
    cmdgroupiter = cmdgroups.find(cmd);

    if (cmditer != (*cmds).end())
      explain_cmd_usage(cmd, out);
    else if (cmdgroupiter != cmdgroups.end())
      {
        explain_cmdgroup_usage(cmd, out);
        out << std::endl;
        out << "For information on a specific command, type "
               "'mtn help <command_name>'." << std::endl;
        out << std::endl;
      }
    else
      {
        assert(cmd.empty());

        explain_cmdgroups(out);
        out << std::endl;
        out << "To see what commands are available in a group, type "
               "'mtn help <group_name>'." << std::endl;
        out << "For information on a specific command, type "
               "'mtn help <command_name>'." << std::endl;
        out << std::endl;
      }
  }

  int process(app_state & app, string const & cmd, vector<utf8> const & args)
  {
    if ((*cmds).find(cmd) != (*cmds).end())
      {
        L(FL("executing command '%s'") % cmd);

        // at this point we process the data from _MTN/options if
        // the command needs it.
        if ((*cmds)[cmd]->use_workspace_options)
          app.process_options();

        (*cmds)[cmd]->exec(app, args);
        return 0;
      }
    else
      {
        P(F("unknown command '%s'") % cmd);
        return 1;
      }
  }

  options::options_type command_options(vector<utf8> const & cmdline)
  {
    if (cmdline.empty())
      return options::options_type();
    string cmd = complete_command(idx(cmdline,0)());
    if ((*cmds).find(cmd) != (*cmds).end())
      {
        return (*cmds)[cmd]->get_options(cmdline);
      }
    else
      {
        N(!cmd.empty(),
          F("unknown command '%s'") % cmd);
        return options::options_type();
      }
  }

  options::options_type toplevel_command_options(string const & cmd)
  {
    if ((*cmds).find(cmd) != (*cmds).end())
      {
        return (*cmds)[cmd]->opts;
      }
    else
      {
        return options::options_type();
      }
  }
}
////////////////////////////////////////////////////////////////////////

CMD(help, N_("informative"), N_("command [ARGS...]"),
    N_("Displays help about commands and options"),
    N_("display command help"),
    options::opts::none)
{
  if (args.size() < 1)
    {
      app.opts.help = true;
      throw usage("");
    }

  string full_cmd = complete_command(idx(args, 0)());
  string full_cmdgroup = complete_command_group(idx(args, 0)());

  if (cmdgroups.find(full_cmdgroup) != cmdgroups.end())
    {
      app.opts.help = true;
      throw usage(full_cmdgroup);
    }
  else if ((*cmds).find(full_cmd) != (*cmds).end())
    {
      app.opts.help = true;
      throw usage(full_cmd);
    }
  else
    {
      // No matched commands or command groups
      N(!full_cmd.empty() && full_cmdgroup.empty(),
        F("unknown command or command group '%s'") % idx(args, 0)());
      throw usage("");
    }
}

CMD(crash, hidden_group(), "{ N | E | I | exception | signal }",
    "Triggers the specified kind of crash",
    "trigger the specified kind of crash",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(name);
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
      throw usage(name);
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
      std::string msg;
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
