// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include <algorithm>
#include <iterator>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>

#include "commands.hh"
#include "constants.hh"

#include "app_state.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "keys.hh"
#include "manifest.hh"
#include "netsync.hh"
#include "packet.hh"
#include "rcs_import.hh"
#include "sanity.hh"
#include "cert.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"
#include "work.hh"

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
  struct command;
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

  struct command; 

  static map<string,command *> cmds;

  struct command 
  {
    string name;
    string cmdgroup;
    string params;
    string desc;
    command(string const & n,
            string const & g,
            string const & p,
            string const & d) : name(n), cmdgroup(g), params(p), desc(d) 
    { cmds[n] = this; }
    virtual ~command() {}
    virtual void exec(app_state & app, vector<utf8> const & args) = 0;
  };

  bool operator<(command const & self, command const & other)
  {
    return ((self.cmdgroup < other.cmdgroup)
            || ((self.cmdgroup == other.cmdgroup) && (self.name < other.name)));
  }


  string complete_command(string const & cmd) 
  {
    if (cmd.length() == 0 || cmds.find(cmd) != cmds.end()) return cmd;

    P(F("expanding command '%s'\n") % cmd);

    vector<string> matched;

    for (map<string,command *>::const_iterator i = cmds.begin();
         i != cmds.end(); ++i) 
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
      P(F("expanded command to '%s'\n") %  completed);  
      return completed;
      }
    else if (matched.size() > 1) 
      {
      string err = (F("command '%s' has multiple ambiguous expansions: \n") % cmd).str();
      for (vector<string>::iterator i = matched.begin();
           i != matched.end(); ++i)
        err += (*i + "\n");
      W(boost::format(err));
    }

    return cmd;
  }

  void explain_usage(string const & cmd, ostream & out)
  {
    map<string,command *>::const_iterator i;

    string completed = complete_command(cmd);

    // try to get help on a specific command

    i = cmds.find(completed);

    if (i != cmds.end())
      {
        string params = i->second->params;
        vector<string> lines;
        split_into_lines(params, lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "     " << i->second->name << " " << *j << endl;
        split_into_lines(i->second->desc, lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "       " << *j << endl;
        out << endl;
        return;
      }

    vector<command *> sorted;
    out << "commands:" << endl;
    for (i = cmds.begin(); i != cmds.end(); ++i)
      {
        sorted.push_back(i->second);
      }
  
    sort(sorted.begin(), sorted.end(), std::greater<command *>());

    string curr_group;
    size_t col = 0;
    size_t col2 = 0;
    for (size_t i = 0; i < sorted.size(); ++i)
      {
                col2 = col2 > idx(sorted, i)->cmdgroup.size() ? col2 : idx(sorted, i)->cmdgroup.size();
      }

    for (size_t i = 0; i < sorted.size(); ++i)
      {
        if (idx(sorted, i)->cmdgroup != curr_group)
          {
            curr_group = idx(sorted, i)->cmdgroup;
            out << endl;
            out << "  " << idx(sorted, i)->cmdgroup;
            col = idx(sorted, i)->cmdgroup.size() + 2;
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
    string completed = complete_command(cmd);
    
    if (cmds.find(completed) != cmds.end())
      {
        L(F("executing %s command\n") % completed);
        cmds[completed]->exec(app, args);
        return 0;
      }
    else
      {
        ui.inform(F("unknown command '%s'\n") % cmd);
        return 1;
      }
  }

#define CMD(C, group, params, desc)              \
struct cmd_ ## C : public command                \
{                                                \
  cmd_ ## C() : command(#C, group, params, desc) \
  {}                                             \
  virtual void exec(app_state & app,             \
                    vector<utf8> const & args);  \
};                                               \
static cmd_ ## C C ## _cmd;                      \
void cmd_ ## C::exec(app_state & app,            \
                     vector<utf8> const & args)  \

#define ALIAS(C, realcommand, group, params, desc)      \
CMD(C, group, params, desc)                             \
{                                                       \
  process(app, string(#realcommand), args);             \
}

static void 
get_work_path(local_path & w_path)
{
  w_path = (mkpath(book_keeping_dir) / mkpath(work_file_name)).string();
  L(F("work path is %s\n") % w_path);
}

static void 
get_revision_path(local_path & m_path)
{
  m_path = (mkpath(book_keeping_dir) / mkpath(revision_file_name)).string();
  L(F("revision path is %s\n") % m_path);
}

static void 
get_revision_id(revision_id & c)
{
  c = revision_id();
  local_path c_path;
  get_revision_path(c_path);
  if(file_exists(c_path))
    {
      data c_data;
      L(F("loading revision id from %s\n") % c_path);
      read_data(c_path, c_data);
      c = revision_id(remove_ws(c_data()));
    }
  else
    {
      L(F("no revision id file %s\n") % c_path);
    }
}

static void 
put_revision_id(revision_id & rev)
{
  local_path c_path;
  get_revision_path(c_path);
  L(F("writing revision id to %s\n") % c_path);
  data c_data(rev.inner()() + "\n");
  write_data(c_path, c_data);
}

static void 
get_path_rearrangement(change_set::path_rearrangement & w)
{
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    {
      L(F("checking for un-committed work file %s\n") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_path_rearrangement(w_data, w);
      L(F("read rearrangement from %s\n") % w_path);
    }
  else
    {
      L(F("no un-committed work file %s\n") % w_path);
    }
}

static void 
remove_path_rearrangement()
{
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    delete_file(w_path);
}

static void 
put_path_rearrangement(change_set::path_rearrangement & w)
{
  local_path w_path;
  get_work_path(w_path);
  
  if (w.empty())
    {
      if (file_exists(w_path))
        delete_file(w_path);
    }
  else
    {
      data w_data;
      write_path_rearrangement(w, w_data);
      write_data(w_path, w_data);
    }
}

static void
restrict_path_set(string const & type,
                  path_set const & paths, 
                  path_set & included, 
                  path_set & excluded,
                  app_state & app)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (app.restriction_includes(*i)) 
        {
          L(F("restriction includes %s %s\n") % type % *i);
          included.insert(*i);
        }
      else
        {
          L(F("restriction excludes %s %s\n") % type % *i);
          excluded.insert(*i);
        }
    }
}

static void 
restrict_rename_set(string const & type,
                    std::map<file_path, file_path> const & renames, 
                    std::map<file_path, file_path> & included,
                    std::map<file_path, file_path> & excluded, 
                    app_state & app)
{
  for (std::map<file_path, file_path>::const_iterator i = renames.begin();
       i != renames.end(); ++i)
    {
      bool src_included = app.restriction_includes(i->first);
      bool dst_included = app.restriction_includes(i->second);
      if (src_included && dst_included)
        {
          L(F("restriction includes %s '%s' to '%s'\n") % type % i->first % i->second);
          included.insert(*i);
        }
      else if (!src_included && !dst_included)
        {
          L(F("restriction excludes %s '%s' to '%s'\n") % type % i->first % i->second);
          excluded.insert(*i);
        }
      else
        {
          N(false,
            F("rename '%s' to '%s' crosses path restriction\n"
              "please include or exclude explicitly") % i->first % i->second);
        }
    }
}

static void
restrict_path_rearrangement(change_set::path_rearrangement const & work, 
                            change_set::path_rearrangement & included,
                            change_set::path_rearrangement & excluded,
                            app_state & app)
{
  restrict_path_set("delete file", work.deleted_files, 
                    included.deleted_files, excluded.deleted_files, app);
  restrict_path_set("delete dir", work.deleted_dirs, 
                    included.deleted_dirs, excluded.deleted_dirs, app);

  restrict_rename_set("rename file", work.renamed_files, 
                      included.renamed_files, excluded.renamed_files, app);
  restrict_rename_set("rename dir", work.renamed_dirs, 
                      included.renamed_dirs, excluded.renamed_dirs, app);

  restrict_path_set("add file", work.added_files, 
                    included.added_files, excluded.added_files, app);
}

static void 
get_path_rearrangement(change_set::path_rearrangement & included,
                       change_set::path_rearrangement & excluded,
                       app_state & app)
{
  change_set::path_rearrangement work;
  get_path_rearrangement(work);
  restrict_path_rearrangement(work, included, excluded, app);
}

static void 
update_any_attrs(app_state & app)
{
  file_path fp;
  data attr_data;
  attr_map attr;

  get_attr_path(fp);
  if (!file_exists(fp))
    return;

  read_data(fp, attr_data);
  read_attr_map(attr_data, attr);
  apply_attributes(app, attr);
}

static void
calculate_base_revision(app_state & app, 
                        revision_id & rid,
                        revision_set & rev,
                        manifest_id & mid,
                        manifest_map & man)
{
  rev.edges.clear();
  man.clear();

  get_revision_id(rid);

  if (! rid.inner()().empty())
    {

      N(app.db.revision_exists(rid),
        F("base revision %s does not exist in database\n") % rid);
      
      app.db.get_revision_manifest(rid, mid);
      L(F("old manifest is %s\n") % mid);
      
      N(app.db.manifest_version_exists(mid),
        F("base manifest %s does not exist in database\n") % mid);
      
      app.db.get_manifest(mid, man);
    }

  L(F("old manifest has %d entries\n") % man.size());
}

static void
calculate_base_revision(app_state & app, 
                        revision_set & rev,
                        manifest_map & man)
{
  revision_id rid;
  manifest_id mid;
  calculate_base_revision(app, rid, rev, mid, man);
}

static void
calculate_base_manifest(app_state & app, 
                        manifest_map & man)
{
  revision_id rid;
  manifest_id mid;
  revision_set rev;
  calculate_base_revision(app, rid, rev, mid, man);
}

static void
calculate_current_revision(app_state & app, 
                           revision_set & rev,
                           manifest_map & m_old,
                           manifest_map & m_new)
{
  manifest_id old_manifest_id;
  revision_id old_revision_id;  
  change_set cs;
  path_set old_paths, new_paths;
  manifest_map m_old_rearranged;

  rev.edges.clear();
  m_old.clear();
  m_new.clear();

  calculate_base_revision(app, 
                          old_revision_id, rev, 
                          old_manifest_id, m_old);
  

  get_path_rearrangement(cs.rearrangement);
  extract_path_set(m_old, old_paths);
  apply_path_rearrangement(old_paths, cs.rearrangement, new_paths);
  build_manifest_map(new_paths, m_new, app);
  complete_change_set(m_old, m_new, cs);
  
  calculate_ident(m_new, rev.new_manifest);
  L(F("new manifest is %s\n") % rev.new_manifest);

  rev.edges.insert(make_pair(old_revision_id,
                             make_pair(old_manifest_id, cs)));
}

static void
calculate_restricted_revision(app_state & app, 
                              revision_set & rev,
                              manifest_map & m_old,
                              manifest_map & m_new,
                              change_set::path_rearrangement & restricted_work)
{
  manifest_id old_manifest_id;
  revision_id old_revision_id;    
  change_set cs;
  path_set old_paths, new_paths;
  manifest_map m_old_rearranged;

  rev.edges.clear();
  m_old.clear();
  m_new.clear();

  calculate_base_revision(app, 
                          old_revision_id, rev, 
                          old_manifest_id, m_old);

  change_set::path_rearrangement included, excluded;

  get_path_rearrangement(included, excluded, app);

  extract_path_set(m_old, old_paths);
  apply_path_rearrangement(old_paths, included, new_paths);

  cs.rearrangement = included;
  restricted_work = excluded;

  build_restricted_manifest_map(new_paths, m_old, m_new, app);
  complete_change_set(m_old, m_new, cs);

  calculate_ident(m_new, rev.new_manifest);
  L(F("new manifest is %s\n") % rev.new_manifest);

  rev.edges.insert(make_pair(old_revision_id,
                             make_pair(old_manifest_id, cs)));
}


static void
calculate_restricted_revision(app_state & app, 
                              revision_set & rev,
                              manifest_map & m_old,
                              manifest_map & m_new)
{
  change_set::path_rearrangement work;
  calculate_restricted_revision(app, rev, m_old, m_new, work);
}

static string 
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

static void 
get_log_message(revision_set const & cs, 
                app_state & app,
                string & log_message)
{
  string commentary;
  data summary;
  write_revision_set(cs, summary);
  commentary += "----------------------------------------------------------------------\n";
  commentary += "Enter Log.  Lines beginning with `MT:' are removed automatically\n";
  commentary += "\n";
  commentary += summary();
  commentary += "----------------------------------------------------------------------\n";
  N(app.lua.hook_edit_comment(commentary, log_message),
    F("edit of log message failed"));
}

static string
describe_revision(app_state & app, revision_id const & id)
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

static void
decode_selector(string const & orig_sel,
                selector_type & type,
                string & sel,
                app_state & app)
{
  sel = orig_sel;

  L(F("decoding selector '%s'\n") % sel);

  if (sel.size() < 2 || sel[1] != ':')
    {
      string tmp;
      if (!app.lua.hook_expand_selector(sel, tmp))
        {
          L(F("expansion of selector '%s' failed\n") % sel);
        }
      else
        {
          P(F("expanded selector '%s' -> '%s'\n") % sel % tmp);
          sel = tmp;
        }
    }
  
  if (sel.size() >= 2 && sel[1] == ':')
    {
      switch (sel[0])
        {
        case 'a': 
          type = sel_author;
          break;
        case 'b':
          type = sel_branch;
          break;
        case 'd':
          type = sel_date;
          break;
        case 'i':
          type = sel_ident;
          break;
        case 't':
          type = sel_tag;
          break;
        default:          
          W(F("unknown selector type: %c\n") % sel[0]);
          break;
        }
      sel.erase(0,2);
    }
}

static void
complete_selector(string const & orig_sel,
                  vector<pair<selector_type, string> > const & limit,             
                  selector_type & type,
                  set<string> & completions,
                  app_state & app)
{  
  string sel;
  decode_selector(orig_sel, type, sel, app);
  app.db.complete(type, sel, limit, completions);
}


static void 
complete(app_state & app, 
         string const & str, 
         revision_id & completion)
{

  // this rule should always be enabled, even if the user specifies
  // --norc: if you provide a revision id, you get a revision id.
  if (str.find_first_not_of(constants::legal_id_bytes) == string::npos
      && str.size() == constants::idlen)
    {
      completion = revision_id(str);
      return;
    }
  
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  boost::char_separator<char> slash("/");
  tokenizer tokens(str, slash);

  vector<string> selector_strings;
  vector<pair<selector_type, string> > selectors;  
  copy(tokens.begin(), tokens.end(), back_inserter(selector_strings));
  for (vector<string>::const_iterator i = selector_strings.begin();
       i != selector_strings.end(); ++i)
    {
      string sel;
      selector_type type = sel_unknown;
      decode_selector(*i, type, sel, app);
      selectors.push_back(make_pair(type, sel));
    }

  P(F("expanding selection '%s'\n") % str);

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  selector_type ty = sel_ident;
  complete_selector("", selectors, ty, completions, app);

  N(completions.size() != 0,
    F("no match for selection '%s'") % str);
  if (completions.size() > 1)
    {
      string err = (F("selection '%s' has multiple ambiguous expansions: \n") % str).str();
      for (set<string>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (describe_revision(app, revision_id(*i)) + "\n");
      N(completions.size() == 1, boost::format(err));
    }
  completion = revision_id(*(completions.begin()));  
  P(F("expanded to '%s'\n") %  completion);  
}

static void 
complete(app_state & app, 
         string const & str, 
         manifest_id & completion)
{
  N(str.find_first_not_of(constants::legal_id_bytes) == string::npos,
    F("non-hex digits in id"));
  if (str.size() == constants::idlen)
    {
      completion = manifest_id(str);
      return;
    }
  set<manifest_id> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have a unique expansion") % str);
  if (completions.size() > 1)
    {
      string err = (F("partial id '%s' has multiple ambiguous expansions: \n") % str).str();
      for (set<manifest_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (i->inner()() + "\n");
      N(completions.size() == 1, boost::format(err));
    }
  completion = *(completions.begin());  
  P(F("expanding partial id '%s'\n") % str);
  P(F("expanded to '%s'\n") %  completion);
}

static void 
complete(app_state & app, 
         string const & str, 
         file_id & completion)
{
  N(str.find_first_not_of(constants::legal_id_bytes) == string::npos,
    F("non-hex digits in id"));
  if (str.size() == constants::idlen)
    {
      completion = file_id(str);
      return;
    }
  set<file_id> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have a unique expansion") % str);
  if (completions.size() > 1)
    {
      string err = (F("partial id '%s' has multiple ambiguous expansions: \n") % str).str();
      for (set<file_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (i->inner()() + "\n");
      N(completions.size() == 1, boost::format(err));
    }
  completion = *(completions.begin());  
  P(F("expanding partial id '%s'\n") % str);
  P(F("expanded to '%s'\n") %  completion);
}

static void 
ls_certs(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  vector<cert> certs;
  
  transaction_guard guard(app.db);
  
  revision_id ident;
  complete(app, idx(args, 0)(), ident);
  vector< revision<cert> > ts;
  app.db.get_revision_certs(ident, ts);
  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;      
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !app.db.public_key_exists(idx(certs, i).key))
          P(F("warning: no public key '%s' found in database\n")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }
        
  // Make the output deterministic; this is useful for the test suite, in
  // particular.
  sort(certs.begin(), certs.end());

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;      
      decode_base64(idx(certs, i).value, tv);
      string washed;
      if (guess_binary(tv()))
        {
          washed = "<binary data>";
        }
      else
        {
          washed = tv();
        }

      string stat;
      switch (status)
        {
        case cert_ok:
          stat = "ok";
          break;
        case cert_bad:
          stat = "bad";
          break;
        case cert_unknown:
          stat = "unknown";
          break;
        }

      vector<string> lines;
      split_into_lines(washed, lines);
      I(lines.size() > 0);

      cout << "-----------------------------------------------------------------" << endl
           << "Key   : " << idx(certs, i).key() << endl
           << "Sig   : " << stat << endl           
           << "Name  : " << idx(certs, i).name() << endl           
           << "Value : " << idx(lines, 0) << endl;
      
      for (size_t i = 1; i < lines.size(); ++i)
        cout << "      : " << idx(lines, i) << endl;
    }  

  if (certs.size() > 0)
    cout << endl;

  guard.commit();
}

static void 
ls_keys(string const & name, app_state & app, vector<utf8> const & args)
{
  vector<rsa_keypair_id> pubkeys;
  vector<rsa_keypair_id> privkeys;

  app.initialize(false);

  transaction_guard guard(app.db);

  if (args.size() == 0)
    app.db.get_key_ids("", pubkeys, privkeys);
  else if (args.size() == 1)
    app.db.get_key_ids(idx(args, 0)(), pubkeys, privkeys);
  else
    throw usage(name);
  
  if (pubkeys.size() > 0)
    {
      cout << endl << "[public keys]" << endl;
      for (size_t i = 0; i < pubkeys.size(); ++i)
        {
          rsa_keypair_id keyid = idx(pubkeys, i)();
          base64<rsa_pub_key> pub_encoded;
          hexenc<id> hash_code;

          app.db.get_key(keyid, pub_encoded); 
          key_hash_code(keyid, pub_encoded, hash_code);
          cout << hash_code << " " << keyid << endl;
        }
      cout << endl;
    }

  if (privkeys.size() > 0)
    {
      cout << endl << "[private keys]" << endl;
      for (size_t i = 0; i < privkeys.size(); ++i)
        {
          rsa_keypair_id keyid = idx(privkeys, i)();
          base64< arc4<rsa_priv_key> > priv_encoded;
          hexenc<id> hash_code;
          app.db.get_key(keyid, priv_encoded); 
          key_hash_code(keyid, priv_encoded, hash_code);
          cout << hash_code << " " << keyid << endl;
        }
      cout << endl;
    }

  if (pubkeys.size() == 0 &&
      privkeys.size() == 0)
    {
      if (args.size() == 0)
        P(F("no keys found\n"));
      else
        W(F("no keys found matching '%s'\n") % idx(args, 0)());
    }
  guard.commit();
}

// The changes_summary structure holds a list all of files and directories
// affected in a revision, and is useful in the 'log' command to print this
// information easily.  It has to be constructed from all change_set objects
// that belong to a revision.
struct
changes_summary
{
  bool empty;
  change_set::path_rearrangement rearrangement;
  std::set<file_path> modified_files;

  changes_summary(void);
  void add_change_set(change_set const & cs);
  void print(std::ostream & os, size_t max_cols) const;
};

changes_summary::changes_summary(void) : empty(true)
{
}

void
changes_summary::add_change_set(change_set const & cs)
{
  if (cs.empty())
    return;
  empty = false;

  change_set::path_rearrangement const & pr = cs.rearrangement;

  for (std::set<file_path>::const_iterator i = pr.deleted_files.begin();
       i != pr.deleted_files.end(); i++)
    rearrangement.deleted_files.insert(*i);

  for (std::set<file_path>::const_iterator i = pr.deleted_dirs.begin();
       i != pr.deleted_dirs.end(); i++)
    rearrangement.deleted_dirs.insert(*i);

  for (std::map<file_path, file_path>::const_iterator
       i = pr.renamed_files.begin(); i != pr.renamed_files.end(); i++)
    rearrangement.renamed_files.insert(*i);

  for (std::map<file_path, file_path>::const_iterator
       i = pr.renamed_dirs.begin(); i != pr.renamed_dirs.end(); i++)
    rearrangement.renamed_dirs.insert(*i);

  for (std::set<file_path>::const_iterator i = pr.added_files.begin();
       i != pr.added_files.end(); i++)
    rearrangement.added_files.insert(*i);

  for (change_set::delta_map::const_iterator i = cs.deltas.begin();
       i != cs.deltas.end(); i++)
    {
      if (pr.added_files.find(i->first()) == pr.added_files.end())
        modified_files.insert(i->first());
    }
}

void
changes_summary::print(std::ostream & os, size_t max_cols) const
{
#define PRINT_INDENTED_SET(setname) \
  size_t cols = 8; \
  os << "       "; \
  for (std::set<file_path>::const_iterator i = setname.begin(); \
       i != setname.end(); i++) \
    { \
      const std::string str = (*i)(); \
      if (cols > 8 && cols + str.size() + 1 >= max_cols) \
        { \
          cols = 8; \
          os << endl << "       "; \
        } \
      os << " " << str; \
      cols += str.size() + 1; \
    } \
  os << endl;

  if (! rearrangement.deleted_files.empty())
    {
      os << "Deleted files:" << endl;
      PRINT_INDENTED_SET(rearrangement.deleted_files)
    }

  if (! rearrangement.deleted_dirs.empty())
    {
      os << "Deleted directories:" << endl;
      PRINT_INDENTED_SET(rearrangement.deleted_dirs)
    }

  if (! rearrangement.renamed_files.empty())
    {
      os << "Renamed files:" << endl;
      for (std::map<file_path, file_path>::const_iterator
           i = rearrangement.renamed_files.begin();
           i != rearrangement.renamed_files.end(); i++)
        os << "        " << i->first << " to " << i->second << endl;
    }

  if (! rearrangement.renamed_dirs.empty())
    {
      os << "Renamed directories:" << endl;
      for (std::map<file_path, file_path>::const_iterator
           i = rearrangement.renamed_dirs.begin();
           i != rearrangement.renamed_dirs.end(); i++)
        os << "        " << i->first << " to " << i->second << endl;
    }

  if (! rearrangement.added_files.empty())
    {
      os << "Added files:" << endl;
      PRINT_INDENTED_SET(rearrangement.added_files)
    }

  if (! modified_files.empty())
    {
      os << "Modified files:" << endl;
      PRINT_INDENTED_SET(modified_files)
    }

#undef PRINT_INDENTED_SET
}

CMD(genkey, "key and cert", "KEYID", "generate an RSA key-pair")
{
  if (args.size() != 1)
    throw usage(name);
  
  app.initialize(false);

  transaction_guard guard(app.db);
  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  N(! app.db.key_exists(ident),
    F("key '%s' already exists in database") % ident);
  
  base64<rsa_pub_key> pub;
  base64< arc4<rsa_priv_key> > priv;
  P(F("generating key-pair '%s'\n") % ident);
  generate_key_pair(app.lua, ident, pub, priv);
  P(F("storing key-pair '%s' in database\n") % ident);
  app.db.put_key_pair(ident, pub, priv);

  guard.commit();
}

CMD(chkeypass, "key and cert", "KEYID", "change passphrase of a private RSA key")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  transaction_guard guard(app.db);
  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  N(app.db.key_exists(ident),
    F("key '%s' does not exist in database") % ident);

  base64< arc4<rsa_priv_key> > key;
  app.db.get_key(ident, key);
  change_key_passphrase(app.lua, ident, key);
  app.db.delete_private_key(ident);
  app.db.put_key(ident, key);
  P(F("passphrase changed\n"));

  guard.commit();
}

CMD(cert, "key and cert", "REVISION CERTNAME [CERTVAL]",
    "create a cert for a revision")
{
  if ((args.size() != 3) && (args.size() != 2))
    throw usage(name);

  app.initialize(false);

  transaction_guard guard(app.db);

  hexenc<id> ident;
  revision_id rid;
  complete(app, idx(args, 0)(), rid);
  ident = rid.inner();
  
  cert_name name;
  internalize_cert_name(idx(args, 1), name);

  rsa_keypair_id key;
  if (app.signing_key() != "")
    key = app.signing_key;
  else
    N(guess_default_key(key, app),
      F("no unique private key found, and no key specified"));
  
  cert_value val;
  if (args.size() == 3)
    val = cert_value(idx(args, 2)());
  else
    val = cert_value(get_stdin());

  base64<cert_value> val_encoded;
  encode_base64(val, val_encoded);

  cert t(ident, name, val_encoded, key);
  
  packet_db_writer dbw(app);
  calculate_cert(app, t);
  dbw.consume_revision_cert(revision<cert>(t));
  guard.commit();
}

CMD(trusted, "key and cert", "REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]",
    "test whether a hypothetical cert would be trusted\n"
    "by current settings")
{
  if (args.size() < 4)
    throw usage(name);

  app.initialize(false);

  revision_id rid;
  complete(app, idx(args, 0)(), rid);
  hexenc<id> ident(rid.inner());
  
  cert_name name;
  internalize_cert_name(idx(args, 1), name);
  
  cert_value value(idx(args, 2)());

  set<rsa_keypair_id> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      rsa_keypair_id keyid;
      internalize_rsa_keypair_id(idx(args, i), keyid);
      signers.insert(keyid);
    }
  
  
  bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                      name, value);

  cout << "if a cert on: " << ident << endl
       << "with key: " << name << endl
       << "and value: " << value << endl
       << "was signed by: ";
  for (set<rsa_keypair_id>::const_iterator i = signers.begin(); i != signers.end(); ++i)
    cout << *i << " ";
  cout << endl
       << "it would be: " << (trusted ? "trusted" : "UNtrusted") << endl;
}

CMD(tag, "review", "REVISION TAGNAME", 
    "put a symbolic tag cert on a revision version")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_tag(r, idx(args, 1)(), app, dbw);
}


CMD(testresult, "review", "ID (true|false)", 
    "note the results of running a test on a revision")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_testresult(r, idx(args, 1)(), app, dbw);
}

