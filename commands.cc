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
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

#include "commands.hh"
#include "constants.hh"

#include "app_state.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "keys.hh"
#include "manifest.hh"
#include "network.hh"
#include "packet.hh"
#include "patch_set.hh"
#include "rcs_import.hh"
#include "sanity.hh"
#include "cert.hh"
#include "transforms.hh"
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
  struct std::greater<commands::command *>
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
  virtual void exec(app_state & app, vector<string> const & args) = 0;
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
      out << "     " << i->second->name << " " << i->second->params << endl
	  << "     " << i->second->desc << endl << endl;
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
      col2 = col2 > sorted[i]->cmdgroup.size() ? col2 : sorted[i]->cmdgroup.size();
    }

  for (size_t i = 0; i < sorted.size(); ++i)
    {
      if (sorted[i]->cmdgroup != curr_group)
	{
	  curr_group = sorted[i]->cmdgroup;
	  out << endl;
	  out << "  " << sorted[i]->cmdgroup;
	  col = sorted[i]->cmdgroup.size() + 2;
	  while (col++ < (col2 + 3))
	    out << ' ';
	}
      out << " " << sorted[i]->name;
      col += sorted[i]->name.size() + 1;
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

void process(app_state & app, string const & cmd, vector<string> const & args)
{
  if (cmds.find(cmd) != cmds.end())
    {
      L("executing %s command\n", cmd.c_str());
      cmds[cmd]->exec(app, args);
    }
  else
    {
      throw usage(cmd);
    }
}

#define CMD(C, group, params, desc)               \
struct cmd_ ## C : public command                 \
{                                                 \
  cmd_ ## C() : command(#C, group, params, desc)  \
  {}                                              \
  virtual void exec(app_state & app,              \
                    vector<string> const & args); \
};                                                \
static cmd_ ## C C ## _cmd;                       \
void cmd_ ## C::exec(app_state & app,             \
                     vector<string> const & args) \

