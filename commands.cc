// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <algorithm>

#include "restrictions.hh"
#include "transforms.hh"
#include "inodeprint.hh"

#include "cmd.hh"
//
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
  using std::map;
  // This must be a pointer.
  // It's used by the constructor of other static objects in different
  // files (cmd_*.cc), and since they're in different files, there's no
  // guarantee about what order they'll be initialized in. So have this
  // be something that doesn't get automatic initialization, and initialize
  // it ourselves the first time we use it.
  static map<string,command *> *cmds;
  command::command(string const & n,
                  string const & g,
                  string const & p,
                  string const & d,
                  bool u,
                  command_opts const & o)
    : name(n), cmdgroup(g), params(p), desc(d), use_workspace_options(u),
      options(o)
  {
    static bool first(true);
    if (first)
      cmds = new map<string, command *>;
    first = false;
    (*cmds)[n] = this;
  }
  command::~command() {}
  bool operator<(command const & self, command const & other);
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
  using namespace std;

  bool operator<(command const & self, command const & other)
  {
    // *twitch*
    return ((std::string(_(self.cmdgroup.c_str())) < std::string(_(other.cmdgroup.c_str())))
            || ((self.cmdgroup == other.cmdgroup)
                && (std::string(_(self.name.c_str())) < (std::string(_(other.name.c_str()))))));
  }


  string complete_command(string const & cmd) 
  {
    if (cmd.length() == 0 || (*cmds).find(cmd) != (*cmds).end()) return cmd;

    L(FL("expanding command '%s'\n") % cmd);

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

    if (matched.size() == 1) 
      {
      string completed = *matched.begin();
      L(FL("expanded command to '%s'") %  completed);  
      return completed;
      }
    else if (matched.size() > 1) 
      {
      string err = (F("command '%s' has multiple ambiguous expansions:\n") % cmd).str();
      for (vector<string>::iterator i = matched.begin();
           i != matched.end(); ++i)
        err += (*i + "\n");
      W(i18n_format(err));
    }

    return cmd;
  }

  const char * safe_gettext(const char * msgid)
  {
    if (strlen(msgid) == 0)
      return msgid;

    return _(msgid);
  }

  void explain_usage(string const & cmd, ostream & out)
  {
    map<string,command *>::const_iterator i;

    // try to get help on a specific command

    i = (*cmds).find(cmd);

    if (i != (*cmds).end())
      {
        string params = safe_gettext(i->second->params.c_str());
        vector<string> lines;
        split_into_lines(params, lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "     " << i->second->name << " " << *j << endl;
        split_into_lines(safe_gettext(i->second->desc.c_str()), lines);
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
        sorted.push_back(i->second);
      }
  
    sort(sorted.begin(), sorted.end(), std::greater<command *>());

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
        L(FL("executing command '%s'\n") % cmd);

        // at this point we process the data from _MTN/options if
        // the command needs it.
        if ((*cmds)[cmd]->use_workspace_options)
          app.process_options();

        (*cmds)[cmd]->exec(app, args);
        return 0;
      }
    else
      {
        P(F("unknown command '%s'\n") % cmd);
        return 1;
      }
  }

  set<int> command_options(string const & cmd)
  {
    if ((*cmds).find(cmd) != (*cmds).end())
      {
        return (*cmds)[cmd]->options.opts;
      }
    else
      {
        return set<int>();
      }
  }

  const no_opts OPT_NONE = no_opts();
}
////////////////////////////////////////////////////////////////////////

CMD(help, N_("informative"), N_("command [ARGS...]"), N_("display command help"), OPT_NONE)
{
        if (args.size() < 1)
                throw usage("");

        string full_cmd = complete_command(idx(args, 0)());
        if ((*cmds).find(full_cmd) == (*cmds).end())
                throw usage("");

        throw usage(full_cmd);
}

using std::set;
using std::pair;
using std::cin;

void
maybe_update_inodeprints(app_state & app)
{
  if (!in_inodeprints_mode())
    return;
  inodeprint_map ipm_new;
  temp_node_id_source nis;
  roster_t old_roster, new_roster;

  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);
  update_current_roster_from_filesystem(new_roster, app);

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      node_id nid = i->first;
      if (old_roster.has_node(nid))
        {
          node_t old_node = old_roster.get_node(nid);
          if (is_file_t(old_node))
            {
              node_t new_node = i->second;
              I(is_file_t(new_node));

              file_t old_file = downcast_to_file_t(old_node);
              file_t new_file = downcast_to_file_t(new_node);

              if (new_file->content == old_file->content)
                {
                  split_path sp;
                  new_roster.get_name(nid, sp);
                  file_path fp(sp);                  
                  hexenc<inodeprint> ip;
                  if (inodeprint_file(fp, ip))
                    ipm_new.insert(inodeprint_entry(fp, ip));
                }
            }
        }
    }
  data dat;
  write_inodeprint_map(ipm_new, dat);
  write_inodeprints(dat);
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
         std::set<revision_id> & completion,
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

  P(F("expanding selection '%s'\n") % str);

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
      P(F("expanded to '%s'\n") % *(p.first));
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
      string err = (F("selection '%s' has multiple ambiguous expansions: \n") % str).str();
      for (set<revision_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (describe_revision(app, *i) + "\n");
      N(completions.size() == 1, i18n_format(err));
    }

  completion = *completions.begin();
}

void
notify_if_multiple_heads(app_state & app)
{
  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  if (heads.size() > 1) {
    std::string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider '%s merge'"),
                      prefixedline);
    P(i18n_format(prefixedline) % app.branch_name % app.prog_name);
  }
}

// FIXME BUG: our log message handling is terribly locale-unaware -- if it's
// passed as -m, we convert to unicode, if it's passed as --message-file or
// entered interactively, we simply pass it through as bytes.

void
process_commit_message_args(bool & given,
                            string & log_message,
                            app_state & app)
{
  // can't have both a --message and a --message-file ...
  N(app.message().length() == 0 || app.message_file().length() == 0,
    F("--message and --message-file are mutually exclusive"));
  
  if (app.is_explicit_option(OPT_MESSAGE))
    {
      log_message = app.message();
      given = true;
    }
  else if (app.is_explicit_option(OPT_MSGFILE))
    {
      data dat;
      read_data_for_command_line(app.message_file(), dat);
      log_message = dat();
      given = true;
    }
  else
    given = false;
}