CMD(approve, "review", "REVISION", 
    "approve of a particular revision")
{
  if (args.size() != 1)
    throw usage(name);  

  app.initialize(false);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_value branchname;
  guess_branch (r, app, branchname);
  app.set_branch(branchname());
  N(app.branch_name() != "", F("need --branch argument for approval"));  
  cert_revision_in_branch(r, app.branch_name(), app, dbw);
}


CMD(disapprove, "review", "REVISION", 
    "disapprove of a particular revision")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  revision_id r;
  revision_set rev, rev_inverse;
  change_set cs_inverse;
  complete(app, idx(args, 0)(), r);
  app.db.get_revision(r, rev);

  N(rev.edges.size() == 1, 
    F("revision %s has %d changesets, cannot invert\n") % r % rev.edges.size());

  cert_value branchname;
  guess_branch (r, app, branchname);
  app.set_branch(branchname());
  N(app.branch_name() != "", F("need --branch argument for disapproval"));  
  
  edge_entry const & old_edge (*rev.edges.begin());
  rev_inverse.new_manifest = edge_old_manifest(old_edge);
  manifest_map m_old;
  app.db.get_manifest(edge_old_manifest(old_edge), m_old);
  invert_change_set(edge_changes(old_edge), m_old, cs_inverse);
  rev_inverse.edges.insert(make_pair(r, make_pair(rev.new_manifest, cs_inverse)));

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    revision_id inv_id;
    revision_data rdat;

    write_revision_set(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    dbw.consume_revision_data(inv_id, rdat);
    
    cert_revision_in_branch(inv_id, branchname, app, dbw); 
    cert_revision_date_now(inv_id, app, dbw);
    cert_revision_author_default(inv_id, app, dbw);
    cert_revision_changelog(inv_id, (F("disapproval of revision %s") % r).str(), app, dbw);
    guard.commit();
  }
}