#define ALIAS(C, realcommand, group, params, desc)	\
CMD(C, group, params, desc)				\
{							\
  process(app, string(#realcommand), args);		\
}


static void ensure_bookdir()
{
  mkdir_p(local_path(book_keeping_dir));
}

static void get_manifest_path(local_path & m_path)
{
  m_path = (fs::path(book_keeping_dir) / fs::path(manifest_file_name)).string();
  L("manifest path is %s\n", m_path().c_str());
}

static void get_work_path(local_path & w_path)
{
  w_path = (fs::path(book_keeping_dir) / fs::path(work_file_name)).string();
  L("work path is %s\n", w_path().c_str());
}

static void get_manifest_map(manifest_map & m)
{
  ensure_bookdir();
  local_path m_path;
  base64< gzip<data> > m_data;
  get_manifest_path(m_path);
  if (file_exists(m_path))
    {
      L("loading manifest file %s\n", m_path().c_str());      
      read_data(m_path, m_data);
      read_manifest_map(manifest_data(m_data), m);
      L("read %d manifest entries\n", m.size());
    }
  else
    {
      L("no manifest file %s\n", m_path().c_str());
    }
}

static void put_manifest_map(manifest_map const & m)
{
  ensure_bookdir();
  local_path m_path;
  manifest_data m_data;
  get_manifest_path(m_path);
  L("writing manifest file %s\n", m_path().c_str());
  write_manifest_map(m, m_data);
  write_data(m_path, m_data.inner());
  L("wrote %d manifest entries\n", m.size());
}

static void get_work_set(work_set & w)
{
  ensure_bookdir();
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    {
      L("checking for un-committed work file %s\n", 
	w_path().c_str());
      data w_data;
      read_data(w_path, w_data);
      read_work_set(w_data, w);
      L("read %d dels, %d adds from %s\n", 
	w.dels.size(), w.adds.size(), w_path().c_str());
    }
  else
    {
      L("no un-committed work file %s\n", w_path().c_str());
    }
}

static void put_work_set(work_set & w)
{
  local_path w_path;
  get_work_path(w_path);

  if (w.adds.size() > 0
      || w.dels.size() > 0)
    {
      ensure_bookdir();
      data w_data;
      write_work_set(w_data, w);
      write_data(w_path, w_data);
    }
  else
    {
      delete_file(w_path);
    }
}

static void calculate_new_manifest_map(manifest_map const & m_old, 
				       manifest_map & m_new)
{
  path_set paths;
  work_set work;
  extract_path_set(m_old, paths);
  get_work_set(work);
  if (work.dels.size() > 0)
    L("removing %d dead files from manifest\n", 
      work.dels.size());
  if (work.adds.size() > 0)
    L("adding %d files to manifest\n", work.adds.size());
  apply_work_set(work, paths);
  build_manifest_map(paths, m_new);
}

static string get_stdin()
{
  char buf[bufsz];
  string tmp;
  while(cin)
    {
      cin.read(buf, bufsz);
      tmp.append(buf, cin.gcount());
    }
  return tmp;
}

static void get_log_message(patch_set const & ps, 
			    app_state & app,
			    string & log_message)
{
  string commentary;
  string summary;
  stringstream ss;
  patch_set_to_text_summary(ps, ss);
  summary = ss.str();
  commentary += "----------------------------------------------------------------------\n";
  commentary += "Enter Log.  Lines beginning with `MT:' are removed automatically\n";
  commentary += "\n";
  commentary += "Summary of changes:\n";
  commentary += "\n";
  commentary += summary;
  commentary += "----------------------------------------------------------------------\n";
  N(app.lua.hook_edit_comment(commentary, log_message),
    "edit of log message failed");
}


// the goal here is to look back through the ancestry of the provided
// child, checking to see the least ancestor it has which we received from
// the given network url/group pair.
//
// we use the ancestor as the source manifest when building a patchset to
// send to that url/group.

static bool find_ancestor_on_netserver (manifest_id const & child, 
					url const & u,
					group const & g, 
					manifest_id & anc,
					app_state & app)
{
  set<manifest_id> frontier;
  cert_name tn(ancestor_cert_name);
  frontier.insert(child);

  while (!frontier.empty())
    {
      set<manifest_id> next_frontier;
      for (set<manifest_id>::const_iterator i = frontier.begin();
	   i != frontier.end(); ++i)
	{
	  vector< manifest<cert> > tmp;
	  app.db.get_manifest_certs(*i, tn, tmp);

	  // we go through this vector backwards because we would prefer to
	  // hit more recently-queued ancestors (such as intermediate nodes
	  // in a multi-node merge) rather than older ancestors. but of
	  // course, any ancestor will do.

	  for (vector< manifest<cert> >::reverse_iterator j = tmp.rbegin();
	       j != tmp.rend(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id anc_id (tv());

	      L("looking for parent %s of %s on server\n", 
		i->inner()().c_str(),
		anc_id.inner()().c_str());

	      if (app.db.manifest_exists_on_netserver (u, g, anc_id))
		{
		  L("found parent %s on server\n", anc_id.inner()().c_str());
		  anc = anc_id;
		  return true;
		}
	      else
		next_frontier.insert(anc_id);
	    }	  
	}

      frontier = next_frontier;
    }

  return false;
}


static void queue_edge_for_target_ancestor (pair<url,group> const & targ,
					    manifest_id const & child_id,
					    manifest_map const & child_map,
					    app_state & app)
{  
  // now here is an interesting thing: we *might* be sending data to a
  // depot, or someone with indeterminate pre-existing state (say the first
  // time we post to netnews), therefore we cannot just "send the edge" we
  // just constructed in a merge or commit, we need to send an edge from a
  // parent which we know to be present in the depot (or else an edge from
  // the empty map -- full contents of all files). what is sent therefore
  // changes on a depot-by-depot basis. this function calculates the
  // appropriate thing to send.
  //
  // nb: this has no direct relation to what we store in our own
  // database. we always store the edge from our parent, and we always know
  // when we have a parent.

  vector< pair<url, group> > one_target;
  one_target.push_back(targ);
  queueing_packet_writer qpw(app, one_target);
  
  manifest_data targ_ancestor_data;
  manifest_map targ_ancestor_map;
  manifest_id targ_ancestor_id;
  
  if (find_ancestor_on_netserver (child_id, 
				  targ.first, 
				  targ.second, 
				  targ_ancestor_id, 
				  app))
    {	    
      app.db.get_manifest_version(targ_ancestor_id, targ_ancestor_data);
      read_manifest_map(targ_ancestor_data, targ_ancestor_map);
    }

  patch_set ps;
  manifests_to_patch_set(targ_ancestor_map, child_map, app, ps);
  patch_set_to_packets(ps, app, qpw);

  // now that we've queued the data, we can note this new child
  // node as existing (well .. soon-to-exist) on the server
  app.db.note_manifest_on_netserver (targ.first, targ.second, child_id);

}


// this helper tries to produce merge <- mergeN(left,right), possibly
// merge3 if it can find an ancestor, otherwise merge2. it also queues the
// appropriate edges from known ancestors to the new merge node, to be
// transmitted to each of the targets provided.

static void try_one_merge(manifest_id const & left,
			  manifest_id const & right,
			  manifest_id & merged,
			  app_state & app, 
			  vector< pair<url,group> > const & targets)
{
  manifest_data left_data, right_data, ancestor_data, merged_data;
  manifest_map left_map, right_map, ancestor_map, merged_map;
  manifest_id ancestor;

  app.db.get_manifest_version(left, left_data);
  app.db.get_manifest_version(right, right_data);
  read_manifest_map(left_data, left_map);
  read_manifest_map(right_data, right_map);
  
  simple_merge_provider merger(app);
  
  if(find_common_ancestor(left, right, ancestor, app))	    
    {
      P("common ancestor %s found, trying merge3\n", ancestor.inner()().c_str()); 
      app.db.get_manifest_version(ancestor, ancestor_data);
      read_manifest_map(ancestor_data, ancestor_map);
      N(merge3(ancestor_map, left_map, right_map, 
	       app, merger, merged_map),
	(string("failed to merge manifests ")
	 + left.inner()() + " and " + right.inner()()));	      
    }
  else
    {
      P("no common ancestor found, trying merge2\n"); 
      N(merge2(left_map, right_map, app, merger, merged_map),
	(string("failed to merge manifests ")
	 + left.inner()() + " and " + right.inner()()));	      
    }
  
  write_manifest_map(merged_map, merged_data);
  calculate_manifest_map_ident(merged_map, merged);	  
  
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
    
    // make sure the appropriate edges get queued for the network.
    for (vector< pair<url,group> >::const_iterator targ = targets.begin();
	 targ != targets.end(); ++targ)
      {
	queue_edge_for_target_ancestor (*targ, merged, merged_map, app);
      }
    
    queueing_packet_writer qpw(app, targets);
    cert_manifest_ancestor(left, merged, app, qpw);
    cert_manifest_ancestor(right, merged, app, qpw);
    cert_manifest_date_now(merged, app, qpw);
    cert_manifest_author_default(merged, app, qpw);
  }

}			  


// actual commands follow

CMD(lscerts, "key and cert", "(file|manifest) <id>", 
    "list certs associated with manifest or file")
{
  if (args.size() != 2)
    throw usage(name);

  vector<cert> certs;

  transaction_guard guard(app.db);

  if (args[0] == "manifest")
    {
      manifest_id ident(args[1]);
      vector< manifest<cert> > ts;
      app.db.get_manifest_certs(ident, ts);
      for (size_t i = 0; i < ts.size(); ++i)
	certs.push_back(ts[i].inner());
    }
  else if (args[0] == "file")
    {
      file_id ident(args[1]);
      vector< file<cert> > ts;
      app.db.get_file_certs(ident, ts);
      for (size_t i = 0; i < ts.size(); ++i)
	certs.push_back(ts[i].inner());
    }
  else
    throw usage(name);
	
  for (size_t i = 0; i < certs.size(); ++i)
    {
      bool ok = check_cert(app, certs[i]);
      cert_value tv;      
      decode_base64(certs[i].value, tv);
      string washed;
      if (guess_binary(tv()))
	{
	  washed = "<binary data>";
	}
      else
	{
	  washed = tv();
	}
      string head = string(ok ? "ok sig from " : "bad sig from ")
	+ "[" + certs[i].key() + "] : " 
	+ "[" + certs[i].name() + "] = [";
      string pad(head.size(), ' ');
      vector<string> lines;
      split_into_lines(washed, lines);
      I(lines.size() > 0);
      cout << head << lines[0] ;
      for (size_t i = 1; i < lines.size(); ++i)
	cout << endl << pad << lines[i];
      cout << "]" << endl;
    }  
  guard.commit();
}

CMD(lskeys, "key and cert", "[partial-id]", "list keys")
{
  vector<rsa_keypair_id> pubkeys;
  vector<rsa_keypair_id> privkeys;

  transaction_guard guard(app.db);

  if (args.size() == 0)
    app.db.get_key_ids("", pubkeys, privkeys);
  else if (args.size() == 1)
    app.db.get_key_ids(args[0], pubkeys, privkeys);
  else
    throw usage(name);
  
  if (pubkeys.size() > 0)
    {
      cout << endl << "[public keys]" << endl;
      for (size_t i = 0; i < pubkeys.size(); ++i)
	cout << pubkeys[i]() << endl;
      cout << endl;
    }

  if (privkeys.size() > 0)
    {
      cout << endl << "[private keys]" << endl;
      for (size_t i = 0; i < privkeys.size(); ++i)
	cout << privkeys[i]() << endl;
      cout << endl;
    }

  guard.commit();
}

CMD(genkey, "key and cert", "<keyid>", "generate an RSA key-pair")
{
  if (args.size() != 1)
    throw usage(name);
  
  transaction_guard guard(app.db);
  rsa_keypair_id ident(args[0]);

  N(! app.db.key_exists(ident),
    (string("key '") + ident() + "' already exists in database"));
  
  base64<rsa_pub_key> pub;
  base64< arc4<rsa_priv_key> > priv;
  P("generating key-pair '%s'\n", ident().c_str());
  generate_key_pair(app.lua, ident, pub, priv);
  P("storing key-pair '%s' in database\n", ident().c_str());
  app.db.put_key_pair(ident, pub, priv);

  guard.commit();
}

CMD(cert, "key and cert", "(file|manifest) <id> <certname> [certval]", 
                        "create a cert for a file or manifest")
{
  if ((args.size() != 4) && (args.size() != 3))
    throw usage(name);

  transaction_guard guard(app.db);

  hexenc<id> ident(args[1]);
  cert_name name(args[2]);

  rsa_keypair_id key;
  if (app.signing_key() != "")
    key = app.signing_key;
  else
    N(guess_default_key(key, app),
      "no unique private key found, and no key specified");
  
  cert_value val;
  if (args.size() == 4)
    val = cert_value(args[3]);
  else
    val = cert_value(get_stdin());

  base64<cert_value> val_encoded;
  encode_base64(val, val_encoded);

  cert t(ident, name, val_encoded, key);
  
  // nb: we want to throw usage on mis-use *before* asking for a
  // passphrase.

  if (args[0] == "file")
    {
      calculate_cert(app, t);
      app.db.put_file_cert(file<cert>(t));
    }
  else if (args[0] == "manifest")
    {
      calculate_cert(app, t);
      app.db.put_manifest_cert(manifest<cert>(t));
    }
  else
    throw usage(this->name);

  guard.commit();
}


CMD(tag, "certificate", "<id> <tagname>", 
    "put a symbolic tag cert on a manifest version")
{
  if (args.size() != 2)
    throw usage(name);
  manifest_id m(args[0]);
  packet_db_writer dbw(app);
  cert_manifest_tag(m, args[1], app, dbw);
}

CMD(approve, "certificate", "(file|manifest) <id>", 
    "approve of a manifest or file version")
{
  if (args.size() != 2)
    throw usage(name);
  if (args[0] == "manifest")
    {
      manifest_id m(args[1]);
      packet_db_writer dbw(app);
      cert_manifest_approval(m, true, app, dbw);
    }
  else if (args[0] == "file")
    {
      packet_db_writer dbw(app);
      file_id f(args[1]);
      cert_file_approval(f, true, app, dbw);
    }
  else
    throw usage(name);
}

CMD(disapprove, "certificate", "(file|manifest) <id>", 
    "disapprove of a manifest or file version")
{
  if (args.size() != 2)
    throw usage(name);
  if (args[0] == "manifest")
    {
      manifest_id m(args[1]);
      packet_db_writer dbw(app);
      cert_manifest_approval(m, false, app, dbw);
    }
  else if (args[0] == "file")
    {
      file_id f(args[1]);
      packet_db_writer dbw(app);
      cert_file_approval(f, false, app, dbw);
    }
  else
    throw usage(name);
}


CMD(comment, "certificate", "(file|manifest) <id> [comment]", 
    "comment on a file or manifest version")
{
  if (args.size() != 2 && args.size() != 3)
    throw usage(name);

  string comment;
  if (args.size() == 3)
    comment = args[2];
  else
    N(app.lua.hook_edit_comment("", comment), "edit comment failed");
  
  N(comment.find_first_not_of(" \r\t\n") == string::npos, "empty comment");

  if (args[0] == "file")
    {
      packet_db_writer dbw(app);
      cert_file_comment(file_id(args[1]), comment, app, dbw); 
    }
  else if (args[0] == "manifest")
    {
      packet_db_writer dbw(app);
      cert_manifest_comment(manifest_id(args[1]), comment, app, dbw);
    }
  else
    throw usage(name);
}



CMD(add, "working copy", "<pathname> [...]", "add files to working copy")
{
  if (args.size() < 1)
    throw usage(name);

  transaction_guard guard(app.db);

  manifest_map man;
  work_set work;  
  get_manifest_map(man);
  get_work_set(work);
  bool rewrite_work = false;

  for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_addition(file_path(*i), app, work, man, rewrite_work);
  
  guard.commit();
  
  // small race here
  if (rewrite_work)
    put_work_set(work);
}

CMD(drop, "working copy", "<pathname> [...]", "drop files from working copy")
{
  if (args.size() < 1)
    throw usage(name);

  manifest_map man;
  work_set work;
  get_manifest_map(man);
  get_work_set(work);
  bool rewrite_work = false;

  transaction_guard guard(app.db);

  for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_deletion(file_path(*i), app, work, man, rewrite_work);
  
  guard.commit();

  // small race here
  if (rewrite_work)
    put_work_set(work);
}

CMD(commit, "working copy", "[log message]", "commit working copy to database")
{
  string log_message("");
  manifest_map m_old, m_new;
  patch_set ps;

  get_manifest_map(m_old);
  calculate_new_manifest_map(m_old, m_new);
  manifest_id old_id, new_id;
  calculate_manifest_map_ident(m_old, old_id);
  calculate_manifest_map_ident(m_new, new_id);

  if (args.size() != 0 && args.size() != 1)
    throw usage(name);
  
  cert_value branchname;
  if (app.branch_name != "")
    {
      branchname = app.branch_name;
    }
  else
    {
      vector< manifest<cert> > certs;
      cert_name branch(branch_cert_name);
      app.db.get_manifest_certs(old_id, branch, certs);

      N(certs.size() != 0, 
	string("no branch certs found for old manifest ")
	+ old_id.inner()() + ", please provide a branch name");

      N(certs.size() == 1,
	string("multiple branch certs found for old manifest ")
	+ old_id.inner()() + ", please provide a branch name");

      decode_base64(certs[0].inner().value, branchname);
    }
    
  L("committing %s to branch %s\n", 
    new_id.inner()().c_str(), branchname().c_str());
  app.branch_name = branchname();

  manifests_to_patch_set(m_old, m_new, app, ps);

  // get log message
  if (args.size() == 1)
    log_message = args[0];
  else
    get_log_message(ps, app, log_message);

  N(log_message.find_first_not_of(" \r\t\n") != string::npos,
    "empty log message");

  {
    transaction_guard guard(app.db);

    // process manifest delta or new manifest
    if (app.db.manifest_version_exists(ps.m_new))
      {
	L("skipping manifest %s, already in database\n", ps.m_new.inner()().c_str());
      }
    else
      {
	if (app.db.manifest_version_exists(ps.m_old))
	  {
	    L("inserting manifest delta %s -> %s\n", 
	      ps.m_old.inner()().c_str(), ps.m_new.inner()().c_str());
	    manifest_data m_old_data, m_new_data;
	    app.db.get_manifest_version(ps.m_old, m_old_data);
	    write_manifest_map(m_new, m_new_data);
	    base64< gzip<delta> > del;
	    diff(m_old_data.inner(), m_new_data.inner(), del);
	    app.db.put_manifest_version(ps.m_old, ps.m_new, manifest_delta(del));
	  }
	else
	  {
	    L("inserting full manifest %s\n", 
	      ps.m_new.inner()().c_str());
	    manifest_data m_new_data;
	    write_manifest_map(m_new, m_new_data);
	    app.db.put_manifest(ps.m_new, m_new_data);
	  }
      }

    // process file deltas
    for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
	 i != ps.f_deltas.end(); ++i)
      {
	if (app.db.file_version_exists(i->id_new))
	  {
	    L("skipping file delta %s, already in database\n", i->id_new.inner()().c_str());
	  }
	else
	  {
	    if (app.db.file_version_exists(i->id_old))
	      {
		L("inserting delta %s -> %s\n", 
		  i->id_old.inner()().c_str(), i->id_new.inner()().c_str());
		file_data old_data;
		base64< gzip<data> > new_data;
		app.db.get_file_version(i->id_old, old_data);
		read_data(i->path, new_data);
		base64< gzip<delta> > del;
		diff(old_data.inner(), new_data, del);
		app.db.put_file_version(i->id_old, i->id_new, file_delta(del));
	      }
	    else
	      {
		L("inserting full version %s\n", i->id_old.inner()().c_str());
		base64< gzip<data> > new_data;
		read_data(i->path, new_data);
		// sanity check
		hexenc<id> tid;
		calculate_ident(new_data, tid);
		I(tid == i->id_new.inner());
		app.db.put_file(i->id_new, file_data(new_data));
	      }
	  }
      }
  
    // process file adds
    for (set<patch_addition>::const_iterator i = ps.f_adds.begin();
	 i != ps.f_adds.end(); ++i)
      {
	if (app.db.file_version_exists(i->ident))
	  {
	    L("skipping file %s %s, already in database\n", 
	      i->path().c_str(), i->ident.inner()().c_str());
	  }
	else
	  {
	    // it's a new file
	    L("inserting new file %s %s\n", 
	      i->path().c_str(), i->ident.inner()().c_str());
	    base64< gzip<data> > new_data;
	    read_data(i->path, new_data);
	    app.db.put_file(i->ident, new_data);
	  }
      }

    packet_db_writer dbw(app);

    if (! m_old.empty())
      cert_manifest_ancestor(ps.m_old, ps.m_new, app, dbw);

    cert_manifest_in_branch(ps.m_new, branchname, app, dbw); 
    cert_manifest_date_now(ps.m_new, app, dbw);
    cert_manifest_author_default(ps.m_new, app, dbw);
    cert_manifest_changelog(ps.m_new, log_message, app, dbw);

    // commit done, now queue diff for sending

    if (app.db.manifest_version_exists(ps.m_new))
      {
	vector< pair<url,group> > targets;
	app.lua.hook_get_post_targets(branchname, targets);
	
	// make sure the appropriate edges get queued for the network.
	for (vector< pair<url,group> >::const_iterator targ = targets.begin();
	     targ != targets.end(); ++targ)
	  {
	    queue_edge_for_target_ancestor (*targ, ps.m_new, m_new, app);
	  }
	
	// throw in all available certs for good measure
	queueing_packet_writer qpw(app, targets);
	vector< manifest<cert> > certs;
	app.db.get_manifest_certs(ps.m_new, certs);
	for(vector< manifest<cert> >::const_iterator i = certs.begin();
	    i != certs.end(); ++i)
	  qpw.consume_manifest_cert(*i);
      } 
    
    guard.commit();
  }
  // small race condition here...
  local_path w_path;
  get_work_path(w_path);
  delete_file(w_path);
  put_manifest_map(m_new);
  P("committed %s\n", ps.m_new.inner()().c_str());
}

CMD(update, "working copy", "[sort keys...]", "update working copy, relative to sorting keys")
{

  manifest_data m_chosen_data;
  manifest_map m_old, m_working, m_chosen, m_new;
  manifest_id m_old_id, m_chosen_id;

  transaction_guard guard(app.db);

  get_manifest_map(m_old);
  calculate_manifest_map_ident(m_old, m_old_id);
  calculate_new_manifest_map(m_old, m_working);
  
  pick_update_target(m_old_id, args, app, m_chosen_id);
  P("selected update target %s\n",
    m_chosen_id.inner()().c_str());
  app.db.get_manifest_version(m_chosen_id, m_chosen_data);
  read_manifest_map(m_chosen_data, m_chosen);

  update_merge_provider merger(app);
  N(merge3(m_old, m_chosen, m_working, app, merger, m_new),
    string("manifest merge failed, no update performed"));

  P("calculating patchset for update\n");
  patch_set ps;
  manifests_to_patch_set(m_working, m_new, app, ps);

  L("applying %d deletions to files in tree\n", ps.f_dels.size());
  for (set<file_path>::const_iterator i = ps.f_dels.begin();
       i != ps.f_dels.end(); ++i)
    {
      L("deleting %s\n", (*i)().c_str());
      delete_file(*i);
    }

  L("applying %d moves to files in tree\n", ps.f_moves.size());
  for (set<patch_move>::const_iterator i = ps.f_moves.begin();
       i != ps.f_moves.end(); ++i)
    {
      L("moving %s -> %s\n", i->path_old().c_str(), i->path_new().c_str());
      move_file(i->path_old, i->path_new);
    }
  
  L("applying %d additions to tree\n", ps.f_adds.size());
  for (set<patch_addition>::const_iterator i = ps.f_adds.begin();
       i != ps.f_adds.end(); ++i)
    {
      L("adding %s as %s\n", i->ident.inner()().c_str(), i->path().c_str());
      file_data tmp;
      if (app.db.file_version_exists(i->ident))
	app.db.get_file_version(i->ident, tmp);
      else if (merger.temporary_store.find(i->ident) != merger.temporary_store.end())
	tmp = merger.temporary_store[i->ident];
      else
	I(false); // trip assert. this should be impossible.
      write_data(i->path, tmp.inner());
    }

  L("applying %d deltas to tree\n", ps.f_deltas.size());
  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    {
      P("updating file %s: %s -> %s\n", 
	i->path().c_str(),
	i->id_old.inner()().c_str(),
	i->id_new.inner()().c_str());
      
      // sanity check
      {
	base64< gzip<data> > dtmp;
	hexenc<id> dtmp_id;
	read_data(i->path, dtmp);
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
	write_data(i->path, tmp.inner());
      }
    }
  
  L("update successful\n");
  guard.commit();

  // small race condition here...
  // nb: we write out m_chosen, not m_new, because the manifest-on-disk
  // is the basis of the working copy, not the working copy itself.
  put_manifest_map(m_chosen);
  P("updated to base version %s\n", m_chosen_id.inner()().c_str());
}



CMD(cat, "tree", "(file|manifest) <id>", "write file or manifest from database to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);

  if (args[0] == "file")
    {
      file_data dat;
      file_id ident(args[1]);

      N(app.db.file_version_exists(ident),
	(string("no file version ") + ident.inner()() + " found in database"));

      L("dumping file %s\n", ident.inner()().c_str());
      app.db.get_file_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());

    }
  else if (args[0] == "manifest")
    {
      manifest_data dat;
      manifest_id ident(args[1]);

      N(app.db.manifest_version_exists(ident),
	(string("no file version ") + ident.inner()() + " found in database"));

      L("dumping manifest %s\n", ident.inner()().c_str());
      app.db.get_manifest_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }
  else 
    throw usage(name);

  guard.commit();
}


