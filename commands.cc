// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <cstdio>
#include <set>
#include <vector>
#include <algorithm>
#include <iterator>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>

#include "commands.hh"
#include "constants.hh"

#include "app_state.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "keys.hh"
#include "manifest.hh"
#include "netsync.hh"
#include "nonce.hh"
#include "packet.hh"
#include "patch_set.hh"
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


  void explain_usage(string const & cmd, ostream & out)
  {
    map<string,command *>::const_iterator i;
    i = cmds.find(cmd);
    if (i != cmds.end())
      {
	string params = i->second->params;
	int old = 0;
	int j = params.find('\n');
	while (j != -1)
	  {
	    out << "     " << i->second->name
		<< " " << params.substr(old, j - old)
		<< endl;
	    old = j + 1;
	    j = params.find('\n', old);
	  }
	out << "     " << i->second->name
	    << " " << params.substr(old, j - old)
	    << endl
	    << "       " << i->second->desc << endl << endl;
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
    if (cmds.find(cmd) != cmds.end())
      {
	L(F("executing %s command\n") % cmd);
	cmds[cmd]->exec(app, args);
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

#define ALIAS(C, realcommand, group, params, desc)	\
CMD(C, group, params, desc)				\
{							\
  process(app, string(#realcommand), args);		\
}

static void 
ensure_bookdir()
{
  mkdir_p(local_path(book_keeping_dir));
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
  ensure_bookdir();
  local_path c_path;
  get_revision_path(c_path);
  if(file_exists(c_path))
    {
      data c_data;
      L(F("loading revision id from %s\n") % c_path);
      read_data(c_path, c_data);
      c = revision_id(c_data());
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
  ensure_bookdir();
  data c_data(rev.inner()());
  write_data(c_path, c_data);
}

static void 
get_path_rearrangement(change_set::path_rearrangement & w)
{
  ensure_bookdir();
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    {
      L(F("checking for un-committed work file %s\n") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_path_rearrangement(w_data, w);
      L(F("read %d nodes from %s\n") % w.first.size() % w_path);
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
  
  if (w.first.size() > 0)
    {
      ensure_bookdir();
      data w_data;
      write_path_rearrangement(w, w_data);
      write_data(w_path, w_data);
    }
  else
    {
      delete_file(w_path);
    }
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
calculate_current_revision(app_state & app, 
			   revision_set & rev,
			   manifest_map & m_old,
			   manifest_map & m_new)
{
  manifest_id old_manifest_id;
  revision_id old_revision_id;    
  change_set cs;
  path_set paths;
  manifest_map m_old_rearranged;

  rev.edges.clear();
  m_old.clear();
  m_new.clear();

  get_revision_id(old_revision_id);
  if (! old_revision_id.inner()().empty())
    {

      N(app.db.revision_exists(old_revision_id),
	F("base revision %s does not exist in database\n") % old_revision_id);
      
      app.db.get_revision_manifest(old_revision_id, old_manifest_id);
      L(F("old manifest is %s\n") % old_manifest_id);
      
      N(app.db.manifest_version_exists(old_manifest_id),
	F("base manifest %s does not exist in database\n") % old_manifest_id);
      
      {
	manifest_data mdat;
	app.db.get_manifest_version(old_manifest_id, mdat);
	read_manifest_map(mdat, m_old);
      }
    }

  L(F("old manifest has %d entries\n") % m_old.size());

  get_path_rearrangement(cs.rearrangement);
  
  apply_path_rearrangement(m_old, cs.rearrangement, m_old_rearranged);
  extract_path_set(m_old_rearranged, paths);
  build_manifest_map(paths, m_new, app);

  I(m_new.size() == m_old_rearranged.size());
  manifest_map::const_iterator i = m_old_rearranged.begin();
  for (manifest_map::const_iterator j = m_new.begin(); j != m_new.end(); ++j, ++i)
    {
      I(manifest_entry_path(i) == manifest_entry_path(j));
      if (! (manifest_entry_id(i) == manifest_entry_id(j)))
	{
	  L(F("noted delta %s -> %s on %s\n") 
	    % manifest_entry_id(i) 
	    % manifest_entry_id(j) 
	    % manifest_entry_path(i));
	  cs.deltas.insert(make_pair(manifest_entry_path(i),
				     make_pair(manifest_entry_id(i),
					       manifest_entry_id(j))));
	}
    }
  
  calculate_ident(m_new, rev.new_manifest);
  L(F("new manifest is %s\n") % rev.new_manifest);

  rev.edges.insert(make_pair(old_revision_id,
			     make_pair(old_manifest_id, cs)));
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
	err += (*i + "\n");
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
	
  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;      
      decode_base64(idx(certs, i).value, tv);
      string washed;
      if (guess_binary(tv()) 
	  || idx(certs, i).name == rename_cert_name)
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
    P(F("warning: no keys found matching '%s'\n") % idx(args, 0)());

  guard.commit();
}

CMD(genkey, "key and cert", "KEYID", "generate an RSA key-pair")
{
  if (args.size() != 1)
    throw usage(name);
  
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

CMD(cert, "key and cert", "REVISION CERTNAME [CERTVAL]",
    "create a cert for a revision")
{
  if ((args.size() != 3) && (args.size() != 2))
    throw usage(name);

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

CMD(vcheck, "key and cert", "create [REVISION]\ncheck [REVISION]", 
    "create or check a cryptographic version-check certificate")
{
  if (args.size() < 1 || args.size() > 2)
    throw usage(name);

  set<manifest_id> ids;
  if (args.size() == 1)
    {
      set<revision_id> rids;
      N(app.branch_name() != "", F("need --branch argument for branch-based vcheck"));
      get_branch_heads(app.branch_name(), app, rids);      
      for (set<revision_id>::const_iterator i = rids.begin(); i != rids.end(); ++i)
	{
	  manifest_id mid;
	  app.db.get_revision_manifest(*i, mid);
	  ids.insert(mid);
	}
    }
  else
    {
      for (size_t i = 1; i < args.size(); ++i)
	{
	  manifest_id mid;
	  revision_id rid;
	  complete(app, idx(args, i)(), rid);
	  app.db.get_revision_manifest(rid, mid);
	  ids.insert(mid);
	}
    }

  if (idx(args, 0)() == "create")
    for (set<manifest_id>::const_iterator i = ids.begin();
	 i != ids.end(); ++i)
    {
      packet_db_writer dbw(app);
      cert_manifest_vcheck(*i, app, dbw); 
    }

  else if (idx(args, 0)() == "check")
    for (set<manifest_id>::const_iterator i = ids.begin();
	 i != ids.end(); ++i)
    {
      check_manifest_vcheck(*i, app); 
    }

  else 
    throw usage(name);
}


CMD(tag, "certificate", "REVISION TAGNAME", 
    "put a symbolic tag cert on a revision version")
{
  if (args.size() != 2)
    throw usage(name);
  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_tag(r, idx(args, 1)(), app, dbw);
}


CMD(comment, "certificate", "REVISION [COMMENT]",
    "comment on a particular revision")
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

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



CMD(add, "working copy", "PATHNAME...", "add files to working copy")
{
  if (args.size() < 1)
    throw usage(name);

  manifest_map man;
  change_set::path_rearrangement work;  
  get_path_rearrangement(work);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_addition(file_path((*i)()), app, work);
    
  put_path_rearrangement(work);

  update_any_attrs(app);
  app.write_options();
}

CMD(drop, "working copy", "FILE...", "drop files from working copy")
{
  if (args.size() < 1)
    throw usage(name);

  change_set::path_rearrangement work;
  get_path_rearrangement(work);

  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_deletion(file_path((*i)()), work);
  
  put_path_rearrangement(work);

  update_any_attrs(app);
  app.write_options();
}


CMD(rename, "working copy", "SRC DST", "rename entries in the working copy")
{
  if (args.size() != 2)
    throw usage(name);
  
  change_set::path_rearrangement work;

  get_path_rearrangement(work);
  build_rename(file_path(idx(args, 0)()), file_path(idx(args, 1)()), work);
  
  put_path_rearrangement(work);
  
  update_any_attrs(app);
  app.write_options();  
}


// fload and fmerge are simple commands for debugging the line merger.
// most of the time, leave them commented out. they can be helpful for certain
// cases, though.

CMD(fload, "tree", "", "load file contents into db")
{
  string s = get_stdin();
  base64< gzip< data > > gzd;

  pack(data(s), gzd);

  file_id f_id;
  file_data f_data(gzd);
  
  calculate_ident (f_data, f_id);
  
  packet_db_writer dbw(app);
  dbw.consume_file_data(f_id, f_data);  
}

CMD(fmerge, "tree", "<parent> <left> <right>", "merge 3 files and output result")
{
  if (args.size() != 3)
    throw usage(name);
  
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

CMD(status, "informative", "", "show status of working copy")
{
  revision_set rs;
  manifest_map m_old, m_new;
  data tmp;

  calculate_current_revision(app, rs, m_old, m_new);
  write_revision_set(rs, tmp);
  cout << tmp;
}

CMD(cat, "tree", "(file|manifest|revision) ID", 
    "write file, manifest, or revision from database to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);

  if (idx(args, 0)() == "file")
    {
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
      complete(app, idx(args, 1)(), ident);

      N(app.db.manifest_version_exists(ident),
	F("no manifest version %s found in database") % ident);

      L(F("dumping manifest %s\n") % ident);
      app.db.get_manifest_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }
  else if (idx(args, 0)() == "revision")
    {
      revision_data dat;
      revision_id ident;
      complete(app, idx(args, 1)(), ident);

      N(app.db.revision_exists(ident),
	F("no revision %s found in database") % ident);

      L(F("dumping revision %s\n") % ident);
      app.db.get_revision(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }
  else 
    throw usage(name);

  guard.commit();
}


CMD(checkout, "tree", "REVISION DIRECTORY\nDIRECTORY", 
    "check out revision from database into directory")
{

  revision_id ident;
  string dir;

  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  if (args.size() == 1)
    {
      set<revision_id> heads;
      N(app.branch_name() != "", F("need --branch argument for branch-based checkout"));
      get_branch_heads(app.branch_name(), app, heads);
      N(heads.size() > 0, F("branch %s is empty") % app.branch_name);
      N(heads.size() == 1, F("branch %s has multiple heads") % app.branch_name);
      ident = *(heads.begin());
      dir = idx(args, 0)();
    }

  else
    {
      complete(app, idx(args, 0)(), ident);
      dir = idx(args, 1)();
    }

  if (dir != string("."))
    {
      fs::path co_dir = mkpath(dir);
      fs::create_directories(co_dir);
      chdir(co_dir.native_directory_string().c_str());
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
  manifest_data m_data;
  app.db.get_manifest_version(mid, m_data);
  read_manifest_map(m_data, m);      
  
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
  app.write_options();
}

ALIAS(co, checkout, "tree", "REVISION DIRECTORY\nDIRECTORY",
      "check out revision from database; alias for checkout")

CMD(heads, "tree", "", "show unmerged head revisions of branch")
{
  set<revision_id> heads;
  if (args.size() != 0)
    throw usage(name);
  
  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  if (heads.size() == 0)
    P(F("branch '%s' is empty\n") % app.branch_name);
  else if (heads.size() == 1)
    P(F("branch '%s' is currently merged:\n") % app.branch_name);
  else
    P(F("branch '%s' is currently unmerged:\n") % app.branch_name);
  
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  for (set<revision_id>::const_iterator i = heads.begin(); 
       i != heads.end(); ++i)
    {
      cout << i->inner()(); 

      // print authors and date of this head
      vector< revision<cert> > tmp;
      app.db.get_revision_certs(*i, author_name, tmp);
      erase_bogus_certs(tmp, app);
      for (vector< revision<cert> >::const_iterator j = tmp.begin();
	   j != tmp.end(); ++j)
      {
	 cert_value tv;
	 decode_base64(j->inner().value, tv);
	 cout << " " << tv;
      }
      app.db.get_revision_certs(*i, date_name, tmp);
      erase_bogus_certs(tmp, app);
      for (vector< revision<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
      {
	 cert_value tv;
	 decode_base64(j->inner().value, tv);
	 cout << " " << tv;
      }
      cout << endl;
    }
}

static void 
ls_branches(string name, app_state & app, vector<utf8> const & args)
{
  transaction_guard guard(app.db);
  vector< revision<cert> > certs;
  app.db.get_revision_certs(branch_cert_name, certs);

  vector<string> names;
  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_value name;
      decode_base64(idx(certs, i).inner().value, name);
      names.push_back(name());
    }

  sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (size_t i = 0; i < names.size(); ++i)
    cout << idx(names, i) << endl;

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
    if (man.find(path) == man.end())
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
ls_unknown (app_state & app, bool want_ignored)
{
  revision_set rev;
  manifest_map m_old, m_new;
  calculate_current_revision(app, rev, m_old, m_new);
  unknown_itemizer u(app, m_new, want_ignored);
  walk_tree(u);
}

static void
ls_missing (app_state & app)
{
  revision_set rev;
  manifest_map m_old, m_new;
  path_set paths;
  calculate_current_revision(app, rev, m_old, m_new);
  extract_path_set(m_new, paths);

  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (!file_exists(*i))	
	cout << *i << endl;
    }
}


CMD(list, "informative", 
    "certs ID\n"
    "keys [PATTERN]\n"
    "branches\n"
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
  else if (idx(args, 0)() == "unknown")
    ls_unknown(app, false);
  else if (idx(args, 0)() == "ignored")
    ls_unknown(app, true);
  else if (idx(args, 0)() == "missing")
    ls_missing(app);
  else
    throw usage(name);
}

ALIAS(ls, list, "informative",  
      "certs ID\n"
      "keys [PATTERN]\n"
      "branches\n"
      "unknown\n"
      "ignored\n"
      "missing", "show certs, keys, or branches")


CMD(mdelta, "packet i/o", "OLDID NEWID", "write manifest delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  packet_writer pw(cout);

  manifest_id m_old_id, m_new_id; 
  manifest_data m_old_data, m_new_data;
  manifest_map m_old, m_new;
  patch_set ps;      

  complete(app, idx(args, 0)(), m_old_id);
  complete(app, idx(args, 1)(), m_new_id);

  app.db.get_manifest_version(m_old_id, m_old_data);
  app.db.get_manifest_version(m_new_id, m_new_data);
  read_manifest_map(m_old_data, m_old);
  read_manifest_map(m_new_data, m_new);

  base64< gzip<delta> > del;
  diff(m_old, m_new, del);
  pw.consume_manifest_delta(m_old_id, m_new_id, 
			    manifest_delta(del));
}

CMD(fdelta, "packet i/o", "OLDID NEWID", "write file delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

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

  packet_writer pw(cout);

  file_id f_id;
  file_data f_data;

  complete(app, idx(args, 0)(), f_id);

  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);  
}


CMD(rcerts, "packet i/o", "ID", "write revision cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  revision_id r_id;
  vector< revision<cert> > certs;

  complete(app, idx(args, 0)(), r_id);

  app.db.get_revision_certs(r_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_revision_cert(idx(certs, i));
}

CMD(mcerts, "packet i/o", "ID", "write manifest cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  manifest_id m_id;
  vector< manifest<cert> > certs;

  complete(app, idx(args, 0)(), m_id);

  app.db.get_manifest_certs(m_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_manifest_cert(idx(certs, i));
}

CMD(fcerts, "packet i/o", "ID", "write file cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  file_id f_id;
  vector< file<cert> > certs;

  complete(app, idx(args, 0)(), f_id);

  app.db.get_file_certs(f_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_file_cert(idx(certs, i));
}

CMD(pubkey, "packet i/o", "ID", "write public key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);
  rsa_keypair_id ident(idx(args, 0)());
  base64< rsa_pub_key > key;
  app.db.get_key(ident, key);
  pw.consume_public_key(ident, key);
}

CMD(privkey, "packet i/o", "ID", "write private key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);
  rsa_keypair_id ident(idx(args, 0)());
  base64< arc4<rsa_priv_key> > key;
  app.db.get_key(ident, key);
  pw.consume_private_key(ident, key);
}


CMD(read, "packet i/o", "", "read packets from stdin")
{
  packet_db_writer dbw(app, true);
  size_t count = read_packets(cin, dbw);
  N(count != 0, F("no packets found on stdin"));
  if (count == 1)
    P(F("read 1 packet\n"));
  else
    P(F("read %d packets\n") % count);
}


CMD(debug, "debug", "SQL", "issue SQL queries directly (dangerous)")
{
  if (args.size() != 1)
    throw usage(name);
  app.db.debug(idx(args, 0)(), cout);
}


CMD(reindex, "network", "COLLECTION...", 
    "rebuild the hash-tree indices used to sync COLLECTION over the network")
{
  if (args.size() < 1)
    throw usage(name);

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

  rsa_keypair_id key;
  N(guess_default_key(key, app), F("could not guess default signing key"));
  app.signing_key = key;

  utf8 addr(idx(args,0));
  vector<utf8> collections(args.begin() + 1, args.end());
  run_netsync_protocol(server_voice, source_and_sink_role, addr, collections, app);  
}

CMD(db, "database", "init\ninfo\nversion\ndump\nload\nmigrate", "manipulate database state")
{
  if (args.size() != 1)
    throw usage(name);
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
  else
    throw usage(name);
}

CMD(commit, "working copy", "MESSAGE", "commit working copy to database")
{
  string log_message("");
  revision_set rs;
  revision_id rid;
  manifest_map m_old, m_new;

  if (args.size() != 0 && args.size() != 1)
    throw usage(name);

  calculate_current_revision(app, rs, m_old, m_new);
  calculate_ident(rs, rid);

  N(rs.edges.size() != 0, F("no changes to commit\n"));
    
  cert_value branchname;
  I(rs.edges.size() == 1);
  guess_branch (edge_old_revision(rs.edges.begin()), app, branchname);
  app.set_branch(branchname());
    
  P(F("beginning commit\n"));
  P(F("manifest %s\n") % rs.new_manifest);
  P(F("revision %s\n") % rid);
  P(F("branch %s\n") % branchname);

  // get log message
  if (args.size() == 1)
    log_message = idx(args, 0)();
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
  remove_path_rearrangement();
  put_revision_id(rid);
  P(F("committed revision %s\n") % rid);

  update_any_attrs(app);
  app.write_options();

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


struct
diff_dumper : public change_set_consumer
{
  app_state & app;
  bool const & new_is_archived;
  diff_dumper(app_state & app,
	      bool const & new_is_archived)
    : app(app), new_is_archived(new_is_archived)
  {}
  
  virtual void add_file(file_path const & pth, 
			file_id const & ident) 
  {
      data unpacked;
      vector<string> lines;
      
      if (new_is_archived)
        {
	  file_data dat;
	  app.db.get_file_version(ident, dat);
	  unpack(dat.inner(), unpacked);
        }
      else
        {
          read_localized_data(pth, unpacked, app.lua);
        }

      split_into_lines(unpacked(), lines);
      if (! lines.empty())
	{
	  cout << (F("--- %s\n") % pth)
	       << (F("+++ %s\n") % pth)
	       << (F("@@ -0,0 +1,%d @@\n") % lines.size());
	  for (vector<string>::const_iterator j = lines.begin();
	       j != lines.end(); ++j)
	    {
	      cout << "+" << *j << endl;
	    }
	}
  }

  virtual void apply_delta(file_path const & path, 
			   file_id const & src, 
			   file_id const & dst)
  {
    file_data f_old;
    gzip<data> decoded_old;
    data decompressed_old, decompressed_new;
    vector<string> old_lines, new_lines;
    
    app.db.get_file_version(src, f_old);
    decode_base64(f_old.inner(), decoded_old);
    decode_gzip(decoded_old, decompressed_old);
    
    if (new_is_archived)
      {
	file_data f_new;
	gzip<data> decoded_new;
	app.db.get_file_version(dst, f_new);
	decode_base64(f_new.inner(), decoded_new);
	decode_gzip(decoded_new, decompressed_new);
      }
    else
      {
	read_localized_data(path, decompressed_new, app.lua);
      }
    
    split_into_lines(decompressed_old(), old_lines);
    split_into_lines(decompressed_new(), new_lines);
    unidiff(path(), path(), old_lines, new_lines, cout);
  }

  virtual void delete_file(file_path const & d) {}
  virtual void delete_dir(file_path const & d) {}
  virtual void rename_file(file_path const & a, file_path const & b) {}
  virtual void rename_dir(file_path const & a, file_path const & b) {}
  virtual ~diff_dumper() {}
};

CMD(diff, "informative", "[REVISION [REVISION]]", "show current diffs on stdout")
{
  revision_set r_old, r_new;
  manifest_map m_new;
  bool new_is_archived;

  change_set composite;

  if (args.size() == 0)
    {
      manifest_map m_old;
      calculate_current_revision(app, r_new, m_old, m_new);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      if (r_new.edges.size() == 1)
	composite = edge_changes(r_new.edges.begin());
      new_is_archived = false;
    }
  else if (args.size() == 1)
    {
      revision_id r_old_id;
      manifest_map m_old;
      complete(app, idx(args, 0)(), r_old_id);
      N(app.db.revision_exists(r_old_id),
	F("revision %s does not exist") % r_old_id);
      app.db.get_revision(r_old_id, r_old);
      calculate_current_revision(app, r_new, m_old, m_new);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      N(r_new.edges.size() == 1, F("current revision has no ancestor"));
      new_is_archived = false;
    }
  else if (args.size() == 2)
    {
      revision_id r_old_id, r_new_id;
      manifest_id m_new_id;
      manifest_data m_new_dat;

      complete(app, idx(args, 0)(), r_old_id);
      complete(app, idx(args, 1)(), r_new_id);

      N(app.db.revision_exists(r_old_id),
	F("revision %s does not exist") % r_old_id);
      app.db.get_revision(r_old_id, r_old);

      N(app.db.revision_exists(r_new_id),
	F("revision %s does not exist") % r_new_id);
      app.db.get_revision(r_new_id, r_new);

      app.db.get_revision_manifest(r_new_id, m_new_id);
      app.db.get_manifest_version(m_new_id, m_new_dat);
      read_manifest_map(m_new_dat, m_new);

      new_is_archived = true;
    }
  else
    {
      throw usage(name);
    }
      


  if (args.size() > 0)
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

      N(find_common_ancestor(src_id, dst_id, anc_id, app),
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
	  manifest_data anc_m_data;
	  manifest_map m_anc;

	  I(!(src_id == anc_id || dst_id == anc_id));

	  app.db.get_revision_manifest(anc_id, anc_m_id);
	  app.db.get_manifest_version(anc_m_id, anc_m_data);
	  read_manifest_map(anc_m_data, m_anc);

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
  for (vector<string>::iterator i = lines.begin(); i != lines.end(); ++i)
    cout << "# " << *i << endl;

  diff_dumper dd(app, new_is_archived);
  play_back_change_set(composite, dd);
}


/*

// this helper tries to produce merge <- mergeN(left,right), possibly
// merge3 if it can find an ancestor, otherwise merge2. 

static void 
try_one_merge(manifest_id const & left,
	      manifest_id const & right,
	      manifest_id & merged,
	      app_state & app)
{
  manifest_data left_data, right_data, ancestor_data, merged_data;
  manifest_map left_map, right_map, ancestor_map, merged_map;
  manifest_id ancestor;
  rename_edge left_renames, right_renames;

  app.db.get_manifest_version(left, left_data);
  app.db.get_manifest_version(right, right_data);
  read_manifest_map(left_data, left_map);
  read_manifest_map(right_data, right_map);
  
  simple_merge_provider merger(app);
  
  if(find_common_ancestor(left, right, ancestor, app))	    
    {
      P(F("common ancestor %s found, trying 3-way merge\n") % ancestor); 
      app.db.get_manifest_version(ancestor, ancestor_data);
      read_manifest_map(ancestor_data, ancestor_map);
      N(merge3(ancestor_map, left_map, right_map, 
	       app, merger, merged_map, left_renames.mapping, right_renames.mapping),
	F("failed to 3-way merge manifests %s and %s via ancestor %s") 
	% left % right % ancestor);
    }
  else
    {
      P(F("no common ancestor found, trying 2-way merge\n")); 
      N(merge2(left_map, right_map, app, merger, merged_map),
	F("failed to 2-way merge manifests %s and %s") % left % right);
    }
  
  write_manifest_map(merged_map, merged_data);
  calculate_ident(merged_map, merged);	  
  
  base64< gzip<delta> > left_edge;
  diff(left_data.inner(), merged_data.inner(), left_edge);

  // FIXME: we do *not* manufacture or store the second edge to
  // the merged version, since doing so violates the
  // assumptions of the db, and the 'right' version already
  // exists in its entirety, anyways. this is a subtle issue
  // though and I'm not sure I'm making the right
  // decision. revisit. if you do not see that it is a subtle
  // issue I suggest you are not thinking about it long enough.
  //
  // base64< gzip<delta> > right_edge;
  // diff(right_data.inner(), merged_data.inner(), right_edge);
  // app.db.put_manifest_version(right, merged, right_edge);
  
  
  // we do of course record the left edge, and ancestry relationship to
  // both predecessors.

  {
    packet_db_writer dbw(app);    

    dbw.consume_manifest_delta(left, merged, left_edge);  
    cert_manifest_ancestor(left, merged, app, dbw);
    cert_manifest_ancestor(right, merged, app, dbw);
    cert_manifest_date_now(merged, app, dbw);
    cert_manifest_author_default(merged, app, dbw);

    left_renames.parent = left;
    left_renames.child = merged;
    right_renames.parent = right;
    right_renames.child = merged;
    cert_manifest_rename(merged, left_renames, app, dbw);
    cert_manifest_rename(merged, right_renames, app, dbw);
  }
}			  


// actual commands follow



// conversion to revisions:
//
// DONE
// ------
// genkey
// cert
// vcheck
// tag
// approve
// disapprove
// comment
// add
// drop
// rename
// commit
// cat
// checkout
// heads
// fload
// complete
// status
// reindex
// push
// pull
// sync
// serve
// list
//
// mdelta
// fdelta
// mdata
// fdata
// mcerts
// fcerts
// pubkey
// privkey
//
// read
// rcs_import
// cvs_import
// debug
// db
//
// NOT-DONE
// --------
// update
// revert
// merge
// propagate
// diff
// log
//
// agraph





CMD(approve, "certificate", "REVISION", 
    "approve of a particular revision")
{
  if (args.size() != 1)
    throw usage(name);  

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_approval(r, true, app, dbw);
}

CMD(disapprove, "certificate", "REVISION", 
    "disapprove of a particular revision")
{
  if (args.size() != 1)
    throw usage(name);

  revision_id c;
  complete(app, idx(args, 0)(), c);
  packet_db_writer dbw(app);
  cert_revision_approval(c, false, app, dbw);
}



CMD(update, "working copy", "", "update working copy")
{
  manifest_data m_chosen_data;
  manifest_map m_old, m_working, m_chosen, m_new;
  manifest_id m_old_id, m_chosen_id;

  transaction_guard guard(app.db);

  get_manifest_map(m_old);
  calculate_ident(m_old, m_old_id);
  calculate_new_manifest_map(m_old, m_working, app);
  
  pick_update_target(m_old_id, app, m_chosen_id);
  if (m_old_id == m_chosen_id)
    {
      P(F("already up to date at %s\n") % m_old_id);
      return;
    }
  P(F("selected update target %s\n") % m_chosen_id);
  app.db.get_manifest_version(m_chosen_id, m_chosen_data);
  read_manifest_map(m_chosen_data, m_chosen);

  rename_edge left_renames, right_renames;
  update_merge_provider merger(app);
  N(merge3(m_old, m_chosen, m_working, app, merger, m_new, 
	   left_renames.mapping, right_renames.mapping),
    F("manifest merge failed, no update performed"));

  P(F("calculating patchset for update\n"));
  patch_set ps;
  manifests_to_patch_set(m_working, m_new, app, ps);

  L(F("applying %d deletions to files in tree\n") % ps.f_dels.size());
  for (set<file_path>::const_iterator i = ps.f_dels.begin();
       i != ps.f_dels.end(); ++i)
    {
      L(F("deleting %s\n") % (*i));
      delete_file(*i);
    }

  L(F("applying %d moves to files in tree\n") % ps.f_moves.size());
  for (set<patch_move>::const_iterator i = ps.f_moves.begin();
       i != ps.f_moves.end(); ++i)
    {
      L(F("moving %s -> %s\n") % i->path_old % i->path_new);
      make_dir_for(i->path_new);
      move_file(i->path_old, i->path_new);
    }
  
  L(F("applying %d additions to tree\n") % ps.f_adds.size());
  for (set<patch_addition>::const_iterator i = ps.f_adds.begin();
       i != ps.f_adds.end(); ++i)
    {
      L(F("adding %s as %s\n") % i->ident % i->path);
      file_data tmp;
      if (app.db.file_version_exists(i->ident))
	app.db.get_file_version(i->ident, tmp);
      else if (merger.temporary_store.find(i->ident) != merger.temporary_store.end())
	tmp = merger.temporary_store[i->ident];
      else
	I(false); // trip assert. this should be impossible.
      write_localized_data(i->path, tmp.inner(), app.lua);
    }

  L(F("applying %d deltas to tree\n") % ps.f_deltas.size());
  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    {
      P(F("updating file %s: %s -> %s\n") 
	% i->path % i->id_old % i->id_new);
      
      // sanity check
      {
	base64< gzip<data> > dtmp;
	hexenc<id> dtmp_id;
	read_localized_data(i->path, dtmp, app.lua);
	calculate_ident(dtmp, dtmp_id);
	I(dtmp_id == i->id_old.inner());
      }

      // ok, replace with new version
      {
	file_data tmp;
	if (app.db.file_version_exists(i->id_new))
	  app.db.get_file_version(i->id_new, tmp);
	else if (merger.temporary_store.find(i->id_new) != merger.temporary_store.end())
	  tmp = merger.temporary_store[i->id_new];
	else
	  I(false); // trip assert. this should be impossible.
	write_localized_data(i->path, tmp.inner(), app.lua);
      }
    }
  
  L(F("update successful\n"));
  guard.commit();

  // small race condition here...
  // nb: we write out m_chosen, not m_new, because the manifest-on-disk
  // is the basis of the working copy, not the working copy itself.
  put_manifest_map(m_chosen);
  P(F("updated to base version %s\n") % m_chosen_id);

  update_any_attrs(app);
  app.write_options();
}

CMD(revert, "working copy", "[FILE]...", 
    "revert file(s) or entire working copy")
{
  manifest_map m_old;

  if (args.size() == 0)
    {
      // revert the whole thing
      get_manifest_map(m_old);
      for (manifest_map::const_iterator i = m_old.begin(); i != m_old.end(); ++i)
	{
	  path_id_pair pip(*i);

	  N(app.db.file_version_exists(manifest_entry_id(i)),
	    F("no file version %s found in database for %s")
	    % manifest_entry_id(i) % manifest_entry_path(i));
      
	  file_data dat;
	  L(F("writing file %s to %s\n") %
	    % manifest_entry_id(i) % manifest_entry_path(i));
	  app.db.get_file_version(manifest_entry_id(i), dat);
	  write_localized_data(manifest_entry_path(i), dat.inner(), app.lua);
	}
      remove_path_rearrangement();
    }
  else
    {
      change_set::path_rearrangement work;
      get_manifest_map(m_old);
      get_path_rearrangement(work);

      // revert some specific files
      vector<utf8> work_args (args.begin(), args.end());
      for (size_t i = 0; i < work_args.size(); ++i)
	{
	  string arg(idx(work_args, i)());
	  if (directory_exists(arg))
	    {
	      // simplest is to just add all files from that
	      // directory.
	      string dir = arg;
	      int off = dir.find_last_not_of('/');
	      if (off != -1)
		dir = dir.substr(0, off + 1);
	      dir += '/';
	      for (manifest_map::const_iterator i = m_old.begin();
		   i != m_old.end(); ++i)
		{
		  file_path p = i->first;
		  if (! p().compare(0, dir.length(), dir))
		    work_args.push_back(p());
		}
	      continue;
	    }

	  N((m_old.find(arg) != m_old.end()) ||
	    (work.adds.find(arg) != work.adds.end()) ||
	    (work.dels.find(arg) != work.dels.end()) ||
	    (work.renames.find(arg) != work.renames.end()),
	    F("nothing known about %s") % arg);

	  if (m_old.find(arg) != m_old.end())
	    {
	      manifest_entry entry = m_old.find(arg);
	      L(F("reverting %s to %s\n") %
		manifest_entry_path(entry) % manifest_entry_id(entry));
	      
	      N(app.db.file_version_exists(pip.ident()),
		F("no file version %s found in database for %s")
		manifest_entry_id(entry) % manifest_entry_path(entry));
	      
	      file_data dat;
	      L(F("writing file %s to %s\n") %
		manifest_entry_id(entry) % manifest_entry_path(entry));
	      app.db.get_file_version(manifest_entry_id(entry), dat);
	      write_localized_data(manifest_entry_path(entry), dat.inner(), app.lua);

	      // a deleted file will always appear in the manifest
	      if (work.dels.find(arg) != work.dels.end())
		{
		  L(F("also removing deletion for %s\n") % arg);
		  work.dels.erase(arg);
		}
	    }
	  else if (work.renames.find(arg) != work.renames.end())
	    {
	      L(F("removing rename for %s\n") % arg);
	      work.renames.erase(arg);
	    }
	  else
	    {
	      I(work.adds.find(arg) != work.adds.end());
	      L(F("removing addition for %s\n") % arg);
	      work.adds.erase(arg);
	    }
	}
      // race
      put_path_rearrangement(work);
    }

  update_any_attrs(app);
  app.write_options();
}


CMD(merge, "tree", "", "merge unmerged heads of branch")
{
  set<manifest_id> heads;

  if (args.size() != 0)
    throw usage(name);

  N(app.branch_name() != "",
    P(F("please specify a branch, with --branch=BRANCH")));

  get_branch_heads(app.branch_name(), app, heads);

  N(heads.size() != 0, F("branch '%s' is empty\n") % app.branch_name);
  N(heads.size() != 1, F("branch '%s' is merged\n") % app.branch_name);

  set<manifest_id>::const_iterator i = heads.begin();
  manifest_id left = *i;
  manifest_id ancestor;
  size_t count = 1;
  for (++i; i != heads.end(); ++i, ++count)
    {
      manifest_id right = *i;
      P(F("merging with manifest %d / %d: %s <-> %s\n")
	% count % heads.size() % left % right);

      manifest_id merged;
      transaction_guard guard(app.db);
      try_one_merge (left, right, merged, app);
	  	  
      // merged 1 edge; now we commit this, update merge source and
      // try next one

      packet_db_writer dbw(app);
      cert_manifest_in_branch(merged, app.branch_name(), app, dbw);

      string log = (F("merge of %s\n"
		      "     and %s\n") % left % right).str();
      cert_manifest_changelog(merged, log, app, dbw);
	  
      guard.commit();
      P(F("[source] %s\n") % left);
      P(F("[source] %s\n") % right);
      P(F("[merged] %s\n") % merged);
      left = merged;
    }

  app.write_options();
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
  
  set<manifest_id> src_heads, dst_heads;

  if (args.size() != 2)
    throw usage(name);

  get_branch_heads(idx(args, 0)(), app, src_heads);
  get_branch_heads(idx(args, 1)(), app, dst_heads);

  N(src_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 0)());
  N(src_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 0)());

  N(dst_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 1)());
  N(dst_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 1)());

  set<manifest_id>::const_iterator src_i = src_heads.begin();
  set<manifest_id>::const_iterator dst_i = dst_heads.begin();
  
  manifest_id merged;
  transaction_guard guard(app.db);
  try_one_merge (*src_i, *dst_i, merged, app);      
  
  packet_db_writer dbw(app);
  
  cert_manifest_in_branch(merged, idx(args, 1)(), app, dbw);
  
  string log = (F("propagate of %s and %s from branch '%s' to '%s'\n")
		% (*src_i) % (*dst_i) % idx(args,0) % idx(args,1)).str();
  
  cert_manifest_changelog(merged, log, app, dbw);
  
  guard.commit();      
}


CMD(complete, "informative", "(revision|manifest|file) PARTIAL-ID", "complete partial id")
{
  if (args.size() != 2)
    throw usage(name);

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


CMD(log, "informative", "[ID] [file]", "print log history in reverse order (which affected file)")
{
  manifest_map m;
  manifest_id m_id;
  set<manifest_id> frontier, cycles;
  file_path file;

  if (args.size() > 2)
    throw usage(name);

  if (args.size() == 2)
  {  
    complete(app, idx(args, 0)(), m_id);
    file=file_path(idx(args, 1)());
  }  
  else if (args.size() == 1)
    { 
      std::string arg=idx(args, 0)();
      if (arg.find_first_not_of(constants::legal_id_bytes) == string::npos
          && arg.size() <= constants::idlen)
	complete(app, arg, m_id);
      else
	{  
	  file = file_path(arg);
	  get_manifest_map(m);
	  calculate_ident (m, m_id);
	}
    }
  else
    {
      get_manifest_map(m);
      calculate_ident (m, m_id);
    }

  frontier.insert(m_id);
  
  cert_name ancestor_name(ancestor_cert_name);
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);
  cert_name changelog_name(changelog_cert_name);
  cert_name comment_name(comment_cert_name);
  cert_name tag_name(tag_cert_name);

  while(! frontier.empty())
    {
      set<manifest_id> next_frontier;
      for (set<manifest_id>::const_iterator i = frontier.begin();
	   i != frontier.end(); ++i)
	{ 
	  bool print_this = file().empty(); // (file==file_path());
	  vector< manifest<cert> > tmp;
	  file_id current_file_id;
	  
	  if (!print_this)
	  {  
	    manifest_data dat;
	    app.db.get_manifest_version(*i,dat);
	    manifest_map mp;
	    read_manifest_map(dat,mp);
	    L(F("Looking for %s in %s, found %s\n") % file % *i % mp[file]);
	    current_file_id=mp[file];
	  }

	  app.db.get_manifest_certs(*i, ancestor_name, tmp);
	  erase_bogus_certs(tmp, app);
	  if (tmp.empty())
	  {  
	    if (!print_this && !(current_file_id==file_id()))
	      print_this=true;
	  }

	  else for (vector< manifest<cert> >::const_iterator j = tmp.begin();
		    j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id id(tv());
	      if (!print_this)
	      {  
		manifest_data dat;
		app.db.get_manifest_version(id,dat);
		manifest_map mp;
		read_manifest_map(dat,mp);
		L(F("Looking for %s in %s, found %s\n") % file % *i % mp[file]);
		print_this=!(current_file_id==mp[file]);
	      }
	      if (cycles.find(id) == cycles.end())
		{
		  next_frontier.insert(id);
		  cycles.insert(id);
		}
	    }

	  if (print_this)
	  {
	  cout << "-----------------------------------------------------------------"
	       << endl;
	  cout << "Version: " << *i << endl;

	  cout << "Author:";
	  app.db.get_manifest_certs(*i, author_name, tmp);
	  erase_bogus_certs(tmp, app);
	  for (vector< manifest<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      cout << " " << tv;
	    }	  
	  cout << endl;

	  cout << "Date:";
	  app.db.get_manifest_certs(*i, date_name, tmp);
	  erase_bogus_certs(tmp, app);
	  for (vector< manifest<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      cout << " " << tv;
	    }	  
	  cout << endl;

	  app.db.get_manifest_certs(*i, tag_name, tmp);
	  erase_bogus_certs(tmp, app);
	  if (!tmp.empty())
	    {
	      for (vector< manifest<cert> >::const_iterator j = tmp.begin();
		   j != tmp.end(); ++j)
		{
		  cert_value tv;
		  decode_base64(j->inner().value, tv);
		  cout << "Tag: " << tv << endl;
		}	  
	      cout << endl;
	    }

	  cout << "ChangeLog:" << endl << endl;
	  app.db.get_manifest_certs(*i, changelog_name, tmp);
	  erase_bogus_certs(tmp, app);
	  for (vector< manifest<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      cout << " " << tv << endl;
	    }	  
	  cout << endl;

	  app.db.get_manifest_certs(*i, comment_name, tmp);
	  erase_bogus_certs(tmp, app);
	  if (!tmp.empty())
	    {
	      cout << "Manifest Comments:" << endl << endl;
	      for (vector< manifest<cert> >::const_iterator j = tmp.begin();
		   j != tmp.end(); ++j)
		{
		  cert_value tv;
		  decode_base64(j->inner().value, tv);
		  cout << j->inner().key << ": " << tv << endl;
		}	  
	      cout << endl;
	    }
	  }
	}
      frontier = next_frontier;
    }
}




CMD(agraph, "debug", "", "dump ancestry graph to stdout")
{
  transaction_guard guard(app.db);

  set<string> nodes;
  multimap<string,string> branches;
  vector< pair<string, string> > edges;

  vector< manifest<cert> > certs;
  app.db.get_manifest_certs(ancestor_cert_name, certs);

  for(vector< manifest<cert> >::iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      nodes.insert(tv());
      nodes.insert(i->inner().ident());
      edges.push_back(make_pair(tv(), i->inner().ident()));
    }

  app.db.get_manifest_certs(branch_cert_name, certs);
  for(vector< manifest<cert> >::iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      nodes.insert(i->inner().ident()); // in case no edges were connected
      branches.insert(make_pair(i->inner().ident(), tv()));
    }  


  cout << "graph: " << endl << "{" << endl; // open graph
  for (set<string>::iterator i = nodes.begin(); i != nodes.end();
       ++i)
    {
      cout << "node: { title : \"" << *i << "\"\n"
	   << "        label : \"\\fb" << *i;
      pair<multimap<string,string>::const_iterator,
	multimap<string,string>::const_iterator> pair =
	branches.equal_range(*i);
      for (multimap<string,string>::const_iterator j = pair.first;
	   j != pair.second; ++j)
	{
	  cout << "\\n\\fn" << j->second;
	}
      cout << "\"}" << endl;
    }
  for (vector< pair<string,string> >::iterator i = edges.begin(); i != edges.end();
       ++i)
    {
      cout << "edge: { sourcename : \"" << i->first << "\"" << endl
	   << "        targetname : \"" << i->second << "\" }" << endl;
    }
  cout << "}" << endl << endl; // close graph
  guard.commit();
}


CMD(rcs_import, "rcs", "RCSFILE...", "import all versions in RCS files")
{
  if (args.size() < 1)
    throw usage(name);
  
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

  import_cvs_repo(mkpath(idx(args, 0)()), app);
}

 */

}; // namespace commands