CMD(comment, "review", "REVISION [COMMENT]",
    "comment on a particular revision")
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  app.initialize(false);

  string comment;
  if (args.size() == 2)
    comment = idx(args, 1)();
  else
    N(app.lua.hook_edit_comment("", comment), 
      F("edit comment failed"));
  
  N(comment.find_first_not_of(" \r\t\n") != string::npos, 
    F("empty comment"));

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_comment(r, comment, app, dbw);
}



CMD(add, "working copy", "PATH...", "add files to working copy")
{
  if (args.size() < 1)
    throw usage(name);

  app.initialize(true);

  manifest_map m_old;
  calculate_base_manifest(app, m_old);

  change_set::path_rearrangement work;  
  get_path_rearrangement(work);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_addition(app.prefix((*i)()), m_old, app, work);
    
  put_path_rearrangement(work);

  update_any_attrs(app);
}

CMD(drop, "working copy", "PATH...", "drop files from working copy")
{
  if (args.size() < 1)
    throw usage(name);

  app.initialize(true);

  manifest_map m_old;
  calculate_base_manifest(app, m_old);

  change_set::path_rearrangement work;
  get_path_rearrangement(work);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_deletion(app.prefix((*i)()), m_old, work);
  
  put_path_rearrangement(work);

  update_any_attrs(app);
}


CMD(rename, "working copy", "SRC DST", "rename entries in the working copy")
{
  if (args.size() != 2)
    throw usage(name);
  
  app.initialize(true);

  manifest_map m_old;
  calculate_base_manifest(app, m_old);

  change_set::path_rearrangement work;
  get_path_rearrangement(work);

  build_rename(app.prefix(idx(args, 0)()), app.prefix(idx(args, 1)()), m_old, work);
  
  put_path_rearrangement(work);
  
  update_any_attrs(app);
}


// fload and fmerge are simple commands for debugging the line
// merger. fcommit is a helper for making single-file commits to monotone
// (such as automated processes might want to do).

CMD(fcommit, "tree", "REVISION FILENAME [LOG_MESSAGE]", 
    "commit change to a single file")
{
  if (args.size() != 2 && args.size() != 3)
    throw usage(name);

  file_id old_fid, new_fid;
  revision_id old_rid, new_rid;
  manifest_id old_mid, new_mid;
  manifest_map old_man, new_man;
  file_data old_fdata, new_fdata;
  cert_value branchname;
  revision_data rdata;
  revision_set rev;
  change_set cs;

  string log_message("");
  base64< gzip< data > > gz_dat;
  base64< gzip< delta > > gz_del;
  file_path pth(idx(args, 1)());

  transaction_guard guard(app.db);
  packet_db_writer dbw(app);
  
  complete(app, idx(args, 0)(), old_rid);

  // find the old rev, manifest and file
  app.db.get_revision_manifest(old_rid, old_mid);
  app.db.get_manifest(old_mid, old_man);
  manifest_map::const_iterator i = old_man.find(pth);
  N(i != old_man.end(), 
    F("cannot find file %s revision %s") 
    % pth % old_rid);

  // fetch the new file input
  string s = get_stdin();
  pack(data(s), gz_dat);    
  new_fdata = file_data(gz_dat);  
  calculate_ident(new_fdata, new_fid);

  // diff and store the file edge
  old_fid = manifest_entry_id(i);
  app.db.get_file_version(old_fid, old_fdata);
  diff(old_fdata.inner(), new_fdata.inner(), gz_del);    
  dbw.consume_file_delta(old_fid, new_fid, 
                         file_delta(gz_del));

  // diff and store the manifest edge
  new_man = old_man;
  new_man[pth] = new_fid;
  calculate_ident(new_man, new_mid);
  diff(old_man, new_man, gz_del);
  dbw.consume_manifest_delta(old_mid, new_mid, 
                             manifest_delta(gz_del));

  // build and store a changeset and revision
  cs.apply_delta(pth, old_fid, new_fid);
  rev.new_manifest = new_mid;
  rev.edges.insert(std::make_pair(old_rid, 
                                  std::make_pair(old_mid, cs)));
  calculate_ident(rev, new_rid);
  write_revision_set(rev, rdata);
  dbw.consume_revision_data(new_rid, rdata);

  // take care of any extra certs
  guess_branch (old_rid, app, branchname);
  app.set_branch(branchname());

  if (args.size() == 3)
    log_message = idx(args, 2)();
  else
    get_log_message(rev, app, log_message);

  N(log_message.find_first_not_of(" \r\t\n") != string::npos,
    F("empty log message"));

  cert_revision_in_branch(new_rid, branchname, app, dbw); 
  cert_revision_date_now(new_rid, app, dbw);
  cert_revision_author_default(new_rid, app, dbw);
  cert_revision_changelog(new_rid, log_message, app, dbw);

  // finish off
  guard.commit();
}


CMD(fload, "debug", "", "load file contents into db")
{
  string s = get_stdin();
  base64< gzip< data > > gzd;

  app.initialize(false);

  pack(data(s), gzd);

  file_id f_id;
  file_data f_data(gzd);
  
  calculate_ident (f_data, f_id);
  
  packet_db_writer dbw(app);
  dbw.consume_file_data(f_id, f_data);  
}

CMD(fmerge, "debug", "<parent> <left> <right>", "merge 3 files and output result")
{
  if (args.size() != 3)
    throw usage(name);

  app.initialize(false);

  file_id anc_id(idx(args, 0)()), left_id(idx(args, 1)()), right_id(idx(args, 2)());
  file_data anc, left, right;
  data anc_unpacked, left_unpacked, right_unpacked;

  N(app.db.file_version_exists (anc_id),
  F("ancestor file id does not exist"));

  N(app.db.file_version_exists (left_id),
  F("left file id does not exist"));

  N(app.db.file_version_exists (right_id),
  F("right file id does not exist"));

  app.db.get_file_version(anc_id, anc);
  app.db.get_file_version(left_id, left);
  app.db.get_file_version(right_id, right);

  unpack(left.inner(), left_unpacked);
  unpack(anc.inner(), anc_unpacked);
  unpack(right.inner(), right_unpacked);

  vector<string> anc_lines, left_lines, right_lines, merged_lines;

  split_into_lines(anc_unpacked(), anc_lines);
  split_into_lines(left_unpacked(), left_lines);
  split_into_lines(right_unpacked(), right_lines);
  N(merge3(anc_lines, left_lines, right_lines, merged_lines), F("merge failed"));
  copy(merged_lines.begin(), merged_lines.end(), ostream_iterator<string>(cout, "\n"));
  
}

CMD(status, "informative", "[PATH]...", "show status of working copy")
{
  revision_set rs;
  manifest_map m_old, m_new;
  data tmp;

  app.initialize(true);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    app.add_restriction((*i)());

  calculate_restricted_revision(app, rs, m_old, m_new);

  write_revision_set(rs, tmp);
  cout << endl << tmp << endl;
}

CMD(identify, "working copy", "[PATH]",
    "calculate identity of PATH or stdin")
{
  if (!(args.size() == 0 || args.size() == 1))
    throw usage(name);

  app.initialize(false);

  data dat;

  if (args.size() == 1)
    {
      read_localized_data(file_path(idx(args, 0)()), dat, app.lua);
    }
  else
    {
      dat = get_stdin();
    }
  
  hexenc<id> ident;
  calculate_ident(dat, ident);
  cout << ident << endl;
}