CMD(checkout, "tree", "<manifest-id>", "check out tree state from database")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);

  file_data data;
  manifest_id ident(args[0]);
  manifest_map m;

  N(app.db.manifest_version_exists(ident),
    (string("no manifest version ") + ident.inner()() + " found in database"));
  
  L("exporting manifest %s\n", ident.inner()().c_str());
  manifest_data m_data;
  app.db.get_manifest_version(ident, m_data);
  read_manifest_map(m_data, m);      
  put_manifest_map(m);
  
  for (manifest_map::const_iterator i = m.begin(); i != m.end(); ++i)
    {
      vector<string> args;
      path_id_pair pip(*i);
      
      N(app.db.file_version_exists(pip.ident()),
	(string("no file version ")
	 + pip.ident().inner()() 
	 + " found in database for "
	 + pip.path()().c_str()));
      
      file_data dat;
      L("writing file %s to %s\n", 
	pip.ident().inner()().c_str(),
	pip.path()().c_str());
      app.db.get_file_version(pip.ident(), dat);
      write_data(pip.path(), dat.inner());
    }

  guard.commit();
}

ALIAS(co, checkout, "tree", "<manifest-id>",
      "check out tree state from database; alias for checkout")

CMD(heads, "tree", "", "show unmerged heads of branch")
{
  vector<manifest_id> heads;
  if (args.size() != 0)
    throw usage(name);

  if (app.branch_name == "")
    {
      cout << "please specify a branch, with --branch=<branchname>" << endl;
      return;
    }

  get_branch_heads(app.branch_name, app, heads);

  if (heads.size() == 0)
    cout << "branch '" << app.branch_name << "' is empty" << endl;
  else if (heads.size() == 1)
    cout << "branch '" << app.branch_name << "' is currently merged:" << endl;
  else
    cout << "branch '" << app.branch_name << "' is currently unmerged:" << endl;

  for (vector<manifest_id>::const_iterator i = heads.begin(); 
       i != heads.end(); ++i)
    {
      cout << i->inner()() << endl;
    }
}


