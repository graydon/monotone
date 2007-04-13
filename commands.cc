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
                   string const & aliases,
                   string const & g,
                   string const & p,
                   string const & a,
                   string const & d,
                   bool u,
                   options::options_type const & o)
    : cmdgroup(g), params_(p), abstract_(a), desc_(d),
      use_workspace_options(u), opts(o)
  {
    if (cmds == NULL)
      cmds = new map< string, command * >;

    names.insert(n);
    (*cmds)[n] = this;

    std::vector< std::string > as;
    split_into_words(aliases, as);
    for (std::vector< std::string >::const_iterator iter = as.begin();
         iter != as.end(); iter++)
      {
        names.insert(*iter);
        (*cmds)[*iter] = this;
      }
  }
  command::~command() {}
  std::string command::params() {return safe_gettext(params_.c_str());}
  std::string command::abstract() {return safe_gettext(abstract_.c_str());}
  std::string command::desc()
  {
    return abstract() + ".\n" + safe_gettext(desc_.c_str());
  }
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
    // These two get the "minor" names of each command, as the 'names'
    // set is sorted alphabetically.
    string const & selfname = *(self.names.begin());
    string const & othername = *(other.names.begin());
    // *twitch*
    return ((string(_(self.cmdgroup.c_str())) < string(_(other.cmdgroup.c_str())))
            || ((self.cmdgroup == other.cmdgroup)
                && (string(_(selfname.c_str())) < (string(_(othername.c_str()))))));
  }

  static command * find_command(string const & name)
  {
    map< string, command * >::iterator iter = (*cmds).find(name);
    return iter == (*cmds).end() ? NULL : (*iter).second;
  }

  string complete_command(string const & cmd)
  {
    if (cmd.length() == 0 || find_command(cmd) != NULL)
      return cmd;

    L(FL("expanding command '%s'") % cmd);

    set<string> matched;

    for (map<string,command *>::const_iterator i = (*cmds).begin();
         i != (*cmds).end(); ++i)
      {
        set< string > const & names = i->second->names;

        for (set< string >::const_iterator i2 = names.begin();
             i2 != names.end(); i2++)
          {
            string const & name = *i2;

            if (cmd.length() < name.length())
              {
                string prefix(name, 0, cmd.length());
                if (cmd == prefix) matched.insert(name);
              }
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
    for (set<string>::iterator i = matched.begin();
         i != matched.end(); ++i)
      err += ('\n' + *i);
    W(i18n_format(err));
    return "";
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

  // Generates a string of the form (a1, ..., aN), preceded by a space
  // for simplicity reasons later on, where a1 through aN are the aliases
  // for the command cmd.  Returns the empty string if no aliases are
  // defined for that command.
  static string format_names(set< string > const & names)
  {
    string text;

    set< string >::const_iterator iter = names.begin();
    do
      {
        text += *iter;
        iter++;
        if (iter != names.end())
          text += ", ";
      }
    while (iter != names.end());

    return text;
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
    vector<string>::const_iterator i = words.begin();
    while (i != words.end())
      {
        string const & word = *i;

        if (col + word.length() + 1 >= maxcol)
          {
            out << std::endl;
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
                while (i != words.end() && (*i) == "");
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

            string tag = format_names(i->second->names);
            size_t len = display_width(utf8(tag + "    "));
            if (colabstract < len)
              colabstract = len;
          }
      }

    sort(sorted.begin(), sorted.end(), greater<command *>());

    out << (*grpi).second << ":" << std::endl;
    for (size_t i = 0; i < sorted.size(); ++i)
      {
        set< string > const & names = idx(sorted, i)->names;
        string const & abstract = idx(sorted, i)->abstract();

        string tag = format_names(names);
        describe(tag, abstract, colabstract, out);
      }
  }

  static void explain_cmd_usage(string const & name, ostream & out)
  {
    command * cmd = find_command(name); // XXX Should be const.
    assert(cmd != NULL);

    out << F(safe_gettext("Syntax specific to 'mtn %s':")) % name
        << std::endl << std::endl;
    string params = cmd->params();
    vector<string> lines;
    split_into_lines(params, lines);
    for (vector<string>::const_iterator j = lines.begin();
         j != lines.end(); ++j)
      out << "  " << name << ' ' << *j << std::endl;
    split_into_lines(cmd->desc(), lines);
    for (vector<string>::const_iterator j = lines.begin();
         j != lines.end(); ++j)
      {
        describe("", *j, 4, out);
        out << std::endl;
      }
    if (cmd->names.size() > 1)
      {
        set< string > othernames = cmd->names;
        othernames.erase(name);
        describe("", "Aliases: " + format_names(othernames) + ".", 4, out);
        out << std::endl;
      }
  }

  void explain_usage(string const & name, ostream & out)
  {
    init_cmdgroups();

    map<string, string>::const_iterator cmdgroupiter;

    cmdgroupiter = cmdgroups.find(name);

    if (find_command(name) != NULL)
      explain_cmd_usage(name, out);
    else if (cmdgroupiter != cmdgroups.end())
      {
        explain_cmdgroup_usage(name, out);
        out << std::endl;
        out << "For information on a specific command, type "
               "'mtn help <command_name>'." << std::endl;
        out << std::endl;
      }
    else
      {
        assert(name.empty());

        explain_cmdgroups(out);
        out << std::endl;
        out << "To see what commands are available in a group, type "
               "'mtn help <group_name>'." << std::endl;
        out << "For information on a specific command, type "
               "'mtn help <command_name>'." << std::endl;
        out << std::endl;
      }
  }

  int process(app_state & app, string const & name, vector<utf8> const & args)
  {
    command * cmd = find_command(name);
    if (cmd != NULL)
      {
        L(FL("executing command '%s'") % name);

        // at this point we process the data from _MTN/options if
        // the command needs it.
        if (cmd->use_workspace_options)
          app.process_options();

        cmd->exec(app, name, args);
        return 0;
      }
    else
      {
        P(F("unknown command '%s'") % name);
        return 1;
      }
  }

  options::options_type command_options(vector<utf8> const & cmdline)
  {
    if (cmdline.empty())
      return options::options_type();
    string name = complete_command(idx(cmdline,0)());
    if (!name.empty())
      {
        return find_command(name)->get_options(cmdline);
      }
    else
      {
        N(!name.empty(),
          F("unknown command '%s'") % idx(cmdline, 0));
        return options::options_type();
      }
  }

  options::options_type toplevel_command_options(string const & name)
  {
    command * cmd = find_command(name);
    if (cmd != NULL)
      {
        return cmd->opts;
      }
    else
      {
        return options::options_type();
      }
  }
}
////////////////////////////////////////////////////////////////////////

CMD(help, "", N_("informative"), N_("command [ARGS...]"),
    N_("Displays help about commands and options"),
    N_(""),
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
  else if (find_command(full_cmd) != NULL)
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

CMD(crash, "", hidden_group(), "{ N | E | I | exception | signal }",
    N_("Triggers the specified kind of crash"),
    N_(""),
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

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

CMD(__test1, "", hidden_group(), "", "", "", options::opts::none) {}

CMD(__test2, "__test2.1",
    hidden_group(), "", "", "", options::opts::none) {}

CMD(__test3, "__test3.1 __test3.2",
    hidden_group(), "", "", "", options::opts::none) {}

UNIT_TEST(commands, find_command)
{
  using namespace commands;

  // Non-existent command.
  BOOST_CHECK(find_command("__test0") == NULL);

  // Lookup commands using their "primary" name.
  BOOST_CHECK(find_command("__test1") != NULL);
  BOOST_CHECK(find_command("__test2") != NULL);
  BOOST_CHECK(find_command("__test3") != NULL);

  // Lookup commands using any of their "secondary" names.
  BOOST_CHECK(find_command("__test2.1") != NULL);
  BOOST_CHECK(find_command("__test3.1") != NULL);
  BOOST_CHECK(find_command("__test3.2") != NULL);
}

UNIT_TEST(commands, format_names)
{
  using namespace commands;

  // Command with one name.
  BOOST_CHECK(format_names(find_command("__test1")->names) ==
              "__test1");

  // Command with two names.
  BOOST_CHECK(format_names(find_command("__test2")->names) ==
              "__test2, __test2.1");
  BOOST_CHECK(format_names(find_command("__test2.1")->names) ==
              "__test2, __test2.1");

  // Command with three names.
  BOOST_CHECK(format_names(find_command("__test3")->names) ==
              "__test3, __test3.1, __test3.2");
  BOOST_CHECK(format_names(find_command("__test3.1")->names) ==
              "__test3, __test3.1, __test3.2");
  BOOST_CHECK(format_names(find_command("__test3.2")->names) ==
              "__test3, __test3.1, __test3.2");
}
#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