CMD(cat, "informative", "(file|manifest|revision) [ID]", 
    "write file, manifest, or revision from database to stdout")
{
  if (!(args.size() == 1 || args.size() == 2))
    throw usage(name);

  app.initialize(false);

  transaction_guard guard(app.db);

  if (idx(args, 0)() == "file")
    {
      if (args.size() == 1)
        throw usage(name);

      file_data dat;
      file_id ident;
      complete(app, idx(args, 1)(), ident);

      N(app.db.file_version_exists(ident),
        F("no file version %s found in database") % ident);

      L(F("dumping file %s\n") % ident);
      app.db.get_file_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());

    }
  else if (idx(args, 0)() == "manifest")
    {
      manifest_data dat;
      manifest_id ident;

      if (args.size() == 1)
        {
          revision_set rev;
          manifest_map m_old, m_new;
          calculate_current_revision(app, rev, m_old, m_new);
          calculate_ident(m_new, ident);
          write_manifest_map(m_new, dat);
        }
      else
        {
          complete(app, idx(args, 1)(), ident);
          N(app.db.manifest_version_exists(ident),
            F("no manifest version %s found in database") % ident);
          app.db.get_manifest_version(ident, dat);
        }

      L(F("dumping manifest %s\n") % ident);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }

  else if (idx(args, 0)() == "revision")
    {
      revision_data dat;
      revision_id ident;

      if (args.size() == 1)
        {
          revision_set rev;
          manifest_map m_old, m_new;
          calculate_current_revision(app, rev, m_old, m_new);
          calculate_ident(rev, ident);
          write_revision_set(rev, dat);
        }
      else
        {
          complete(app, idx(args, 1)(), ident);
          N(app.db.revision_exists(ident),
            F("no revision %s found in database") % ident);
          app.db.get_revision(ident, dat);
        }

      L(F("dumping revision %s\n") % ident);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }
  else 
    throw usage(name);

  guard.commit();
}


CMD(checkout, "tree", "REVISION DIRECTORY\nDIRECTORY\n", 
    "check out revision from database into directory")
{

  revision_id ident;
  string dir;

  if (args.size() > 2)
    throw usage(name);

  if (args.size() == 0 || args.size() == 1)
    {
      N(app.branch_name() != "", F("need --branch argument for branch-based checkout"));

      // if no checkout dir specified, use branch name
      if (args.size() == 0)
          dir = app.branch_name();
      else
          dir = idx(args, 0)();

      app.initialize(dir);

      set<revision_id> heads;
      get_branch_heads(app.branch_name(), app, heads);
      N(heads.size() > 0, F("branch %s is empty") % app.branch_name);
      N(heads.size() == 1, F("branch %s has multiple heads") % app.branch_name);
      ident = *(heads.begin());
    }
  else
    {
      dir = idx(args, 1)();
      app.initialize(dir);

      complete(app, idx(args, 0)(), ident);
    }

  transaction_guard guard(app.db);
    
  file_data data;
  manifest_id mid;
  manifest_map m;

  N(app.db.revision_exists(ident),
    F("no revision %s found in database") % ident);

  app.db.get_revision_manifest(ident, mid);
  put_revision_id(ident);

  N(app.db.manifest_version_exists(mid),
    F("no manifest %s found in database") % ident);
  
  L(F("checking out revision %s to directory %s\n") % ident % dir);
  app.db.get_manifest(mid, m);
  
  for (manifest_map::const_iterator i = m.begin(); i != m.end(); ++i)
    {
      N(app.db.file_version_exists(manifest_entry_id(i)),
        F("no file %s found in database for %s")
        % manifest_entry_id(i) % manifest_entry_path(i));
      
      file_data dat;
      L(F("writing file %s to %s\n")
        % manifest_entry_id(i) % manifest_entry_path(i));
      app.db.get_file_version(manifest_entry_id(i), dat);
      write_localized_data(manifest_entry_path(i), dat.inner(), app.lua);
    }
  remove_path_rearrangement();
  guard.commit();
  update_any_attrs(app);
}

ALIAS(co, checkout, "tree", "REVISION DIRECTORY\nDIRECTORY",
      "check out revision from database; alias for checkout")

CMD(heads, "tree", "", "show unmerged head revisions of branch")
{
  set<revision_id> heads;
  if (args.size() != 0)
    throw usage(name);

  app.initialize(false);

  
  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  if (heads.size() == 0)
    P(F("branch '%s' is empty\n") % app.branch_name);
  else if (heads.size() == 1)
    P(F("branch '%s' is currently merged:\n") % app.branch_name);
  else
    P(F("branch '%s' is currently unmerged:\n") % app.branch_name);
  
  for (set<revision_id>::const_iterator i = heads.begin(); 
       i != heads.end(); ++i)
    cout << describe_revision(app, *i) << endl;
}

static void 
ls_branches(string name, app_state & app, vector<utf8> const & args)
{
  app.initialize(false);

  transaction_guard guard(app.db);
  vector< revision<cert> > certs;
  app.db.get_revision_certs(branch_cert_name, certs);

  vector<string> names;
  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_value name;
      decode_base64(idx(certs, i).inner().value, name);
      if (!app.lua.hook_ignore_branch(name()))
        names.push_back(name());
    }

  sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (size_t i = 0; i < names.size(); ++i)
    cout << idx(names, i) << endl;

  guard.commit();
}

static void 
ls_tags(string name, app_state & app, vector<utf8> const & args)
{
  app.initialize(false);

  transaction_guard guard(app.db);
  vector< revision<cert> > certs;
  app.db.get_revision_certs(tag_cert_name, certs);

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_value name;
      decode_base64(idx(certs, i).inner().value, name);
      cout << name << " " 
           << idx(certs,i).inner().ident  << " "
           << idx(certs,i).inner().key  << endl;
    }

  guard.commit();
}

struct unknown_itemizer : public tree_walker
{
  app_state & app;
  manifest_map & man;
  bool want_ignored;
  unknown_itemizer(app_state & a, manifest_map & m, bool i) 
    : app(a), man(m), want_ignored(i) {}
  virtual void visit_file(file_path const & path)
  {
    if (app.restriction_includes(path) && man.find(path) == man.end())
      {
      if (want_ignored)
        {
          if (app.lua.hook_ignore_file(path))
            cout << path() << endl;
        }
      else
        {
          if (!app.lua.hook_ignore_file(path))
            cout << path() << endl;
        }
      }
  }
};


static void
ls_unknown (app_state & app, bool want_ignored, vector<utf8> const & args)
{
  app.initialize(true);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    app.add_restriction((*i)());

  revision_set rev;
  manifest_map m_old, m_new;
  calculate_restricted_revision(app, rev, m_old, m_new);
  unknown_itemizer u(app, m_new, want_ignored);
  walk_tree(u);
}

static void
ls_missing (app_state & app, vector<utf8> const & args)
{
  revision_set rev;
  revision_id rid;
  manifest_id mid;
  manifest_map man, man_rearranged;
  change_set::path_rearrangement included, excluded;
  path_set old_paths, new_paths;

  app.initialize(true);

  get_revision_id(rid);
  if (! rid.inner()().empty())
    {
      N(app.db.revision_exists(rid),
        F("base revision %s does not exist in database\n") % rid);
      
      app.db.get_revision_manifest(rid, mid);
      L(F("old manifest is %s\n") % mid);
      
      N(app.db.manifest_version_exists(mid),
        F("base manifest %s does not exist in database\n") % mid);
      
      app.db.get_manifest(mid, man);
    }

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    app.add_restriction((*i)());

  L(F("old manifest has %d entries\n") % man.size());

  get_path_rearrangement(included, excluded, app);
  extract_path_set(man, old_paths);
  apply_path_rearrangement(old_paths, included, new_paths);

  for (path_set::const_iterator i = new_paths.begin(); i != new_paths.end(); ++i)
    {
      if (app.restriction_includes(*i) && !file_exists(*i))     
        cout << *i << endl;
    }
}


CMD(list, "informative", 
    "certs ID\n"
    "keys [PATTERN]\n"
    "branches\n"
    "tags\n"
    "unknown\n"
    "ignored\n"
    "missing", 
    "show certs, keys, branches, unknown, intentionally ignored, or missing files")
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  ++i;
  vector<utf8> removed (i, args.end());
  if (idx(args, 0)() == "certs")
    ls_certs(name, app, removed);
  else if (idx(args, 0)() == "keys")
    ls_keys(name, app, removed);
  else if (idx(args, 0)() == "branches")
    ls_branches(name, app, removed);
  else if (idx(args, 0)() == "tags")
    ls_tags(name, app, removed);
  else if (idx(args, 0)() == "unknown")
    ls_unknown(app, false, removed);
  else if (idx(args, 0)() == "ignored")
    ls_unknown(app, true, removed);
  else if (idx(args, 0)() == "missing")
    ls_missing(app, removed);
  else
    throw usage(name);
}

ALIAS(ls, list, "informative",  
      "certs ID\n"
      "keys [PATTERN]\n"
      "branches\n"
      "tags\n"
      "unknown\n"
      "ignored\n"
      "missing",
      "show certs, keys, branches, unknown, intentionally ignored, or missing files; alias for list")


CMD(mdelta, "packet i/o", "OLDID NEWID", "write manifest delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  manifest_id m_old_id, m_new_id; 
  manifest_map m_old, m_new;

  complete(app, idx(args, 0)(), m_old_id);
  complete(app, idx(args, 1)(), m_new_id);

  app.db.get_manifest(m_old_id, m_old);
  app.db.get_manifest(m_new_id, m_new);

  base64< gzip<delta> > del;
  diff(m_old, m_new, del);
  pw.consume_manifest_delta(m_old_id, m_new_id, 
                            manifest_delta(del));
}

CMD(fdelta, "packet i/o", "OLDID NEWID", "write file delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  file_id f_old_id, f_new_id;
  file_data f_old_data, f_new_data;

  complete(app, idx(args, 0)(), f_old_id);
  complete(app, idx(args, 1)(), f_new_id);

  app.db.get_file_version(f_old_id, f_old_data);
  app.db.get_file_version(f_new_id, f_new_data);
  base64< gzip<delta> > del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));  
}

CMD(rdata, "packet i/o", "ID", "write revision data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  revision_id r_id;
  revision_data r_data;

  complete(app, idx(args, 0)(), r_id);

  app.db.get_revision(r_id, r_data);
  pw.consume_revision_data(r_id, r_data);  
}

CMD(mdata, "packet i/o", "ID", "write manifest data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  manifest_id m_id;
  manifest_data m_data;

  complete(app, idx(args, 0)(), m_id);

  app.db.get_manifest_version(m_id, m_data);
  pw.consume_manifest_data(m_id, m_data);  
}


CMD(fdata, "packet i/o", "ID", "write file data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  file_id f_id;
  file_data f_data;

  complete(app, idx(args, 0)(), f_id);

  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);  
}