CMD(merge, "tree", "", "merge unmerged heads of branch")
{

  vector<manifest_id> heads;

  if (args.size() != 0)
    throw usage(name);

  if (app.branch_name == "")
    {
      cout << "please specify a branch, with --branch=<branchname>" << endl;
      return;
    }

  get_branch_heads(app.branch_name, app, heads);

  if (heads.size() == 0)
    {
      cout << "branch " << args[0] << "is empty" << endl;
      return;
    }
  else if (heads.size() == 1)
    {
      cout << "branch " << args[0] << "is merged" << endl;
      return;
    }
  else
    {
      vector< pair<url,group> > targets;
      app.lua.hook_get_post_targets(app.branch_name, targets);

      manifest_id left = heads[0];
      manifest_id ancestor;
      for (size_t i = 1; i < heads.size(); ++i)
	{
	  manifest_id right = heads[i];
	  P("merging with manifest %d / %d: %s <-> %s\n",
	    i, heads.size(),
	    left.inner()().c_str(), right.inner()().c_str());

	  manifest_id merged;
	  transaction_guard guard(app.db);
	  try_one_merge (left, right, merged, app, targets);
	  	  
	  // merged 1 edge; now we commit this, update merge source and
	  // try next one

	  packet_db_writer dbw(app);
	  queueing_packet_writer qpw(app, targets);
	  cert_manifest_in_branch(merged, app.branch_name, app, dbw);
	  cert_manifest_in_branch(merged, app.branch_name, app, qpw);

	  string log = "merge of " + left.inner()() + " and " + right.inner()();
	  cert_manifest_changelog(merged, log, app, dbw);
	  cert_manifest_changelog(merged, log, app, qpw);
	  
	  guard.commit();
	  P("[source] %s\n[source] %s\n[merged] %s\n",
	    left.inner()().c_str(),
	    right.inner()().c_str(),
	    merged.inner()().c_str());
	  left = merged;
	}
    }  
}


