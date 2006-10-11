// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <algorithm>

#include "transforms.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "inodeprint.hh"
#include "cert.hh"
#include "ui.hh"
#include "cmd.hh"

#ifndef _WIN32
#include <boost/lexical_cast.hpp>
#include <signal.h>
#endif

using std::cin;
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
                   string const & d,
                   bool u,
                   option::optset const & o)
    : name(n), cmdgroup(g), params_(p), desc_(d), use_workspace_options(u),
      options(o)
  {
    if (cmds == NULL)
      cmds = new map<string, command *>;
    (*cmds)[n] = this;
  }
  command::~command() {}
  std::string command::params() {return safe_gettext(params_.c_str());}
  std::string command::desc() {return safe_gettext(desc_.c_str());}
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
  using std::endl;
  using std::greater;
  using std::ostream;

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
    N(matched.size() != 0,
      F("unknown command '%s'") % cmd);

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
      err += (*i + "\n");
    W(i18n_format(err));
    return cmd;
  }

  void explain_usage(string const & cmd, ostream & out)
  {
    map<string,command *>::const_iterator i;

    // try to get help on a specific command

    i = (*cmds).find(cmd);

    if (i != (*cmds).end())
      {
        string params = i->second->params();
        vector<string> lines;
        split_into_lines(params, lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "     " << i->second->name << " " << *j << endl;
        split_into_lines(i->second->desc(), lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "       " << *j << endl;
        out << endl;
        return;
      }

    vector<command *> sorted;
    out << _("commands:") << endl;
    for (i = (*cmds).begin(); i != (*cmds).end(); ++i)
      {
        if (i->second->cmdgroup != hidden_group())
          sorted.push_back(i->second);
      }

    sort(sorted.begin(), sorted.end(), greater<command *>());

    string curr_group;
    size_t col = 0;
    size_t col2 = 0;
    for (size_t i = 0; i < sorted.size(); ++i)
      {
        size_t cmp = display_width(utf8(safe_gettext(idx(sorted, i)->cmdgroup.c_str())));
        col2 = col2 > cmp ? col2 : cmp;
      }

    for (size_t i = 0; i < sorted.size(); ++i)
      {
        if (idx(sorted, i)->cmdgroup != curr_group)
          {
            curr_group = idx(sorted, i)->cmdgroup;
            out << endl;
            out << "  " << safe_gettext(idx(sorted, i)->cmdgroup.c_str());
            col = display_width(utf8(safe_gettext(idx(sorted, i)->cmdgroup.c_str()))) + 2;
            while (col++ < (col2 + 3))
              out << ' ';
          }
        out << " " << idx(sorted, i)->name;
        col += idx(sorted, i)->name.size() + 1;
        if (col >= 70)
          {
            out << endl;
            col = 0;
            while (col++ < (col2 + 3))
              out << ' ';
          }
      }
    out << endl << endl;
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

  option::optset command_options(string const & cmd)
  {
    if ((*cmds).find(cmd) != (*cmds).end())
      {
        return (*cmds)[cmd]->options;
      }
    else
      {
        return option::optset();
      }
  }
}
////////////////////////////////////////////////////////////////////////

CMD(help, N_("informative"), N_("command [ARGS...]"), N_("display command help"), option::none)
{
  if (args.size() < 1)
    {
      app.opts.help = true;
      throw usage("");
    }

  string full_cmd = complete_command(idx(args, 0)());
  if ((*cmds).find(full_cmd) == (*cmds).end())
    throw usage("");

  app.opts.help = true;
  throw usage(full_cmd);
}

CMD(crash, hidden_group(), "{ N | E | I | exception | signal }", "trigger the specified kind of crash", option::none)
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
get_stdin()
{
  char buf[constants::bufsz];
  string tmp;
  while(cin)
    {
      cin.read(buf, constants::bufsz);
      tmp.append(buf, cin.gcount());
    }
  return tmp;
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
  app.db.get_revision_certs(id, author_name, tmp);
  erase_bogus_certs(tmp, app);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }
  app.db.get_revision_certs(id, date_name, tmp);
  erase_bogus_certs(tmp, app);
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
      completion.insert(revision_id(str));
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
      pair<set<revision_id>::const_iterator, bool> p = completion.insert(revision_id(*i));
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
  get_branch_heads(app.opts.branch_name(), app, heads);
  if (heads.size() > 1) {
    string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider '%s merge'"),
                      prefixedline);
    P(i18n_format(prefixedline) % app.opts.branch_name % ui.prog_name);
  }
}

void
process_commit_message_args(bool & given,
                            utf8 & log_message,
                            app_state & app)
{
  // can't have both a --message and a --message-file ...
  N(!app.opts.message_given || !app.opts.msgfile_given,
    F("--message and --message-file are mutually exclusive"));

  if (app.opts.message_given)
    {
      log_message = app.opts.message;
      given = true;
    }
  else if (app.opts.msgfile_given)
    {
      data dat;
      read_data_for_command_line(app.opts.msgfile, dat);
      external dat2 = dat();
      system_to_utf8(dat2, log_message);
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