CMD(certs, "packet i/o", "ID", "write cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  packet_writer pw(cout);

  revision_id r_id;
  vector< revision<cert> > certs;

  complete(app, idx(args, 0)(), r_id);

  app.db.get_revision_certs(r_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_revision_cert(idx(certs, i));
}

CMD(pubkey, "packet i/o", "ID", "write public key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  rsa_keypair_id ident(idx(args, 0)());
  N(app.db.public_key_exists(ident),
    F("public key '%s' does not exist in database") % idx(args, 0)());

  packet_writer pw(cout);
  base64< rsa_pub_key > key;
  app.db.get_key(ident, key);
  pw.consume_public_key(ident, key);
}

CMD(privkey, "packet i/o", "ID", "write private key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  rsa_keypair_id ident(idx(args, 0)());
  N(app.db.private_key_exists(ident),
    F("private key '%s' does not exist in database") % idx(args, 0)());

  packet_writer pw(cout);
  base64< arc4<rsa_priv_key> > key;
  app.db.get_key(ident, key);
  pw.consume_private_key(ident, key);
}


CMD(read, "packet i/o", "", "read packets from stdin")
{
  app.initialize(false);

  packet_db_writer dbw(app, true);
  size_t count = read_packets(cin, dbw);
  N(count != 0, F("no packets found on stdin"));
  if (count == 1)
    P(F("read 1 packet\n"));
  else
    P(F("read %d packets\n") % count);
}


CMD(reindex, "network", "COLLECTION...", 
    "rebuild the hash-tree indices used to sync COLLECTION over the network")
{
  if (args.size() < 1)
    throw usage(name);

  app.initialize(false);

  transaction_guard guard(app.db);
  ui.set_tick_trailer("rehashing db");
  app.db.rehash();
  for (size_t i = 0; i < args.size(); ++i)
    {
      ui.set_tick_trailer(string("rebuilding hash-tree indices for ") + idx(args,i)());
      rebuild_merkle_trees(app, idx(args,i));
    }
  guard.commit();
}

CMD(push, "network", "ADDRESS[:PORTNUMBER] COLLECTION...",
    "push COLLECTION to netsync server at ADDRESS")
{
  if (args.size() < 2)
    throw usage(name);

  app.initialize(false);

  rsa_keypair_id key;
  N(guess_default_key(key, app), F("could not guess default signing key"));
  app.signing_key = key;

  utf8 addr(idx(args,0));
  vector<utf8> collections(args.begin() + 1, args.end());
  run_netsync_protocol(client_voice, source_role, addr, collections, app);  
}

CMD(pull, "network", "ADDRESS[:PORTNUMBER] COLLECTION...",
    "pull COLLECTION from netsync server at ADDRESS")
{
  if (args.size() < 2)
    throw usage(name);

  app.initialize(false);

  if (app.signing_key() == "")
    W(F("doing anonymous pull\n"));
  
  utf8 addr(idx(args,0));
  vector<utf8> collections(args.begin() + 1, args.end());
  run_netsync_protocol(client_voice, sink_role, addr, collections, app);  
}

CMD(sync, "network", "ADDRESS[:PORTNUMBER] COLLECTION...",
    "sync COLLECTION with netsync server at ADDRESS")
{
  if (args.size() < 2)
    throw usage(name);

  app.initialize(false);

  rsa_keypair_id key;
  N(guess_default_key(key, app), F("could not guess default signing key"));
  app.signing_key = key;

  utf8 addr(idx(args,0));
  vector<utf8> collections(args.begin() + 1, args.end());
  run_netsync_protocol(client_voice, source_and_sink_role, addr, collections, app);  
}

CMD(serve, "network", "ADDRESS[:PORTNUMBER] COLLECTION...",
    "listen on ADDRESS and serve COLLECTION to connecting clients")
{
  if (args.size() < 2)
    throw usage(name);

  app.initialize(false);

  rsa_keypair_id key;
  N(guess_default_key(key, app), F("could not guess default signing key"));
  app.signing_key = key;

  {
    N(app.lua.hook_persist_phrase_ok(),
      F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
    N(priv_key_exists(app, key),
      F("no private key '%s' found in database or get_priv_key hook") % key);
    N(app.db.public_key_exists(key),
      F("no public key '%s' found in database") % key);
    base64<rsa_pub_key> pub;
    app.db.get_key(key, pub);
    base64< arc4<rsa_priv_key> > priv;
    load_priv_key(app, key, priv);
    require_password(app.lua, key, pub, priv);
  }

  utf8 addr(idx(args,0));
  vector<utf8> collections(args.begin() + 1, args.end());
  run_netsync_protocol(server_voice, source_and_sink_role, addr, collections, app);  
}

static void
check_db(app_state & app)
{
  ticker revs("revs", ".");
  std::multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  std::set<revision_id> seen;
  for (std::multimap<revision_id, revision_id>::const_iterator i = graph.begin();
       i != graph.end(); ++i)
    {
      revision_set rev;
      if (seen.find(i->first) == seen.end())
        {
          if (app.db.revision_exists(i->first))
            {            
              app.db.get_revision(i->first, rev);
              seen.insert(i->first);
              ++revs;
            }
        }
      if (seen.find(i->second) == seen.end())
        {      
          if (app.db.revision_exists(i->second))
            {            
              app.db.get_revision(i->second, rev);
              seen.insert(i->second);
              ++revs;
            }
        }
    }
}


CMD(db, "database", "init\ninfo\nversion\ndump\nload\nmigrate\nexecute", "manipulate database state")
{
  app.initialize(false);

  if (args.size() == 1)
    {
      if (idx(args, 0)() == "init")
        app.db.initialize();
      else if (idx(args, 0)() == "info")
        app.db.info(cout);
      else if (idx(args, 0)() == "version")
        app.db.version(cout);
      else if (idx(args, 0)() == "dump")
        app.db.dump(cout);
      else if (idx(args, 0)() == "load")
        app.db.load(cin);
      else if (idx(args, 0)() == "migrate")
        app.db.migrate();
      else if (idx(args, 0)() == "fsck")
        check_db(app);
      else if (idx(args, 0)() == "changesetify")
        build_changesets_from_manifest_ancestry(app);
      else if (idx(args, 0)() == "rebuild")
        build_changesets_from_existing_revs(app);
      else
        throw usage(name);
    }
  else if (args.size() == 2)
    {
      if (idx(args, 0)() == "execute")
        app.db.debug(idx(args, 1)(), cout);
      else
        throw usage(name);
    }
  else
    throw usage(name);
}

CMD(attr, "working copy", "set FILE ATTR VALUE\nget FILE [ATTR]", 
    "get or set file attributes")
{
  if (args.size() < 2 || args.size() > 4)
    throw usage(name);

  app.initialize(true);

  data attr_data;
  file_path attr_path;
  attr_map attrs;
  get_attr_path(attr_path);

  if (file_exists(attr_path))
    {
      read_data(attr_path, attr_data);
      read_attr_map(attr_data, attrs);
    }
  
  file_path path;
  if (idx(args, 0)() == "set")
    {
      path = file_path(idx(args, 1)());
      if (args.size() != 4)
        throw usage(name);
      attrs[path][idx(args, 2)()] = idx(args, 3)();
      write_attr_map(attr_data, attrs);
      write_data(attr_path, attr_data);

      {
        // check to make sure .mt-attr exists in 
        // current manifest.
        manifest_map man;
        calculate_base_manifest(app, man);
        if (man.find(attr_path) == man.end())
          {
            P(F("registering %s file in working copy\n") % attr_path);
              change_set::path_rearrangement work;  
              get_path_rearrangement(work);
              build_addition(attr_path, man, app, work);
              put_path_rearrangement(work);
          }        
      }

    }
  else if (idx(args, 0)() == "get")
    {
      path = idx(args, 1)();
      if (args.size() != 2 && args.size() != 3)
        throw usage(name);

      attr_map::const_iterator i = attrs.find(path);
      if (i == attrs.end())
        cout << "no attributes for " << path << endl;
      else
        {
          if (args.size() == 2)
            {
              for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
                   j != i->second.end(); ++j)
                cout << path << " : " << j->first << "=" << j->second << endl;
            }
          else
            {       
              std::map<std::string, std::string>::const_iterator j = i->second.find(idx(args, 2)());
              if (j == i->second.end())
                cout << "no attribute " << idx(args, 2)() << " on file " << path << endl;
              else
                cout << path << " : " << j->first << "=" << j->second << endl;
            }
        }
    }
  else 
    throw usage(name);
}
CMD(commit, "working copy", "[--message=STRING] [PATH]...", 
    "commit working copy to database")
{
  string log_message("");
  revision_set rs;
  revision_id rid;
  manifest_map m_old, m_new;
  
  app.initialize(true);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    app.add_restriction((*i)());

  // preserve excluded work for future commmits
  change_set::path_rearrangement excluded_work;
  calculate_restricted_revision(app, rs, m_old, m_new, excluded_work);
  calculate_ident(rs, rid);

  N(!(rs.edges.size() == 0 || 
      edge_changes(rs.edges.begin()).empty()), 
    F("no changes to commit\n"));
    
  cert_value branchname;
  I(rs.edges.size() == 1);

  guess_branch (edge_old_revision(rs.edges.begin()), app, branchname);
  app.set_branch(branchname());
    
  P(F("beginning commit\n"));
  P(F("manifest %s\n") % rs.new_manifest);
  P(F("revision %s\n") % rid);
  P(F("branch %s\n") % branchname);

  // get log message
  if (app.message().length() > 0)
    log_message = app.message();
  else
    get_log_message(rs, app, log_message);

  N(log_message.find_first_not_of(" \r\t\n") != string::npos,
    F("empty log message"));
  
  transaction_guard guard(app.db);
  {
    packet_db_writer dbw(app);
  
    if (app.db.revision_exists(rid))
      {
        L(F("revision %s already in database\n") % rid);
      }
    else
      {
        // new revision
        L(F("inserting new revision %s\n") % rid);
      
        I(rs.edges.size() == 1);
        edge_map::const_iterator edge = rs.edges.begin();
        I(edge != rs.edges.end());
      
        // process manifest delta or new manifest
        if (app.db.manifest_version_exists(rs.new_manifest))
          {
            L(F("skipping manifest %s, already in database\n") % rs.new_manifest);
          }
        else if (app.db.manifest_version_exists(edge_old_manifest(edge)))
          {
            L(F("inserting manifest delta %s -> %s\n") 
              % edge_old_manifest(edge) 
              % rs.new_manifest);
            base64< gzip<delta> > del;
            diff(m_old, m_new, del);
            dbw.consume_manifest_delta(edge_old_manifest(edge), 
                                       rs.new_manifest, 
                                       manifest_delta(del));
          }
        else
          {
            L(F("inserting full manifest %s\n") % rs.new_manifest);
            manifest_data m_new_data;
            write_manifest_map(m_new, m_new_data);
            dbw.consume_manifest_data(rs.new_manifest, m_new_data);
          }
      
        // process file deltas or new files
        for (change_set::delta_map::const_iterator i = edge_changes(edge).deltas.begin();
             i != edge_changes(edge).deltas.end(); ++i)
          {
            if (! delta_entry_src(i).inner()().empty() && 
                app.db.file_version_exists(delta_entry_dst(i)))
              {
                L(F("skipping file delta %s, already in database\n") 
                  % delta_entry_dst(i));
              }
            else if (! delta_entry_src(i).inner()().empty() && 
                     app.db.file_version_exists(delta_entry_src(i)))
              {
                L(F("inserting delta %s -> %s\n") 
                  % delta_entry_src(i) % delta_entry_dst(i));
                file_data old_data;
                base64< gzip<data> > new_data;
                app.db.get_file_version(delta_entry_src(i), old_data);
                read_localized_data(delta_entry_path(i), new_data, app.lua);
                // sanity check
                hexenc<id> tid;
                calculate_ident(new_data, tid);
                I(tid == delta_entry_dst(i).inner());
                base64< gzip<delta> > del;
                diff(old_data.inner(), new_data, del);
                dbw.consume_file_delta(delta_entry_src(i), 
                                       delta_entry_dst(i), 
                                       file_delta(del));
              }
            else
              {
                L(F("inserting full version %s\n") % delta_entry_dst(i));
                base64< gzip<data> > new_data;
                read_localized_data(delta_entry_path(i), new_data, app.lua);
                // sanity check
                hexenc<id> tid;
                calculate_ident(new_data, tid);
                I(tid == delta_entry_dst(i).inner());
                dbw.consume_file_data(delta_entry_dst(i), file_data(new_data));
              }
          }
      }

    revision_data rdat;
    write_revision_set(rs, rdat);
    dbw.consume_revision_data(rid, rdat);
  
    cert_revision_in_branch(rid, branchname, app, dbw); 
    cert_revision_date_now(rid, app, dbw);
    cert_revision_author_default(rid, app, dbw);
    cert_revision_changelog(rid, log_message, app, dbw);
  }
  
  guard.commit();

  // small race condition here...
  put_path_rearrangement(excluded_work);
  put_revision_id(rid);
  P(F("committed revision %s\n") % rid);

  update_any_attrs(app);

  {
    // tell lua what happened. yes, we might lose some information here,
    // but it's just an indicator for lua, eg. to post stuff to a mailing
    // list. if the user *really* cares about cert validity, multiple certs
    // with same name, etc.  they can inquire further, later.
    map<cert_name, cert_value> certs;
    vector< revision<cert> > ctmp;
    app.db.get_revision_certs(rid, ctmp);
    for (vector< revision<cert> >::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      {
        cert_value vtmp;
        decode_base64(i->inner().value, vtmp);
        certs.insert(make_pair(i->inner().name, vtmp));
      }
    app.lua.hook_note_commit(rid, certs);
  }
}


static void 
dump_diffs(change_set::delta_map const & deltas,
           app_state & app,
           bool new_is_archived,
           diff_type type)
{
  
  for (change_set::delta_map::const_iterator i = deltas.begin();
       i != deltas.end(); ++i)
    {
      if (null_id(delta_entry_src(i)))
        {
          data unpacked;
          vector<string> lines;
          
          if (new_is_archived)
            {
              file_data dat;
              app.db.get_file_version(delta_entry_dst(i), dat);
              unpack(dat.inner(), unpacked);
            }
          else
            {
              read_localized_data(delta_entry_path(i), 
                                  unpacked, app.lua);
            }
          
          if (guess_binary(unpacked()))
            cout << "# " << delta_entry_path(i) << " is binary\n";
          else
            {     
              split_into_lines(unpacked(), lines);
              if (! lines.empty())
                {
                  cout << (F("--- %s\n") % delta_entry_path(i))
                       << (F("+++ %s\n") % delta_entry_path(i))
                       << (F("@@ -0,0 +1,%d @@\n") % lines.size());
                  for (vector<string>::const_iterator j = lines.begin();
                       j != lines.end(); ++j)
                    {
                      cout << "+" << *j << endl;
                    }
                }
            }
        }
      else
        {
          file_data f_old;
          gzip<data> decoded_old;
          data decompressed_old, decompressed_new;
          vector<string> old_lines, new_lines;
          
          app.db.get_file_version(delta_entry_src(i), f_old);
          decode_base64(f_old.inner(), decoded_old);
          decode_gzip(decoded_old, decompressed_old);
          
          if (new_is_archived)
            {
              file_data f_new;
              gzip<data> decoded_new;
              app.db.get_file_version(delta_entry_dst(i), f_new);
              decode_base64(f_new.inner(), decoded_new);
              decode_gzip(decoded_new, decompressed_new);
            }
          else
            {
              read_localized_data(delta_entry_path(i), 
                                  decompressed_new, app.lua);
            }

          if (guess_binary(decompressed_new()) || 
              guess_binary(decompressed_old()))
            cout << "# " << delta_entry_path(i) << " is binary\n";
          else
            {
              split_into_lines(decompressed_old(), old_lines);
              split_into_lines(decompressed_new(), new_lines);
              make_diff(delta_entry_path(i)(), 
                        delta_entry_path(i)(), 
                        old_lines, new_lines,
                        cout, type);
            }
        }
    }
}

void do_diff(const string & name, 
             app_state & app, 
             vector<utf8> const & args, 
             diff_type type)
{
  revision_set r_old, r_new;
  manifest_map m_new;
  bool new_is_archived;

  change_set composite;

  // initialize before transaction so we have a database to work with

  if (app.revision_selectors.size() == 0)
      app.initialize(true);
  else if (app.revision_selectors.size() == 1)
      app.initialize(true);
  else if (app.revision_selectors.size() == 2)
      app.initialize(false);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    app.add_restriction((*i)());

  transaction_guard guard(app.db);

  if (app.revision_selectors.size() == 0)
    {
      manifest_map m_old;
      calculate_restricted_revision(app, r_new, m_old, m_new);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      if (r_new.edges.size() == 1)
        composite = edge_changes(r_new.edges.begin());
      new_is_archived = false;
    }
  else if (app.revision_selectors.size() == 1)
    {
      revision_id r_old_id;
      manifest_map m_old;
      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      N(app.db.revision_exists(r_old_id),
        F("revision %s does not exist") % r_old_id);
      app.db.get_revision(r_old_id, r_old);
      calculate_restricted_revision(app, r_new, m_old, m_new);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      N(r_new.edges.size() == 1, F("current revision has no ancestor"));
      new_is_archived = false;
    }
  else if (app.revision_selectors.size() == 2)
    {
      revision_id r_old_id, r_new_id;
      manifest_id m_new_id;

      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      complete(app, idx(app.revision_selectors, 1)(), r_new_id);

      N(app.db.revision_exists(r_old_id),
        F("revision %s does not exist") % r_old_id);
      app.db.get_revision(r_old_id, r_old);

      N(app.db.revision_exists(r_new_id),
        F("revision %s does not exist") % r_new_id);
      app.db.get_revision(r_new_id, r_new);

      app.db.get_revision_manifest(r_new_id, m_new_id);
      app.db.get_manifest(m_new_id, m_new);

      new_is_archived = true;
    }
  else
    {
      throw usage(name);
    }
      
  if (app.revision_selectors.size() > 0)
    {
      revision_id new_id, src_id, dst_id, anc_id;
      calculate_ident(r_old, src_id);
      calculate_ident(r_new, new_id);
      if (new_is_archived)
        dst_id = new_id;
      else
        {
          I(r_new.edges.size() == 1);
          dst_id = edge_old_revision(r_new.edges.begin());
        }

      N(find_least_common_ancestor(src_id, dst_id, anc_id, app),
        F("no common ancestor for %s and %s") % src_id % dst_id);

      if (src_id == anc_id)
        {
          calculate_composite_change_set(src_id, dst_id, app, composite);
          L(F("calculated diff via direct analysis\n"));
        }

      else if (!(src_id == anc_id) && dst_id == anc_id)
        {
          change_set tmp;
          calculate_composite_change_set(dst_id, src_id, app, tmp);
          invert_change_set(tmp, m_new, composite);
          L(F("calculated diff via inverted direct analysis\n"));
        }

      else
        {
          change_set anc_to_src, src_to_anc, anc_to_dst;
          manifest_id anc_m_id;
          manifest_map m_anc;

          I(!(src_id == anc_id || dst_id == anc_id));

          app.db.get_revision_manifest(anc_id, anc_m_id);
          app.db.get_manifest(anc_m_id, m_anc);

          calculate_composite_change_set(anc_id, src_id, app, anc_to_src);
          invert_change_set(anc_to_src, m_anc, src_to_anc);
          calculate_composite_change_set(anc_id, dst_id, app, anc_to_dst);
          concatenate_change_sets(src_to_anc, anc_to_dst, composite);
          L(F("calculated diff via common ancestor %s\n") % anc_id);
        }

      if (!new_is_archived)
        {
          L(F("concatenating un-committed changeset to composite\n"));
          change_set tmp;
          I(r_new.edges.size() == 1);
          concatenate_change_sets(composite, edge_changes(r_new.edges.begin()), tmp);
          composite = tmp;
        }

    }

  data summary;
  write_change_set(composite, summary);

  vector<string> lines;
  split_into_lines(summary(), lines);
  cout << "# " << endl;
  if (summary().size() > 0) 
    {
      for (vector<string>::iterator i = lines.begin(); i != lines.end(); ++i)
        cout << "# " << *i << endl;
    }
  else
    {
      cout << F("# no changes") << endl;
    }
  cout << "# " << endl;

  dump_diffs(composite.deltas, app, new_is_archived, type);
}

CMD(cdiff, "informative", "[--revision=REVISION [--revision=REVISION]] [PATH]...", 
    "show current context diffs on stdout")
{
  do_diff(name, app, args, context_diff);
}

CMD(diff, "informative", "[--revision=REVISION [--revision=REVISION]] [PATH]...", 
    "show current unified diffs on stdout")
{
  do_diff(name, app, args, unified_diff);
}

CMD(lca, "debug", "LEFT RIGHT", "print least common ancestor")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  revision_id anc, left, right;

  complete(app, idx(args, 0)(), left);
  complete(app, idx(args, 1)(), right);

  if (find_least_common_ancestor(left, right, anc, app))
    std::cout << anc << std::endl;
  else
    std::cout << "no common ancestor found" << std::endl;
}


CMD(lcad, "debug", "LEFT RIGHT", "print least common ancestor / dominator")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  revision_id anc, left, right;

  complete(app, idx(args, 0)(), left);
  complete(app, idx(args, 1)(), right);

  if (find_common_ancestor_for_merge(left, right, anc, app))
    std::cout << anc << std::endl;
  else
    std::cout << "no common ancestor/dominator found" << std::endl;
}