CMD(propagate, "tree", "<src-branch> <dst-branch>", 
    "merge from one branch to another asymmetrically")
{
  /*  

  this is a special merge operator, but very useful for people maintaining
  "slightly disparate but related" trees. it does a one-way merge; less
  powerful than putting things in the same branch and also more flexible.

  1. check to see if src and dst branches are merged, if not abort, if so
     call heads N1 and N2 respectively.

  2. (FIXME: not yet present) run the hook propagate ("src-branch",
     "dst-branch", N1, N2) which gives the user a chance to massage N1 into
     a state which is likely to "merge nicely" with N2, eg. edit pathnames,
     omit optional files of no interest.

  3. do a normal 2 or 3-way merge on N1 and N2, depending on the
     existence of common ancestors.

  4. save the results as the delta (N2,M), the ancestry edges (N1,M)
     and (N2,M), and the cert (N2,dst).

  5. queue the resulting packets to send to the url for dst-branch, not
     src-branch.

  */

  vector<manifest_id> src_heads, dst_heads;

  if (args.size() != 2)
    throw usage(name);

  get_branch_heads(args[0], app, src_heads);
  get_branch_heads(args[1], app, dst_heads);

  if (src_heads.size() == 0)
    {
      cout << "branch " << args[0] << "is empty" << endl;
      return;
    }
  else if (src_heads.size() != 1)
    {
      cout << "branch " << args[0] << "is not merged" << endl;
      return;
    }
  else if (dst_heads.size() == 0)
    {
      cout << "branch " << args[1] << "is empty" << endl;
      return;
    }
  else if (dst_heads.size() != 1)
    {
      cout << "branch " << args[1] << "is not merged" << endl;
      return;
    }
  else
    {
      vector< pair<url,group> > targets;
      app.lua.hook_get_post_targets(args[1], targets);

      manifest_id merged;
      transaction_guard guard(app.db);
      try_one_merge (src_heads[0], dst_heads[0], merged, app, targets);      

      queueing_packet_writer qpw(app, targets);
      cert_manifest_in_branch(merged, app.branch_name, app, qpw);
      cert_manifest_changelog(merged, 
			      "propagate of " 
			      + src_heads[0].inner()() 
			      + " and " 
			      + dst_heads[0].inner()()
			      + "\n"
			      + "from branch " 
			      + args[0] + " to " + args[1] + "\n", 
			      app, qpw);	  
      guard.commit();      
    }
}