CMD(agraph, "debug", "", "dump ancestry graph to stdout")
{
  app.initialize(false);

  set<revision_id> nodes;
  multimap<revision_id,string> branches;

  std::multimap<revision_id, revision_id> edges_mmap;
  set<pair<revision_id, revision_id> > edges;

  app.db.get_revision_ancestry(edges_mmap);

  // convert from a weak lexicographic order to a strong one
  for (std::multimap<revision_id, revision_id>::const_iterator i = edges_mmap.begin();
       i != edges_mmap.end(); ++i)
    edges.insert(std::make_pair(i->first, i->second));

  for (set<pair<revision_id, revision_id> >::const_iterator i = edges.begin();
       i != edges.end(); ++i)
    {
      nodes.insert(i->first);
      nodes.insert(i->second);
    }

  vector< revision<cert> > certs;
  app.db.get_revision_certs(branch_cert_name, certs);
  for(vector< revision<cert> >::iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      revision_id tmp(i->inner().ident);
      nodes.insert(tmp); // in case no edges were connected
      branches.insert(make_pair(tmp, tv()));
    }  


  cout << "graph: " << endl << "{" << endl; // open graph
  for (set<revision_id>::iterator i = nodes.begin(); i != nodes.end();
       ++i)
    {
      cout << "node: { title : \"" << *i << "\"\n"
           << "        label : \"\\fb" << *i;
      pair<multimap<revision_id,string>::const_iterator,
        multimap<revision_id,string>::const_iterator> pair =
        branches.equal_range(*i);
      for (multimap<revision_id,string>::const_iterator j = pair.first;
           j != pair.second; ++j)
        {
          cout << "\\n\\fn" << j->second;
        }
      cout << "\"}" << endl;
    }
  for (set<pair<revision_id, revision_id> >::iterator i = edges.begin(); i != edges.end();
       ++i)
    {
      cout << "edge: { sourcename : \"" << i->first << "\"" << endl
           << "        targetname : \"" << i->second << "\" }" << endl;
    }
  cout << "}" << endl << endl; // close graph
}


static void
write_file_targets(change_set const & cs,
                   update_merge_provider & merger,
                   app_state & app)
{

  manifest_map files_to_write;
  for (change_set::delta_map::const_iterator i = cs.deltas.begin();
       i != cs.deltas.end(); ++i)
    {
      file_path pth(delta_entry_path(i));
      file_id ident(delta_entry_dst(i));
      
      if (file_exists(pth))
        {
          hexenc<id> tmp_id;
          calculate_ident(pth, tmp_id, app.lua);
          if (tmp_id == ident.inner())
            continue;
        }
      
      P(F("updating %s to %s\n") % pth % ident);
      
      I(app.db.file_version_exists(ident)
        || merger.temporary_store.find(ident) != merger.temporary_store.end());
      
      file_data tmp;
      if (app.db.file_version_exists(ident))
        app.db.get_file_version(ident, tmp);
      else if (merger.temporary_store.find(ident) != merger.temporary_store.end())
        tmp = merger.temporary_store[ident];    
      write_localized_data(pth, tmp.inner(), app.lua);
    }
}
  

// static void dump_change_set(string const & name,
//                          change_set & cs)
// {
//   data dat;
//   write_change_set(cs, dat);
//   cout << "change set '" << name << "'\n" << dat << endl;
// }

CMD(update, "working copy", "\nREVISION", "update working copy to be based off another revision")
{
  manifest_map m_old, m_ancestor, m_working, m_chosen;
  manifest_id m_ancestor_id, m_chosen_id;
  revision_set r_old, r_working, r_new;
  revision_id r_old_id, r_chosen_id;
  change_set old_to_chosen, update;

  if (args.size() != 0 && args.size() != 1)
    throw usage(name);

  app.initialize(true);
  calculate_current_revision(app, r_working, m_old, m_working);
  
  I(r_working.edges.size() == 1 || r_working.edges.size() == 0);
  if (r_working.edges.size() == 1)
    {
      r_old_id = edge_old_revision(r_working.edges.begin());
    }

  if (args.size() == 0)
    {
      set<revision_id> candidates;
      pick_update_candidates(r_old_id, app, candidates);
      N(candidates.size() != 0,
        F("no candidates remain after selection"));
      if (candidates.size() != 1)
        {
          P(F("multiple update candidates:\n"));
          for (set<revision_id>::const_iterator i = candidates.begin();
               i != candidates.end(); ++i)
            P(F("  %s\n") % describe_revision(app, *i));
          P(F("choose one with 'monotone update <id>'\n"));
          N(false, F("multiple candidates remain after selection"));
        }
      r_chosen_id = *(candidates.begin());
    }
  else
    complete(app, idx(args, 0)(), r_chosen_id);

  if (r_old_id == r_chosen_id)
    {
      P(F("already up to date at %s\n") % r_old_id);
      return;
    }

  P(F("selected update target %s\n") % r_chosen_id);
  app.db.get_revision_manifest(r_chosen_id, m_chosen_id);
  app.db.get_manifest(m_chosen_id, m_chosen);

  if (args.size() == 0)
    {
      calculate_composite_change_set(r_old_id, r_chosen_id, app, old_to_chosen);
      m_ancestor = m_old;
    }
  else
    {
      revision_id r_ancestor_id;

      N(find_least_common_ancestor(r_old_id, r_chosen_id, r_ancestor_id, app),
        F("no common ancestor for %s and %s\n") % r_old_id % r_chosen_id);
      L(F("old is %s\n") % r_old_id);
      L(F("chosen is %s\n") % r_chosen_id);
      L(F("common ancestor is %s\n") % r_ancestor_id);

      app.db.get_revision_manifest(r_ancestor_id, m_ancestor_id);
      app.db.get_manifest(m_ancestor_id, m_ancestor);

      if (r_ancestor_id == r_old_id)
        calculate_composite_change_set(r_old_id, r_chosen_id, app, old_to_chosen);
      else if (r_ancestor_id == r_chosen_id)
        {
          change_set chosen_to_old;
          calculate_composite_change_set(r_chosen_id, r_old_id, app, chosen_to_old);
          invert_change_set(chosen_to_old, m_chosen, old_to_chosen);
        }
      else
        {
          change_set ancestor_to_old;
          change_set old_to_ancestor;
          change_set ancestor_to_chosen;
          calculate_composite_change_set(r_ancestor_id, r_old_id, app, ancestor_to_old);
          invert_change_set(ancestor_to_old, m_ancestor, old_to_ancestor);
          calculate_composite_change_set(r_ancestor_id, r_chosen_id, app, ancestor_to_chosen);
          concatenate_change_sets(old_to_ancestor, ancestor_to_chosen, old_to_chosen);
        }
    }

  update_merge_provider merger(app, m_ancestor, m_chosen, m_working);

  if (r_working.edges.size() == 0)
    {
      // working copy has no changes
      L(F("updating along chosen edge %s -> %s\n") 
        % r_old_id % r_chosen_id);
      update = old_to_chosen;
    }
  else
    {      
      change_set 
        old_to_working(edge_changes(r_working.edges.begin())),
        working_to_merged, 
        chosen_to_merged;

      L(F("merging working copy with chosen edge %s -> %s\n") 
        % r_old_id % r_chosen_id);

      merge_change_sets(old_to_chosen, 
                        old_to_working, 
                        chosen_to_merged, 
                        working_to_merged, 
                        merger, app);
      // dump_change_set("chosen to merged", chosen_to_merged);
      // dump_change_set("working to merged", working_to_merged);

      update = working_to_merged;
    }
  
  local_path tmp_root((mkpath(book_keeping_dir) / mkpath("tmp")).string());
  if (directory_exists(tmp_root))
    delete_dir_recursive(tmp_root);

  mkdir_p(tmp_root);
  apply_rearrangement_to_filesystem(update.rearrangement, tmp_root);
  write_file_targets(update, merger, app);

  if (directory_exists(tmp_root))
    delete_dir_recursive(tmp_root);
  
  // small race condition here...
  // nb: we write out r_chosen, not r_new, because the revision-on-disk
  // is the basis of the working copy, not the working copy itself.
  put_revision_id(r_chosen_id);
  P(F("updated to base revision %s\n") % r_chosen_id);

  update_any_attrs(app);
}



// this helper tries to produce merge <- mergeN(left,right); it searches
// for a common ancestor and if none is found synthesizes a common one with
// no contents. it then computes composite changesets via the common
// ancestor and does a 3-way merge.

static void 
try_one_merge(revision_id const & left_id,
              revision_id const & right_id,
              revision_id const & ancestor_id, // empty ==> use common ancestor
              revision_id & merged_id,
              app_state & app)
{
  revision_id anc_id;
  revision_set left_rev, right_rev, anc_rev, merged_rev;

  app.db.get_revision(left_id, left_rev);
  app.db.get_revision(right_id, right_rev);
  
  packet_db_writer dbw(app);    
    
  manifest_map anc_man, left_man, right_man, merged_man;
  
  change_set 
    anc_to_left, anc_to_right, 
    left_to_merged, right_to_merged;
  
  app.db.get_manifest(right_rev.new_manifest, right_man);
  app.db.get_manifest(left_rev.new_manifest, left_man);
  
  // Make sure that we can't create malformed graphs where the left parent is
  // a descendent or ancestor of the right, or where both parents are equal,
  // etc.
  {
    set<revision_id> ids;
    ids.insert(left_id);
    ids.insert(right_id);
    erase_ancestors(ids, app);
    I(ids.size() == 2);
  }

  if (!null_id(ancestor_id))
    {
      I(is_ancestor(ancestor_id, left_id, app));
      I(is_ancestor(ancestor_id, right_id, app));

      anc_id = ancestor_id;

      app.db.get_revision(anc_id, anc_rev);
      app.db.get_manifest(anc_rev.new_manifest, anc_man);

      calculate_composite_change_set(anc_id, left_id, app, anc_to_left);
      calculate_composite_change_set(anc_id, right_id, app, anc_to_right);
    }
  else if (find_common_ancestor_for_merge(left_id, right_id, anc_id, app))
    {     
      P(F("common ancestor %s found\n") % anc_id); 
      P(F("trying 3-way merge\n"));
      
      app.db.get_revision(anc_id, anc_rev);
      app.db.get_manifest(anc_rev.new_manifest, anc_man);
      
      calculate_composite_change_set(anc_id, left_id, app, anc_to_left);
      calculate_composite_change_set(anc_id, right_id, app, anc_to_right);
    }
  else
    {
      P(F("no common ancestor found, synthesizing edges\n")); 
      build_pure_addition_change_set(left_man, anc_to_left);
      build_pure_addition_change_set(right_man, anc_to_right);
    }
  
  merge_provider merger(app, anc_man, left_man, right_man);
  
  merge_change_sets(anc_to_left, anc_to_right, 
                    left_to_merged, right_to_merged, 
                    merger, app);
  
  {
    // we have to record *some* route to this manifest. we pick the
    // smaller of the two.
    manifest_map tmp;
    apply_change_set(anc_man, anc_to_left, tmp);
    apply_change_set(tmp, left_to_merged, merged_man);
    calculate_ident(merged_man, merged_rev.new_manifest);
    base64< gzip<delta> > left_mdelta, right_mdelta;
    diff(left_man, merged_man, left_mdelta);
    diff(right_man, merged_man, right_mdelta);
    if (left_mdelta().size() < right_mdelta().size())
      dbw.consume_manifest_delta(left_rev.new_manifest, 
                                 merged_rev.new_manifest, left_mdelta);
    else
      dbw.consume_manifest_delta(right_rev.new_manifest, 
                                 merged_rev.new_manifest, right_mdelta);
  }
  
  merged_rev.edges.insert(std::make_pair(left_id,
                                         std::make_pair(left_rev.new_manifest,
                                                        left_to_merged)));
  merged_rev.edges.insert(std::make_pair(right_id,
                                         std::make_pair(right_rev.new_manifest,
                                                        right_to_merged)));
  revision_data merged_data;
  write_revision_set(merged_rev, merged_data);
  calculate_ident(merged_data, merged_id);
  dbw.consume_revision_data(merged_id, merged_data);
  cert_revision_date_now(merged_id, app, dbw);
  cert_revision_author_default(merged_id, app, dbw);
}                         


CMD(merge, "tree", "", "merge unmerged heads of branch")
{
  set<revision_id> heads;

  if (args.size() != 0)
    throw usage(name);

  app.initialize(false);
  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  N(heads.size() != 0, F("branch '%s' is empty\n") % app.branch_name);
  N(heads.size() != 1, F("branch '%s' is merged\n") % app.branch_name);

  set<revision_id>::const_iterator i = heads.begin();
  revision_id left = *i;
  revision_id ancestor;
  size_t count = 1;
  P(F("starting with revision 1 / %d\n") % heads.size());
  for (++i; i != heads.end(); ++i, ++count)
    {
      revision_id right = *i;
      P(F("merging with revision %d / %d\n") % (count + 1) % heads.size());
      P(F("[source] %s\n") % left);
      P(F("[source] %s\n") % right);

      revision_id merged;
      transaction_guard guard(app.db);
      try_one_merge(left, right, revision_id(), merged, app);
                  
      // merged 1 edge; now we commit this, update merge source and
      // try next one

      packet_db_writer dbw(app);
      cert_revision_in_branch(merged, app.branch_name(), app, dbw);

      string log = (F("merge of %s\n"
                      "     and %s\n") % left % right).str();
      cert_revision_changelog(merged, log, app, dbw);
          
      guard.commit();
      P(F("[merged] %s\n") % merged);
      left = merged;
    }

}

CMD(propagate, "tree", "SOURCE-BRANCH DEST-BRANCH", 
    "merge from one branch to another asymmetrically")
{
  //   this is a special merge operator, but very useful for people maintaining
  //   "slightly disparate but related" trees. it does a one-way merge; less
  //   powerful than putting things in the same branch and also more flexible.
  //
  //   1. check to see if src and dst branches are merged, if not abort, if so
  //   call heads N1 and N2 respectively.
  //
  //   2. (FIXME: not yet present) run the hook propagate ("src-branch",
  //   "dst-branch", N1, N2) which gives the user a chance to massage N1 into
  //   a state which is likely to "merge nicely" with N2, eg. edit pathnames,
  //   omit optional files of no interest.
  //
  //   3. do a normal 2 or 3-way merge on N1 and N2, depending on the
  //   existence of common ancestors.
  //
  //   4. save the results as the delta (N2,M), the ancestry edges (N1,M)
  //   and (N2,M), and the cert (N2,dst).
  //
  //   there are also special cases we have to check for where no merge is
  //   actually necessary, because there hasn't been any divergence since the
  //   last time propagate was run.
  
  set<revision_id> src_heads, dst_heads;

  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  get_branch_heads(idx(args, 0)(), app, src_heads);
  get_branch_heads(idx(args, 1)(), app, dst_heads);

  N(src_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 0)());
  N(src_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 0)());

  N(dst_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 1)());
  N(dst_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 1)());

  set<revision_id>::const_iterator src_i = src_heads.begin();
  set<revision_id>::const_iterator dst_i = dst_heads.begin();
  
  P(F("propagating %s -> %s\n") % idx(args,0) % idx(args,1));
  P(F("[source] %s\n") % *src_i);
  P(F("[target] %s\n") % *dst_i);

  // check for special cases
  if (*src_i == *dst_i || is_ancestor(*src_i, *dst_i, app))
    {
      P(F("branch '%s' is up-to-date with respect to branch '%s'\n")
          % idx(args, 1)() % idx(args, 0)());
      P(F("no action taken\n"));
    }
  else if (is_ancestor(*dst_i, *src_i, app))
    {
      P(F("no merge necessary; putting %s in branch '%s'\n")
        % (*src_i) % idx(args, 1)());
      transaction_guard guard(app.db);
      packet_db_writer dbw(app);
      cert_revision_in_branch(*src_i, idx(args, 1)(), app, dbw);
      guard.commit();
    }
  else
    {
      revision_id merged;
      transaction_guard guard(app.db);
      try_one_merge(*src_i, *dst_i, revision_id(), merged, app);

      packet_db_writer dbw(app);

      cert_revision_in_branch(merged, idx(args, 1)(), app, dbw);

      string log = (F("propagate from branch '%s' (head %s)\n"
                      "            to branch '%s' (head %s)\n")
                    % idx(args, 0) % (*src_i)
                    % idx(args, 1) % (*dst_i)).str();

      cert_revision_changelog(merged, log, app, dbw);

      guard.commit();      
    }
}