CMD(diff, "informative", "", "show current diffs on stdout")
{
  manifest_map m_old, m_new;
  patch_set ps;

  transaction_guard guard(app.db);

  get_manifest_map(m_old);
  calculate_new_manifest_map(m_old, m_new);
  manifests_to_patch_set(m_old, m_new, app, ps);

  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    {
      file_data f_old;
      gzip<data> decoded_old;
      data decompressed_old, decompressed_new;
      vector<string> old_lines, new_lines;

      app.db.get_file_version(i->id_old, f_old);
      decode_base64(f_old.inner(), decoded_old);
      decode_gzip(decoded_old, decompressed_old);

      read_data(i->path, decompressed_new);

      split_into_lines(decompressed_old(), old_lines);
      split_into_lines(decompressed_new(), new_lines);

      unidiff(i->path(), i->path(), old_lines, new_lines, cout);
    }  
  guard.commit();
}

CMD(status, "informative", "", "show status of working copy")
{
  manifest_map m_old, m_new;
  patch_set ps;

  transaction_guard guard(app.db);
  get_manifest_map(m_old);
  calculate_new_manifest_map(m_old, m_new);
  manifests_to_patch_set(m_old, m_new, app, ps);
  patch_set_to_text_summary(ps, cout);
  guard.commit();
}