CMD(explicit_merge, "tree", "LEFT-REVISION RIGHT-REVISION DEST-BRANCH\nLEFT-REVISION RIGHT-REVISION COMMON-ANCESTOR DEST-BRANCH",
    "merge two explicitly given revisions, placing result in given branch")
{
  revision_id left, right, ancestor;
  string branch;

  if (args.size() != 3 && args.size() != 4)
    throw usage(name);

  app.initialize(false);

  complete(app, idx(args, 0)(), left);
  complete(app, idx(args, 1)(), right);
  if (args.size() == 4)
    {
      complete(app, idx(args, 2)(), ancestor);
      N(is_ancestor(ancestor, left, app),
        F("%s is not an ancestor of %s") % ancestor % left);
      N(is_ancestor(ancestor, right, app),
        F("%s is not an ancestor of %s") % ancestor % right);
      branch = idx(args, 3)();
    }
  else
    {
      branch = idx(args, 2)();
    }
  
  N(!(left == right),
    F("%s and %s are the same revision, aborting") % left % right);
  N(!is_ancestor(left, right, app),
    F("%s is already an ancestor of %s") % left % right);
  N(!is_ancestor(right, left, app),
    F("%s is already an ancestor of %s") % right % left);

  // Somewhat redundant, but consistent with output of plain "merge" command.
  P(F("[source] %s\n") % left);
  P(F("[source] %s\n") % right);

  revision_id merged;
  transaction_guard guard(app.db);
  try_one_merge(left, right, ancestor, merged, app);
  
  packet_db_writer dbw(app);
  
  cert_revision_in_branch(merged, branch, app, dbw);
  
  string log = (F("explicit_merge of %s\n"
                  "              and %s\n"
                  "   using ancestor %s\n"
                  "        to branch '%s'\n")
                % left % right % ancestor % branch).str();
  
  cert_revision_changelog(merged, log, app, dbw);
  
  guard.commit();      
  P(F("[merged] %s\n") % merged);
}

CMD(complete, "informative", "(revision|manifest|file) PARTIAL-ID", "complete partial id")
{
  if (args.size() != 2)
    throw usage(name);

  app.initialize(false);

  if (idx(args, 0)() == "revision")
    {      
      N(idx(args, 1)().find_first_not_of("abcdef0123456789") == string::npos,
        F("non-hex digits in partial id"));
      set<revision_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<revision_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << endl;
    }
  else if (idx(args, 0)() == "manifest")
    {      
      N(idx(args, 1)().find_first_not_of("abcdef0123456789") == string::npos,
        F("non-hex digits in partial id"));
      set<manifest_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<manifest_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << endl;
    }
  else if (idx(args, 0)() == "file")
    {
      N(idx(args, 1)().find_first_not_of("abcdef0123456789") == string::npos,
        F("non-hex digits in partial id"));
      set<file_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<file_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << endl;
    }
  else
    throw usage(name);  
}


CMD(revert, "working copy", "[PATH]...", 
    "revert file(s) or entire working copy")
{
  manifest_map m_old;
  revision_set r_old;

  app.initialize(true);

  calculate_base_revision(app, r_old, m_old);

  if (args.size() == 0)
    {
      // revert the whole thing
      for (manifest_map::const_iterator i = m_old.begin(); i != m_old.end(); ++i)
        {

          N(app.db.file_version_exists(manifest_entry_id(i)),
            F("no file version %s found in database for %s")
            % manifest_entry_id(i) % manifest_entry_path(i));
      
          file_data dat;
          L(F("writing file %s to %s\n")
            % manifest_entry_id(i) % manifest_entry_path(i));
          app.db.get_file_version(manifest_entry_id(i), dat);
          write_localized_data(manifest_entry_path(i), dat.inner(), app.lua);
        }
      remove_path_rearrangement();
    }
  else
    {
      change_set::path_rearrangement work;
      get_path_rearrangement(work);

      // TODO: set up restriction
      // TODO: restrict rearrangement into included and excluded
      // TODO: revert all included files
      // TODO: rewrite excluded work

      // revert some specific files
      vector<file_path> work_args;
      for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
        work_args.push_back(app.prefix((*i)()));

      for (size_t i = 0; i < work_args.size(); ++i)
        {
          string arg(idx(work_args, i)());
          if (directory_exists(file_path(arg)))
            {
              // simplest is to just add all files from that
              // directory.
              string dir = fs::path(arg).string();
              for (manifest_map::const_iterator i = m_old.begin();
                   i != m_old.end(); ++i)
                {
                  // this doesn't seem quite right... *some* branch path, not *the* branch path
                  file_path p = i->first;
                  if (fs::path(p()).branch_path().string() == dir)
                    work_args.push_back(p());
                }
            }

          N(directory_exists(file_path(arg)) ||
            (m_old.find(arg) != m_old.end()) ||
            (work.added_files.find(arg) != work.added_files.end()) ||
            (work.deleted_dirs.find(arg) != work.deleted_dirs.end()) ||
            (work.deleted_files.find(arg) != work.deleted_files.end()) ||
            (work.deleted_dirs.find(arg) != work.deleted_dirs.end()) ||
            (work.renamed_files.find(arg) != work.renamed_files.end()),
            F("nothing known about %s") % arg);

          manifest_map::const_iterator entry = m_old.find(file_path(arg));
          if (entry != m_old.end())
            {
              
              L(F("reverting %s to %s\n") %
                manifest_entry_path(entry) % manifest_entry_id(entry));
              
              N(app.db.file_version_exists(manifest_entry_id(entry)),
                F("no file version %s found in database for %s")
                % manifest_entry_id(entry) % manifest_entry_path(entry));
              
              file_data dat;
              L(F("writing file %s to %s\n") %
                manifest_entry_id(entry) % manifest_entry_path(entry));
              app.db.get_file_version(manifest_entry_id(entry), dat);
              write_localized_data(manifest_entry_path(entry), dat.inner(), app.lua);

              // a deleted file will always appear in the manifest
              if (work.deleted_files.find(arg) != work.deleted_files.end())
                {
                  L(F("also removing deletion for %s\n") % arg);
                  work.deleted_files.erase(arg);
                }
            }
          else if (work.deleted_dirs.find(arg) != work.deleted_dirs.end())
            {
              L(F("removing delete for %s\n") % arg);
              work.deleted_dirs.erase(arg);
            }
          else if (work.deleted_files.find(arg) != work.deleted_files.end())
            {
              L(F("removing delete for %s\n") % arg);
              work.deleted_files.erase(arg);
            }
          else if (work.renamed_dirs.find(arg) != work.renamed_dirs.end())
            {
              L(F("removing rename for %s\n") % arg);
              work.renamed_dirs.erase(arg);
            }
          else if (work.renamed_files.find(arg) != work.renamed_files.end())
            {
              L(F("removing rename for %s\n") % arg);
              work.renamed_files.erase(arg);
            }
          else if (work.added_files.find(arg) != work.added_files.end())
            {
              L(F("removing addition for %s\n") % arg);
              work.added_files.erase(arg);
            }
        }
      // race
      put_path_rearrangement(work);
    }

  update_any_attrs(app);
}


CMD(rcs_import, "debug", "RCSFILE...",
    "import all versions in RCS files\n"
    "this command doesn't reconstruct revisions.  you probably want cvs_import")
{
  if (args.size() < 1)
    throw usage(name);
  
  app.initialize(false);

  transaction_guard guard(app.db);
  for (vector<utf8>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      import_rcs_file(mkpath((*i)()), app.db);
    }
  guard.commit();
}


CMD(cvs_import, "rcs", "CVSROOT", "import all versions in CVS repository")
{
  if (args.size() != 1)
    throw usage(name);

  app.initialize(false);

  import_cvs_repo(mkpath(idx(args, 0)()), app);
}

static void
log_certs(app_state & app, revision_id id, cert_name name, string label, bool multiline)
{
  vector< revision<cert> > certs;

  app.db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, app);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      cout << label;

      if (multiline) 
          cout << endl << endl << tv << endl;
      else
          cout << tv << endl;
    }     
}

CMD(log, "informative", "[ID] [file]", "print history in reverse order starting from 'ID' (filtering by 'file')")
{
  revision_set rev;
  revision_id rid;
  set< pair<file_path, revision_id> > frontier;
  file_path file;

  if (args.size() > 2)
    throw usage(name);

  if (args.size() == 2)
  {  
    app.initialize(false);
    complete(app, idx(args, 0)(), rid);
    file=file_path(idx(args, 1)());
  }  
  else if (args.size() == 1)
    { 
      std::string arg=idx(args, 0)();
      if (arg.find_first_not_of(constants::legal_id_bytes) == string::npos
          && arg.size()<=constants::idlen)
        {
          app.initialize(false);
          complete(app, arg, rid);
        }
      else
        {  
          app.initialize(true); // no id arg, must have working copy
          file=file_path(arg);
          file = file_path(arg);
          get_revision_id(rid);
        }
    }
  else
    {
      app.initialize(true); // no id arg, must have working copy
      get_revision_id(rid);
    }

  frontier.insert(make_pair(file, rid));
  
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);
  cert_name branch_name(branch_cert_name);
  cert_name tag_name(tag_cert_name);
  cert_name changelog_name(changelog_cert_name);
  cert_name comment_name(comment_cert_name);

  set<revision_id> seen;

  while(! frontier.empty())
    {
      set< pair<file_path, revision_id> > next_frontier;
      for (set< pair<file_path, revision_id> >::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        { 
          file = i->first;
          rid = i->second;

          bool print_this = file().empty();
          set<  revision<id> > parents;
          vector< revision<cert> > tmp;

          if (!app.db.revision_exists(rid))
            {
              L(F("revision %s does not exist in db, skipping\n") % rid);
              continue;
            }

          if (seen.find(rid) != seen.end())
            continue;

          seen.insert(rid);

          app.db.get_revision(rid, rev);

          changes_summary csum;
          
          set<revision_id> ancestors;

          for (edge_map::const_iterator e = rev.edges.begin();
               e != rev.edges.end(); ++e)
            {
              ancestors.insert(edge_old_revision(e));

              change_set const & cs = edge_changes(e);
              if (! file().empty())
                {
                  if (cs.rearrangement.has_deleted_file(file) ||
                      cs.rearrangement.has_renamed_file_src(file))
                    {
                      print_this = false;
                      next_frontier.clear();
                      break;
                    }
                  else
                    {
                      file_path old_file = apply_change_set_inverse(cs, file);
                      L(F("revision '%s' in '%s' maps to '%s' in %s\n")
                        % rid % file % old_file % edge_old_revision(e));
                      if (!(old_file == file) ||
                          cs.deltas.find(file) != cs.deltas.end())
                        {
                          file = old_file;
                          print_this = true;
                        }
                    }
                }
              next_frontier.insert(std::make_pair(file, edge_old_revision(e)));

              csum.add_change_set(cs);
            }
          
          if (print_this)
          {
            cout << "-----------------------------------------------------------------"
                 << endl;
            cout << "Revision: " << rid << endl;

            for (set<revision_id>::const_iterator anc = ancestors.begin(); 
                 anc != ancestors.end(); ++anc)
              cout << "Ancestor: " << *anc << endl;

            log_certs(app, rid, author_name, "Author: ", false);
            log_certs(app, rid, date_name,   "Date: ",   false);
            log_certs(app, rid, branch_name, "Branch: ", false);
            log_certs(app, rid, tag_name,    "Tag: ",    false);

            if (! csum.empty)
              {
                cout << endl;
                csum.print(cout, 70);
                cout << endl;
              }

            log_certs(app, rid, changelog_name, "ChangeLog: ", true);
            log_certs(app, rid, comment_name,   "Comments: ",  true);
          }
        }
      frontier = next_frontier;
    }
}


CMD(setup, "tree", "DIRECTORY", "setup a new working copy directory")
{
  string dir;

  if (args.size() != 1)
    throw usage(name);

  dir = idx(args,0)();
  app.initialize(dir);
}



}; // namespace commands