CMD(mdelta, "packet i/o", "<oldid> <newid>", "write manifest delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_old_id, m_new_id; 
  manifest_data m_old_data, m_new_data;
  manifest_map m_old, m_new;
  patch_set ps;      
  m_old_id = hexenc<id>(args[0]); 
  m_new_id = hexenc<id>(args[1]);
  app.db.get_manifest_version(m_old_id, m_old_data);
  app.db.get_manifest_version(m_new_id, m_new_data);
  read_manifest_map(m_old_data, m_old);
  read_manifest_map(m_new_data, m_new);
  manifests_to_patch_set(m_old, m_new, app, ps);
  patch_set_to_packets(ps, app, pw);  
  guard.commit();
}

CMD(fdelta, "packet i/o", "<oldid> <newid>", "write file delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_old_id, f_new_id;
  file_data f_old_data, f_new_data;
  f_old_id = hexenc<id>(args[0]);
  f_new_id = hexenc<id>(args[1]);     
  app.db.get_file_version(f_old_id, f_old_data);
  app.db.get_file_version(f_new_id, f_new_data);
  base64< gzip<delta> > del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));  
  guard.commit();
}

CMD(mdata, "packet i/o", "<id>", "write manifest data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_id;
  manifest_data m_data;
  m_id = hexenc<id>(args[0]);
  app.db.get_manifest_version(m_id, m_data);
  pw.consume_manifest_data(m_id, m_data);  
  guard.commit();
}


CMD(fdata, "packet i/o", "<id>", "write file data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_id;
  file_data f_data;
  f_id = hexenc<id>(args[0]);
  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);  
  guard.commit();
}

CMD(mcerts, "packet i/o", "<id>", "write manifest cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_id;
  vector< manifest<cert> > certs;

  m_id = hexenc<id>(args[0]);
  app.db.get_manifest_certs(m_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_manifest_cert(certs[i]);
  guard.commit();
}

CMD(fcerts, "packet i/o", "<id>", "write file cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_id;
  vector< file<cert> > certs;

  f_id = hexenc<id>(args[0]);
  app.db.get_file_certs(f_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_file_cert(certs[i]);
  guard.commit();
}

CMD(pubkey, "packet i/o", "<id>", "write public key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);
  rsa_keypair_id ident(args[0]);
  base64< rsa_pub_key > key;
  app.db.get_key(ident, key);
  pw.consume_public_key(ident, key);
  guard.commit();
}

CMD(privkey, "packet i/o", "<id>", "write private key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);
  rsa_keypair_id ident(args[0]);
  base64< arc4<rsa_priv_key> > key;
  app.db.get_key(ident, key);
  pw.consume_private_key(ident, key);
  guard.commit();
}


CMD(read, "packet i/o", "", "read packets from stdin")
{
  transaction_guard guard(app.db);
  packet_db_writer dbw(app, true);
  size_t count = read_packets(cin, dbw);
  N(count != 0, "no packets found on stdin");
  if (count == 1)
    P("read 1 packet\n");
  else
    P("read %d packets\n", count);
  guard.commit();
}


CMD(agraph, "graph visualization", "", "dump ancestry graph to stdout")
{
  vector< manifest<cert> > certs;
  transaction_guard guard(app.db);
  app.db.get_manifest_certs(ancestor_cert_name, certs);
  set<string> nodes;
  vector< pair<string, string> > edges;
  for(vector< manifest<cert> >::iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      nodes.insert(tv());
      nodes.insert(i->inner().ident());
      edges.push_back(make_pair(tv(), i->inner().ident()));
    }
  cout << "graph: " << endl << "{" << endl; // open graph
  for (set<string>::iterator i = nodes.begin(); i != nodes.end();
       ++i)
    {
      cout << "node: { title : \"" << *i << "\"}" << endl;
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

CMD(fetch, "network", "[URL] [groupname]", "fetch recent changes from network")
{
  if (args.size() > 2)
    throw usage(name);

  vector< pair<url,group> > sources;

  if (args.size() == 0)
    {
      if (app.branch_name == "")
	{
	  P("no branch name provided, fetching from all known URLs\n");
	  app.db.get_all_known_sources(sources);
	}
      else
	{
	  N(app.lua.hook_get_fetch_sources(app.branch_name, sources),
	    ("no URL / group pairs found for branch " + app.branch_name)); 
	}
    }
  else
    {
      N(args.size() == 2, "need URL and groupname");
      sources.push_back(make_pair(url(args[0]),
				  group(args[1])));
    }
  
  fetch_queued_blobs_from_network(sources, app);
}

CMD(post, "network", "[URL] [groupname]", "post queued changes to network")
{
  if (args.size() > 2)
    throw usage(name);

  vector< pair<url,group> > targets;
  if (args.size() == 0)
    {
      if (app.branch_name == "")
	{
	  P("no branch name provided, posting all queued targets\n");
	  app.db.get_queued_targets(targets);
	}
      else
	{
	  N(app.lua.hook_get_post_targets(app.branch_name, targets),
	    ("no URL / group pairs found for branch " + app.branch_name)); 
	}
    }  
  else
    {
      N(args.size() == 2, "need URL and groupname");
      targets.push_back(make_pair(url(args[0]),
				  group(args[1])));
    }

  post_queued_blobs_to_network(targets, app);
}


CMD(rcs_import, "rcs", "<rcsfile> ...", "import all versions in RCS files")
{
  if (args.size() < 1)
    throw usage(name);
  
  transaction_guard guard(app.db);
  for (vector<string>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      import_rcs_file(fs::path(*i), app.db);
    }
  guard.commit();
}


CMD(cvs_import, "rcs", "<cvsroot>", "import all versions in CVS repository")
{
  if (args.size() != 1)
    throw usage(name);

  import_cvs_repo(fs::path(args.at(0)), app);
}


}; // namespace commands
