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

  int process(app_state & app, string const & cmd, vector<string> const & args)
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

static bool bookdir_exists()
{
  return directory_exists(local_path(book_keeping_dir));
}

static void ensure_bookdir()
{
  mkdir_p(local_path(book_keeping_dir));
}

static void get_manifest_path(local_path & m_path)
{
  m_path = (fs::path(book_keeping_dir) / fs::path(manifest_file_name)).string();
  L(F("manifest path is %s\n") % m_path);
}

static void get_work_path(local_path & w_path)
{
  w_path = (fs::path(book_keeping_dir) / fs::path(work_file_name)).string();
  L(F("work path is %s\n") % w_path);
}

static void get_manifest_map(manifest_map & m)
{
  ensure_bookdir();
  local_path m_path;
  base64< gzip<data> > m_data;
  get_manifest_path(m_path);
  if (file_exists(m_path))
    {
      L(F("loading manifest file %s\n") % m_path);      
      read_data(m_path, m_data);
      read_manifest_map(manifest_data(m_data), m);
      L(F("read %d manifest entries\n") % m.size());
    }
  else
    {
      L(F("no manifest file %s\n") % m_path);
    }
}

static void put_manifest_map(manifest_map const & m)
{
  ensure_bookdir();
  local_path m_path;
  manifest_data m_data;
  get_manifest_path(m_path);
  L(F("writing manifest file %s\n") % m_path);
  write_manifest_map(m, m_data);
  write_data(m_path, m_data.inner());
  L(F("wrote %d manifest entries\n") % m.size());
}

static void get_work_set(work_set & w)
{
  ensure_bookdir();
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    {
      L(F("checking for un-committed work file %s\n") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_work_set(w_data, w);
      L(F("read %d dels, %d adds, %d renames from %s\n") %
	w.dels.size() % w.adds.size() % w.renames.size() % w_path);
    }
  else
    {
      L(F("no un-committed work file %s\n") % w_path);
    }
}

static void remove_work_set()
{
  local_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    delete_file(w_path);
}

static void put_work_set(work_set & w)
{
  local_path w_path;
  get_work_path(w_path);
  
  if (w.adds.size() > 0
      || w.dels.size() > 0
      || w.renames.size() > 0)
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

static void update_any_attrs(app_state & app)
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

static void calculate_new_manifest_map(manifest_map const & m_old, 
				       manifest_map & m_new,
				       rename_set & renames)
{
  path_set paths;
  work_set work;
  extract_path_set(m_old, paths);
  get_work_set(work);
  if (work.dels.size() > 0)
    L(F("removing %d dead files from manifest\n") %
      work.dels.size());
  if (work.adds.size() > 0)
    L(F("adding %d files to manifest\n") % 
      work.adds.size());
  if (work.renames.size() > 0)
    L(F("renaming %d files in manifest\n") % 
      work.renames.size());
  apply_work_set(work, paths);
  build_manifest_map(paths, m_new);
  renames = work.renames;
}


static void calculate_new_manifest_map(manifest_map const & m_old, 
				       manifest_map & m_new)
{
  rename_set dummy;
  calculate_new_manifest_map (m_old, m_new, dummy);
}


static string get_stdin()
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
  commentary += summary;
  commentary += "----------------------------------------------------------------------\n";
  N(app.lua.hook_edit_comment(commentary, log_message),
    F("edit of log message failed"));
}

template <typename ID>
static void complete(app_state & app, 
		     string const & str, 
		     ID & completion)
{
  N(str.find_first_not_of("abcdef0123456789") == string::npos,
    F("non-hex digits in id"));
  if (str.size() == 40)
    {
      completion = ID(str);
      return;
    }
  set<ID> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have a unique expansion") % str);
  if (completions.size() > 1)
    {
      string err = (F("partial id '%s' has multiple ambiguous expansions: \n") % str).str();
      for (typename set<ID>::const_iterator i = completions.begin();
	   i != completions.end(); ++i)
	err += (i->inner()() + "\n");
      N(completions.size() == 1, err);
    }
  completion = *(completions.begin());  
  P(F("expanded partial id '%s' to '%s'\n") % str % completion);
}


static void find_oldest_ancestors (manifest_id const & child, 
				   set<manifest_id> & ancs,
				   app_state & app)
{
  cert_name tn(ancestor_cert_name);
  ancs.insert(child);  
  while (true)
    {
      set<manifest_id> next_frontier;
      for (set<manifest_id>::const_iterator i = ancs.begin();
	   i != ancs.end(); ++i)
	{
	  vector< manifest<cert> > tmp;
	  app.db.get_manifest_certs(*i, tn, tmp);
	  erase_bogus_certs(tmp, app);
	  for (vector< manifest<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id anc_id (tv());
	      next_frontier.insert(anc_id);
	    }
	}
      if (next_frontier.empty())
	break;
      else
	ancs = next_frontier;
    }
}

// the goal here is to look back through the ancestry of the provided
// child, checking to see the least ancestor it has which we received from
// the given network url.
//
// we use the ancestor as the source manifest when building a patchset to
// send to that url.

static bool find_ancestor_on_netserver (manifest_id const & child, 
					url const & u,
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
	  erase_bogus_certs(tmp, app);

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

	      L(F("looking for parent %s of %s on server\n") % (*i) % anc_id);

	      if (app.db.manifest_exists_on_netserver (u, anc_id))
		{
		  L(F("found parent %s on server\n") % anc_id);
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


static void queue_edge_for_target_ancestor (url const & targ,
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

  set<url> one_target;
  one_target.insert(targ);
  queueing_packet_writer qpw(app, one_target);
  
  manifest_id targ_ancestor_id;
  
  if (find_ancestor_on_netserver (child_id, 
				  targ,
				  targ_ancestor_id, 
				  app))
    {
      // write everything from there to here
      reverse_queue rq(app.db);
      qpw.rev.reset(rq);
      write_ancestry_paths(targ_ancestor_id, child_id, app, qpw);
      qpw.rev.reset();
    }
  else
    {
      // or just write a complete version of "here"
      manifest_map empty_manifest;
      patch_set ps;
      manifests_to_patch_set(empty_manifest, child_map, app, ps);
      patch_set_to_packets(ps, app, qpw);
    }

  // now that we've queued the data, we can note this new child
  // node as existing (well .. soon-to-exist) on the server
  app.db.note_manifest_on_netserver (targ, child_id);

}


// this helper tries to produce merge <- mergeN(left,right), possibly
// merge3 if it can find an ancestor, otherwise merge2. it also queues the
// appropriate edges from known ancestors to the new merge node, to be
// transmitted to each of the targets provided.

static void try_one_merge(manifest_id const & left,
			  manifest_id const & right,
			  manifest_id & merged,
			  app_state & app, 
			  set<url> const & targets)
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
    
    // make sure the appropriate edges get queued for the network.
    for (set<url>::const_iterator targ = targets.begin();
	 targ != targets.end(); ++targ)
      {
	queue_edge_for_target_ancestor (*targ, merged, merged_map, app);
      }
    
    // throw in all available certs for good measure
    queueing_packet_writer qpw(app, targets);
    vector< manifest<cert> > certs;
    app.db.get_manifest_certs(merged, certs);
    for(vector< manifest<cert> >::const_iterator i = certs.begin();
	i != certs.end(); ++i)
      qpw.consume_manifest_cert(*i);
  }

}			  


// actual commands follow


static void ls_certs (string name, app_state & app, vector<string> const & args)
{
  if (args.size() != 2)
    throw usage(name);

  vector<cert> certs;

  transaction_guard guard(app.db);

  if (idx(args, 0) == "manifest")
    {
      manifest_id ident;
      complete(app, idx(args, 1), ident);
      vector< manifest<cert> > ts;
      app.db.get_manifest_certs(ident, ts);
      for (size_t i = 0; i < ts.size(); ++i)
	certs.push_back(idx(ts, i).inner());
    }
  else if (idx(args, 0) == "file")
    {
      file_id ident;
      complete(app, idx(args, 1), ident);
      vector< file<cert> > ts;
      app.db.get_file_certs(ident, ts);
      for (size_t i = 0; i < ts.size(); ++i)
	certs.push_back(idx(ts, i).inner());
    }
  else
    throw usage(name);

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
      bool ok = check_cert(app, idx(certs, i));
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
      string head = string(ok ? "ok sig from " : "bad sig from ")
	+ "[" + idx(certs, i).key() + "] : " 
	+ "[" + idx(certs, i).name() + "] = [";
      string pad(head.size(), ' ');
      vector<string> lines;
      split_into_lines(washed, lines);
      I(lines.size() > 0);
      cout << head << idx(lines, 0) ;
      for (size_t i = 1; i < lines.size(); ++i)
	cout << endl << pad << idx(lines, i);
      cout << "]" << endl;
    }  
  guard.commit();
}

static void ls_keys (string name, app_state & app, vector<string> const & args)
{
  vector<rsa_keypair_id> pubkeys;
  vector<rsa_keypair_id> privkeys;

  transaction_guard guard(app.db);

  if (args.size() == 0)
    app.db.get_key_ids("", pubkeys, privkeys);
  else if (args.size() == 1)
    app.db.get_key_ids(idx(args, 0), pubkeys, privkeys);
  else
    throw usage(name);
  
  if (pubkeys.size() > 0)
    {
      cout << endl << "[public keys]" << endl;
      for (size_t i = 0; i < pubkeys.size(); ++i)
	cout << idx(pubkeys, i)() << endl;
      cout << endl;
    }

  if (privkeys.size() > 0)
    {
      cout << endl << "[private keys]" << endl;
      for (size_t i = 0; i < privkeys.size(); ++i)
	cout << idx(privkeys, i)() << endl;
      cout << endl;
    }

  if (pubkeys.size() == 0 &&
      privkeys.size() == 0)
    P(F("warning: no keys found matching '%s'\n") % idx(args, 0));

  guard.commit();
}

CMD(genkey, "key and cert", "KEYID", "generate an RSA key-pair")
{
  if (args.size() != 1)
    throw usage(name);
  
  transaction_guard guard(app.db);
  rsa_keypair_id ident(idx(args, 0));

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

CMD(cert, "key and cert", "(file|manifest) ID CERTNAME [CERTVAL]",
    "create a cert for a file or manifest")
{
  if ((args.size() != 4) && (args.size() != 3))
    throw usage(name);

  transaction_guard guard(app.db);

  hexenc<id> ident;
  if (idx(args, 0) == "manifest")
    {
      manifest_id mid;
      complete(app, idx(args, 1), mid);
      ident = mid.inner();
    }
  else if (idx(args, 0) == "file")
    {
      file_id fid;
      complete(app, idx(args, 1), fid);
      ident = fid.inner();
    }
  else
    throw usage(this->name);

  cert_name name(idx(args, 2));

  rsa_keypair_id key;
  if (app.signing_key() != "")
    key = app.signing_key;
  else
    N(guess_default_key(key, app),
      F("no unique private key found, and no key specified"));
  
  cert_value val;
  if (args.size() == 4)
    val = cert_value(idx(args, 3));
  else
    val = cert_value(get_stdin());

  base64<cert_value> val_encoded;
  encode_base64(val, val_encoded);

  cert t(ident, name, val_encoded, key);
  
  set<url> targets;
  cert_value branchname;
  guess_branch (manifest_id(ident), app, branchname);
  app.lua.hook_get_post_targets(branchname(), targets);  

  queueing_packet_writer qpw(app, targets);
  packet_db_writer dbw(app);

  // nb: we want to throw usage on mis-use *before* asking for a
  // passphrase.
  
  if (idx(args, 0) == "file")
    {
      calculate_cert(app, t);  
      dbw.consume_file_cert(file<cert>(t));
      qpw.consume_file_cert(file<cert>(t));
    }
  else if (idx(args, 0) == "manifest")
    {
      calculate_cert(app, t);
      dbw.consume_manifest_cert(manifest<cert>(t));
      qpw.consume_manifest_cert(manifest<cert>(t));
    }
  else
    throw usage(this->name);

  guard.commit();
}

CMD(vcheck, "key and cert", "create [MANIFEST]\ncheck [MANIFEST]", 
    "create or check a cryptographic version-check certificate")
{
  if (args.size() < 1 || args.size() > 2)
    throw usage(name);

  set<manifest_id> ids;
  if (args.size() == 1)
    {
      N(app.branch_name != "", F("need --branch argument for branch-based vcheck"));
      get_branch_heads(app.branch_name, app, ids);      
    }
  else
    {
      for (size_t i = 1; i < args.size(); ++i)
	{
	  manifest_id mid;
	  complete(app, idx(args, i), mid);
	  ids.insert(mid);
	}
    }

  if (idx(args, 0) == "create")
    for (set<manifest_id>::const_iterator i = ids.begin();
	 i != ids.end(); ++i)
    {
      packet_db_writer dbw(app);
      cert_manifest_vcheck(*i, app, dbw); 
    }

  else if (idx(args, 0) == "check")
    for (set<manifest_id>::const_iterator i = ids.begin();
	 i != ids.end(); ++i)
    {
      check_manifest_vcheck(*i, app); 
    }

  else 
    throw usage(name);
}


CMD(tag, "certificate", "ID TAGNAME", 
    "put a symbolic tag cert on a manifest version")
{
  if (args.size() != 2)
    throw usage(name);
  manifest_id m;
  complete(app, idx(args, 0), m);
  packet_db_writer dbw(app);

  set<url> targets;
  cert_value branchname;
  guess_branch (m, app, branchname);
  app.lua.hook_get_post_targets(branchname(), targets);  

  queueing_packet_writer qpw(app, targets);
  cert_manifest_tag(m, idx(args, 1), app, dbw);
  cert_manifest_tag(m, idx(args, 1), app, qpw);
}

CMD(approve, "certificate", "(file|manifest) ID", 
    "approve of a manifest or file version")
{
  if (args.size() != 2)
    throw usage(name);

  if (idx(args, 0) == "manifest")
    {
      manifest_id m;
      complete(app, idx(args, 1), m);
      set<url> targets;
      cert_value branchname;
      guess_branch (m, app, branchname);
      app.lua.hook_get_post_targets(branchname(), targets);  
      queueing_packet_writer qpw(app, targets);
      packet_db_writer dbw(app);
      cert_manifest_approval(m, true, app, dbw);
      cert_manifest_approval(m, true, app, qpw);
    }
  else if (idx(args, 0) == "file")
    {
      packet_db_writer dbw(app);
      file_id f;
      complete(app, idx(args, 1), f);
      set<url> targets;
      N(app.branch_name != "", F("need --branch argument for posting"));
      app.lua.hook_get_post_targets(cert_value(app.branch_name), targets); 
      queueing_packet_writer qpw(app, targets);
      cert_file_approval(f, true, app, dbw);
      cert_file_approval(f, true, app, qpw);
    }
  else
    throw usage(name);
}

CMD(disapprove, "certificate", "(file|manifest) ID", 
    "disapprove of a manifest or file version")
{
  if (args.size() != 2)
    throw usage(name);

  if (idx(args, 0) == "manifest")
    {
      manifest_id m;
      complete(app, idx(args, 1), m);
      set<url> targets;
      cert_value branchname;
      guess_branch (m, app, branchname);
      app.lua.hook_get_post_targets(branchname(), targets);  
      queueing_packet_writer qpw(app, targets);
      packet_db_writer dbw(app);
      cert_manifest_approval(m, false, app, dbw);
      cert_manifest_approval(m, false, app, qpw);
    }
  else if (idx(args, 0) == "file")
    {
      file_id f;;
      complete(app, idx(args, 1), f);
      set<url> targets;
      N(app.branch_name != "", F("need --branch argument for posting"));
      app.lua.hook_get_post_targets(cert_value(app.branch_name), targets); 
      queueing_packet_writer qpw(app, targets);
      packet_db_writer dbw(app);
      cert_file_approval(f, false, app, dbw);
      cert_file_approval(f, false, app, qpw);
    }
  else
    throw usage(name);
}


CMD(comment, "certificate", "(file|manifest) ID [COMMENT]",
    "comment on a file or manifest version")
{
  if (args.size() != 2 && args.size() != 3)
    throw usage(name);

  string comment;
  if (args.size() == 3)
    comment = idx(args, 2);
  else
    N(app.lua.hook_edit_comment("", comment), 
      F("edit comment failed"));
  
  N(comment.find_first_not_of(" \r\t\n") != string::npos, 
    F("empty comment"));

  if (idx(args, 0) == "file")
    {
      file_id f;
      complete(app, idx(args, 1), f);
      set<url> targets;
      N(app.branch_name != "", F("need --branch argument for posting"));
      app.lua.hook_get_post_targets(cert_value(app.branch_name), targets); 
      queueing_packet_writer qpw(app, targets);
      packet_db_writer dbw(app);
      cert_file_comment(f, comment, app, dbw); 
      cert_file_comment(f, comment, app, qpw); 
    }
  else if (idx(args, 0) == "manifest")
    {
      manifest_id m;
      complete(app, idx(args, 1), m);
      set<url> targets;
      cert_value branchname;
      guess_branch (m, app, branchname);
      app.lua.hook_get_post_targets(branchname(), targets);  
      queueing_packet_writer qpw(app, targets);
      packet_db_writer dbw(app);
      cert_manifest_comment(m, comment, app, dbw);
      cert_manifest_comment(m, comment, app, qpw);
    }
  else
    throw usage(name);
}


CMD(add, "working copy", "PATHNAME...", "add files to working copy")
{
  if (args.size() < 1)
    throw usage(name);

  manifest_map man;
  work_set work;  
  get_manifest_map(man);
  get_work_set(work);
  bool rewrite_work = false;

  for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_addition(file_path(*i), app, work, man, rewrite_work);
    
  if (rewrite_work)
    put_work_set(work);

  update_any_attrs(app);
  app.write_options();
}

CMD(drop, "working copy", "FILE...", "drop files from working copy")
{
  if (args.size() < 1)
    throw usage(name);

  manifest_map man;
  work_set work;
  get_manifest_map(man);
  get_work_set(work);
  bool rewrite_work = false;

  for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
    build_deletion(file_path(*i), app, work, man, rewrite_work);
  
  if (rewrite_work)
    put_work_set(work);

  update_any_attrs(app);
  app.write_options();
}


CMD(rename, "working copy", "SRC DST", "rename entries in the working copy")
{
  if (args.size() != 2)
    throw usage(name);
  
  manifest_map man;
  work_set work;

  get_manifest_map(man);
  get_work_set(work);
  bool rewrite_work = false;

  build_rename(file_path(idx(args, 0)), file_path(idx(args, 1)), app, work, 
	       man, rewrite_work);
  
  if (rewrite_work)
    put_work_set(work);
  
  update_any_attrs(app);
  app.write_options();  
}


CMD(commit, "working copy", "MESSAGE", "commit working copy to database")
{
  string log_message("");
  manifest_map m_old, m_new;
  patch_set ps;

  rename_edge renames;

  get_manifest_map(m_old);
  calculate_new_manifest_map(m_old, m_new, renames.mapping);
  manifest_id old_id, new_id;
  calculate_ident(m_old, old_id);
  calculate_ident(m_new, new_id);
  renames.parent = old_id;
  renames.child = new_id;

  if (args.size() != 0 && args.size() != 1)
    throw usage(name);

  cert_value branchname;
  guess_branch (old_id, app, branchname);
    
  P(F("committing %s to branch %s\n") % new_id % branchname);
  app.set_branch(branchname());

  manifests_to_patch_set(m_old, m_new, renames, app, ps);

  // get log message
  if (args.size() == 1)
    log_message = idx(args, 0);
  else
    get_log_message(ps, app, log_message);

  N(log_message.find_first_not_of(" \r\t\n") != string::npos,
    F("empty log message"));

  {
    transaction_guard guard(app.db);

    // process manifest delta or new manifest
    if (app.db.manifest_version_exists(ps.m_new))
      {
	L(F("skipping manifest %s, already in database\n") % ps.m_new);
      }
    else
      {
	if (app.db.manifest_version_exists(ps.m_old))
	  {
	    L(F("inserting manifest delta %s -> %s\n") % ps.m_old % ps.m_new);
	    manifest_data m_old_data, m_new_data;
	    base64< gzip<delta> > del;
	    diff(m_old, m_new, del);
	    app.db.put_manifest_version(ps.m_old, ps.m_new, manifest_delta(del));
	  }
	else
	  {
	    L(F("inserting full manifest %s\n") % ps.m_new);
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
	    L(F("skipping file delta %s, already in database\n") % i->id_new);
	  }
	else
	  {
	    if (app.db.file_version_exists(i->id_old))
	      {
		L(F("inserting delta %s -> %s\n") % i->id_old % i->id_new);
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
		L(F("inserting full version %s\n") % i->id_old);
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
	    L(F("skipping file %s %s, already in database\n") 
	      % i->path % i->ident);
	  }
	else
	  {
	    // it's a new file
	    L(F("inserting new file %s %s\n") % i->path % i->ident);
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
    
    if (! (ps.f_moves.empty() && renames.mapping.empty()))
      {
	for (set<patch_move>::const_iterator mv = ps.f_moves.begin();
	     mv != ps.f_moves.end(); ++mv)
	  {
	    rename_set::const_iterator rn = renames.mapping.find(mv->path_old);
	    if (rn != renames.mapping.end())
	      I(rn->second == mv->path_new);
	    else
	      renames.mapping.insert(make_pair(mv->path_old, mv->path_new));
	  }
	renames.parent = ps.m_old;
	renames.child = ps.m_new;
	cert_manifest_rename(ps.m_new, renames, app, dbw);
      }    

    // commit done, now queue diff for sending

    if (app.db.manifest_version_exists(ps.m_new))
      {
	set<url> targets;
	app.lua.hook_get_post_targets(branchname, targets);
	
	// make sure the appropriate edges get queued for the network.
	for (set<url>::const_iterator targ = targets.begin();
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
  remove_work_set();
  put_manifest_map(m_new);
  P(F("committed %s\n") % ps.m_new);

  update_any_attrs(app);
  app.write_options();
}

CMD(update, "working copy", "[SORT-KEY]...", "update working copy, relative to sorting keys")
{

  manifest_data m_chosen_data;
  manifest_map m_old, m_working, m_chosen, m_new;
  manifest_id m_old_id, m_chosen_id;

  transaction_guard guard(app.db);

  get_manifest_map(m_old);
  calculate_ident(m_old, m_old_id);
  calculate_new_manifest_map(m_old, m_working);
  
  pick_update_target(m_old_id, args, app, m_chosen_id);
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
      write_data(i->path, tmp.inner());
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

CMD(revert, "working copy", "[FILE]...", "revert file(s) or entire working copy")
{
  manifest_map m_old;

  if (args.size() == 0)
    {
      // revert the whole thing
      get_manifest_map(m_old);
      for (manifest_map::const_iterator i = m_old.begin(); i != m_old.end(); ++i)
	{
	  path_id_pair pip(*i);

	  N(app.db.file_version_exists(pip.ident()),
	    F("no file version %s found in database for %s")
	    % pip.ident() % pip.path());
      
	  file_data dat;
	  L(F("writing file %s to %s\n") %
	    pip.ident() % pip.path());
	  app.db.get_file_version(pip.ident(), dat);
	  write_data(pip.path(), dat.inner());
	}
      remove_work_set();
    }
  else
    {
      work_set work;
      get_manifest_map(m_old);
      get_work_set(work);

      // revert some specific files
      vector<string> work_args (args.begin(), args.end());
      for (size_t i = 0; i < work_args.size(); ++i)
	{
	  if (directory_exists(idx(work_args, i)))
	    {
	      // simplest is to just add all files from that
	      // directory.
	      string dir = idx(work_args, i);
	      int off = idx(work_args, i).find_last_not_of('/');
	      if (off != -1)
		dir = idx(work_args, i).substr(0, off + 1);
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

	  N((m_old.find(idx(work_args, i)) != m_old.end()) ||
	    (work.adds.find(idx(work_args, i)) != work.adds.end()) ||
	    (work.dels.find(idx(work_args, i)) != work.dels.end()) ||
	    (work.renames.find(idx(work_args, i)) != work.renames.end()),
	    F("nothing known about %s") % idx(work_args, i));

	  if (m_old.find(idx(work_args, i)) != m_old.end())
	    {
	      path_id_pair pip(m_old.find(idx(work_args, i)));
	      L(F("reverting %s to %s\n") %
		pip.path() % pip.ident());

	      N(app.db.file_version_exists(pip.ident()),
		F("no file version %s found in database for %s")
		% pip.ident() % pip.path());
	      
	      file_data dat;
	      L(F("writing file %s to %s\n") %
		pip.ident() % pip.path());
	      app.db.get_file_version(pip.ident(), dat);
	      write_data(pip.path(), dat.inner());	      

	      // a deleted file will always appear in the manifest
	      if (work.dels.find(idx(work_args, i)) != work.dels.end())
		{
		  L(F("also removing deletion for %s\n") %
		    idx(work_args, i));
		  work.dels.erase(idx(work_args, i));
		}
	    }
	  else if (work.renames.find(idx(work_args, i)) != work.renames.end())
	    {
	      L(F("removing rename for %s\n") % idx(work_args, i));
	      work.renames.erase(idx(work_args, i));
	    }
	  else
	    {
	      I (work.adds.find(idx(work_args, i)) != work.adds.end());
	      L(F("removing addition for %s\n") % idx(work_args, i));
	      work.adds.erase(idx(work_args, i));
	    }
	}
      // race
      put_work_set(work);
    }

  update_any_attrs(app);
  app.write_options();
}


CMD(cat, "tree", "(file|manifest) ID", "write file or manifest from database to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);

  if (idx(args, 0) == "file")
    {
      file_data dat;
      file_id ident;
      complete(app, idx(args, 1), ident);

      N(app.db.file_version_exists(ident),
	F("no file version %s found in database") % ident);

      L(F("dumping file %s\n") % ident);
      app.db.get_file_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());

    }
  else if (idx(args, 0) == "manifest")
    {
      manifest_data dat;
      manifest_id ident;
      complete(app, idx(args, 1), ident);

      N(app.db.manifest_version_exists(ident),
	F("no manifest version %s found in database") % ident);

      L(F("dumping manifest %s\n") % ident);
      app.db.get_manifest_version(ident, dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      cout.write(unpacked().data(), unpacked().size());
    }
  else 
    throw usage(name);

  guard.commit();
}


CMD(checkout, "tree", "MANIFEST-ID DIRECTORY\nDIRECTORY", "check out tree state from database into directory")
{

  manifest_id ident;
  string dir;

  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  if (args.size() == 1)
    {
      set<manifest_id> heads;
      N(app.branch_name != "", F("need --branch argument for branch-based checkout"));
      get_branch_heads(app.branch_name, app, heads);
      N(heads.size() == 1, F("branch %s has multiple heads") % app.branch_name);
      ident = *(heads.begin());
      dir = idx(args, 0);
    }

  else
    {
      complete(app, idx(args, 0), ident);
      dir = idx(args, 1);
    }

  if (dir != string("."))
    {
      local_path lp(dir);
      mkdir_p(lp);
      chdir(dir.c_str());
    }

  transaction_guard guard(app.db);
    
  file_data data;
  manifest_map m;

  N(app.db.manifest_version_exists(ident),
    F("no manifest version %s found in database") % ident);
  
  L(F("checking out manifest %s to directory %s\n") % ident % dir);
  manifest_data m_data;
  app.db.get_manifest_version(ident, m_data);
  read_manifest_map(m_data, m);      
  put_manifest_map(m);
  
  for (manifest_map::const_iterator i = m.begin(); i != m.end(); ++i)
    {
      path_id_pair pip(*i);
      
      N(app.db.file_version_exists(pip.ident()),
	F("no file version %s found in database for %s")
	% pip.ident() % pip.path());
      
      file_data dat;
      L(F("writing file %s to %s\n") %
	pip.ident() % pip.path());
      app.db.get_file_version(pip.ident(), dat);
      write_data(pip.path(), dat.inner());
    }
  remove_work_set();
  guard.commit();
  update_any_attrs(app);
  app.write_options();
}

ALIAS(co, checkout, "tree", "MANIFEST-ID DIRECTORY\nDIRECTORY",
      "check out tree state from database; alias for checkout")

CMD(heads, "tree", "", "show unmerged heads of branch")
{
  set<manifest_id> heads;
  if (args.size() != 0)
    throw usage(name);

  if (app.branch_name == "")
    {
      cout << "please specify a branch, with --branch=BRANCH" << endl;
      return;
    }

  get_branch_heads(app.branch_name, app, heads);

  if (heads.size() == 0)
    cout << "branch '" << app.branch_name << "' is empty" << endl;
  else if (heads.size() == 1)
    cout << "branch '" << app.branch_name << "' is currently merged:" << endl;
  else
    cout << "branch '" << app.branch_name << "' is currently unmerged:" << endl;

  for (set<manifest_id>::const_iterator i = heads.begin(); 
       i != heads.end(); ++i)
    {
      cout << i->inner()() << endl;
    }
}


CMD(merge, "tree", "", "merge unmerged heads of branch")
{
  set<manifest_id> heads;

  if (args.size() != 0)
    throw usage(name);

  if (app.branch_name == "")
    {
      cout << "please specify a branch, with --branch=BRANCH" << endl;
      return;
    }

  get_branch_heads(app.branch_name, app, heads);

  if (heads.size() == 0)
    {
      cout << "branch '" << app.branch_name << "' is empty" << endl;
      return;
    }
  else if (heads.size() == 1)
    {
      cout << "branch '" << app.branch_name << "' is merged" << endl;
      return;
    }
  else
    {
      set<url> targets;
      app.lua.hook_get_post_targets(app.branch_name, targets);

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
	  try_one_merge (left, right, merged, app, targets);
	  	  
	  // merged 1 edge; now we commit this, update merge source and
	  // try next one

	  packet_db_writer dbw(app);
	  queueing_packet_writer qpw(app, targets);
	  cert_manifest_in_branch(merged, app.branch_name, app, dbw);
	  cert_manifest_in_branch(merged, app.branch_name, app, qpw);

	  string log = (F("merge of %s and %s\n") % left % right).str();
	  cert_manifest_changelog(merged, log, app, dbw);
	  cert_manifest_changelog(merged, log, app, qpw);
	  
	  guard.commit();
	  P(F("[source] %s") % left);
	  P(F("[source] %s") % right);
	  P(F("[merged] %s") % merged);
	  left = merged;
	}
    }

  app.write_options();
}


// float and fmerge are simple commands for debugging the line merger.
// most of the time, leave them commented out. they can be helpful for certain
// cases, though.

/*
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

  file_id anc_id(idx(args, 0)), left_id(idx(args, 1)), right_id(idx(args, 2));
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
*/

CMD(propagate, "tree", "SOURCE-BRANCH DEST-BRANCH", 
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

  set<manifest_id> src_heads, dst_heads;

  if (args.size() != 2)
    throw usage(name);

  get_branch_heads(idx(args, 0), app, src_heads);
  get_branch_heads(idx(args, 1), app, dst_heads);

  if (src_heads.size() == 0)
    {
      P(F("branch '%s' is empty\n") % idx(args, 0));
      return;
    }
  else if (src_heads.size() != 1)
    {
      P(F("branch '%s' is not merged\n") % idx(args, 0));
      return;
    }
  else if (dst_heads.size() == 0)
    {
      P(F("branch '%s' is empty\n") % idx(args, 1));
      return;
    }
  else if (dst_heads.size() != 1)
    {
      P(F("branch '%s' is not merged\n") % idx(args, 1));
      return;
    }
  else
    {
      set<url> targets;
      app.lua.hook_get_post_targets(idx(args, 1), targets);

      set<manifest_id>::const_iterator src_i = src_heads.begin();
      set<manifest_id>::const_iterator dst_i = dst_heads.begin();

      manifest_id merged;
      transaction_guard guard(app.db);
      try_one_merge (*src_i, *dst_i, merged, app, targets);      

      queueing_packet_writer qpw(app, targets);
      cert_manifest_in_branch(merged, app.branch_name, app, qpw);
      cert_manifest_changelog(merged, 
			      (F("propagate of %s and %s from branch '%s' to '%s'\n")
			       % (*src_i) % (*dst_i) % idx(args,0) % idx(args,1)).str(), 
			      app, qpw);
      guard.commit();      
    }
}


CMD(complete, "informative", "(manifest|file) PARTIAL-ID", "complete partial id")
{
  if (args.size() != 2)
    throw usage(name);

  if (idx(args, 0) == "manifest")
    {      
      N(idx(args, 1).find_first_not_of("abcdef0123456789") == string::npos,
	F("non-hex digits in partial id"));
      set<manifest_id> completions;
      app.db.complete(idx(args, 1), completions);
      for (set<manifest_id>::const_iterator i = completions.begin();
	   i != completions.end(); ++i)
	cout << i->inner()() << endl;
    }
  else if (idx(args, 0) == "file")
    {
      N(idx(args, 1).find_first_not_of("abcdef0123456789") == string::npos,
	F("non-hex digits in partial id"));
      set<file_id> completions;
      app.db.complete(idx(args, 1), completions);
      for (set<file_id>::const_iterator i = completions.begin();
	   i != completions.end(); ++i)
	cout << i->inner()() << endl;
    }
  else
    throw usage(name);  
}

CMD(diff, "informative", "[MANIFEST-ID [MANIFEST-ID]]", "show current diffs on stdout")
{
  manifest_map m_old, m_new;
  patch_set ps;
  bool new_is_archived;

  transaction_guard guard(app.db);

  if (args.size() == 0)
    {
      get_manifest_map(m_old);
      calculate_new_manifest_map(m_old, m_new);
      new_is_archived = false;
    }
  else if (args.size() == 1)
    {
      manifest_id m_old_id;
      complete(app, idx(args, 0), m_old_id);
      manifest_data m_old_data;
      app.db.get_manifest_version(m_old_id, m_old_data);
      read_manifest_map(m_old_data, m_old);

      manifest_map parent;
      get_manifest_map(parent);
      calculate_new_manifest_map(parent, m_new);
      new_is_archived = false;
    }
  else if (args.size() == 2)
    {
      manifest_id m_old_id, m_new_id;

      complete(app, idx(args, 0), m_old_id);
      complete(app, idx(args, 1), m_new_id);

      manifest_data m_old_data, m_new_data;
      app.db.get_manifest_version(m_old_id, m_old_data);
      app.db.get_manifest_version(m_new_id, m_new_data);

      read_manifest_map(m_old_data, m_old);
      read_manifest_map(m_new_data, m_new);
      new_is_archived = true;
    }
  else
    {
      throw usage(name);
    }
      
  manifests_to_patch_set(m_old, m_new, app, ps);

  stringstream summary;
  patch_set_to_text_summary(ps, summary);
  vector<string> lines;
  split_into_lines(summary.str(), lines);
  for (vector<string>::iterator i = lines.begin(); i != lines.end(); ++i)
    cout << "# " << *i << endl;

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

      if (new_is_archived)
        {
          file_data f_new;
          gzip<data> decoded_new;
          app.db.get_file_version(i->id_new, f_new);
          decode_base64(f_new.inner(), decoded_new);
          decode_gzip(decoded_new, decompressed_new);
        }
      else
        {
          read_data(i->path, decompressed_new);
        }

      split_into_lines(decompressed_old(), old_lines);
      split_into_lines(decompressed_new(), new_lines);

      unidiff(i->path(), i->path(), old_lines, new_lines, cout);
    }  
  guard.commit();
}

CMD(log, "informative", "[ID]", "print log history in reverse order")
{
  manifest_map m;
  manifest_id m_id;
  set<manifest_id> frontier, cycles;

  if (args.size() > 1)
    throw usage(name);
  
  if (args.size() == 1)
    {
      complete(app, idx(args, 0), m_id);
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

  set<file_id> no_comments;

  while(! frontier.empty())
    {
      set<manifest_id> next_frontier;
      for (set<manifest_id>::const_iterator i = frontier.begin();
	   i != frontier.end(); ++i)
	{
	  cout << "-----------------------------------------------------------------"
	       << endl;
	  cout << "Version: " << *i << endl;

	  cout << "Author:";
	  vector< manifest<cert> > tmp;
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

	  // pull any file-specific comments
	  if (app.db.manifest_version_exists(*i))
	    {
	      manifest_data mdata;
	      manifest_map mtmp;
	      app.db.get_manifest_version(*i, mdata);
	      read_manifest_map(mdata, mtmp);
	      bool wrote_headline = false;
	      for (manifest_map::const_iterator mi = mtmp.begin();
		   mi != mtmp.end(); ++mi)
		{
		  path_id_pair pip(mi);
		  if (no_comments.find(pip.ident()) != no_comments.end())
		    continue;
		  
		  vector< file<cert> > ftmp;
		  app.db.get_file_certs(pip.ident(), comment_name, ftmp);
		  erase_bogus_certs(ftmp, app);
		  if (!ftmp.empty())
		    {
		      if (!wrote_headline)
			{
			  cout << "File Comments:" << endl << endl;
			  wrote_headline = true;
			}
		      
		      cout << "  " << pip.path() << endl;
		      for (vector< file<cert> >::const_iterator j = ftmp.begin();
			   j != ftmp.end(); ++j)
			{
			  cert_value tv;
			  decode_base64(j->inner().value, tv);
			  cout << "    " << j->inner().key << ": " << tv << endl;
			}	  
		    }
		  else
		    no_comments.insert(pip.ident());
		}
	      if (wrote_headline)
		cout << endl;
	    }
	  
	  app.db.get_manifest_certs(*i, ancestor_name, tmp);
	  erase_bogus_certs(tmp, app);
	  for (vector< manifest<cert> >::const_iterator j = tmp.begin();
	       j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id id(tv());
	      if (cycles.find(id) == cycles.end())
		{
		  next_frontier.insert(id);
		  cycles.insert(id);
		}
	    }
	}
      frontier = next_frontier;
    }
}

CMD(status, "informative", "", "show status of working copy")
{
  manifest_map m_old, m_new;
  manifest_id old_id, new_id;
  patch_set ps1;
  rename_edge renames;

  N(bookdir_exists(),
    F("no monotone book-keeping directory '%s' found") 
    % book_keeping_dir);

  transaction_guard guard(app.db);
  get_manifest_map(m_old);
  calculate_ident(m_old, old_id);
  calculate_new_manifest_map(m_old, m_new, renames.mapping);
  calculate_ident(m_new, new_id);

  renames.parent = old_id;
  renames.child = new_id;
  manifests_to_patch_set(m_old, m_new, renames, app, ps1);
  patch_set_to_text_summary(ps1, cout);

  guard.commit();
}

static void ls_branches (string name, app_state & app, vector<string> const & args)
{
  transaction_guard guard(app.db);
  vector< manifest<cert> > certs;
  app.db.get_manifest_certs(branch_cert_name, certs);

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


static void ls_unknown (app_state & app, bool want_ignored)
{
  manifest_map m_old, m_new;
  get_manifest_map(m_old);
  calculate_new_manifest_map(m_old, m_new);
  unknown_itemizer u(app, m_new, want_ignored);
  walk_tree(u);
}

static void ls_queue (string name, app_state & app)
{
  set<url> target_set;
  app.db.get_queued_targets(target_set);
  vector<url> targets;
  copy(target_set.begin(), target_set.end(), back_inserter(targets));

  for (size_t i = 0; i < targets.size(); ++i)
    {
      size_t queue_count;
      string content;
      cout << "target " << i << ": " 
	   << idx(targets, i) << endl;
      app.db.get_queue_count(idx(targets, i), queue_count);
      for (size_t j = 0; j < queue_count; ++j)
	{
	  app.db.get_queued_content(idx(targets, i), j, content);
	  cout << "    target " << i << ", packet " << j 
	       << ": " << content.size() << " bytes" << endl;
	}      
    }
}


CMD(queue, "network", "list\nprint TARGET PACKET\ndelete TARGET PACKET\nadd URL\naddtree URL [ID...]",
    "list, print, delete, or add items to network queue")
{
  if (args.size() == 0)
    throw usage(name);

  if (idx(args, 0) == "list")
    ls_queue(name, app);

  else if (idx(args, 0) == "print" 
	   || idx(args, 0) == "delete")
    {
      if (args.size() != 3)
	throw usage(name);
      size_t target = boost::lexical_cast<size_t>(idx(args,1));
      size_t packet = boost::lexical_cast<size_t>(idx(args,2));

      set<url> target_set;      
      app.db.get_queued_targets(target_set);
      vector<url> targets;
      copy(target_set.begin(), target_set.end(), back_inserter(targets));
      N(target < targets.size(),
	F("target number %d out of range") % target);

      size_t queue_count;
      app.db.get_queue_count(idx(targets, target), queue_count);

      N(packet < queue_count,
	F("packet number %d out of range for target %d")
	% packet % target);
      
      string content;
      app.db.get_queued_content(idx(targets, target), packet, content);
      
      if (idx(args, 0) == "print")
	{
	  cout << content;
	}
      else
	{
	  ui.inform(F("deleting %d byte posting for %s\n") 
		    % content.size() % idx(targets, target));
	  app.db.delete_posting(idx(targets, target), packet);
	}
    }

  else if (idx(args, 0) == "add")
    {
      if (args.size() != 2)
	throw usage(name);
      url u(idx(args,1));
      string s = get_stdin();
      ui.inform(F("queueing %d bytes for %s\n") % s.size() % u);
      app.db.queue_posting(u, s);
    }  

  else if (idx(args, 0) == "addtree")
    {
      if (args.size() < 2)
	throw usage(name);

      url u(idx(args,1));
      set<manifest_id> roots;

      if (args.size() == 2)
	{
	  N(app.branch_name != "", F("need --branch argument for addtree"));
	  get_branch_heads(app.branch_name, app, roots);
	}
      else
	{
	  for (size_t i = 2; i < args.size(); ++i)
	    {
	      roots.insert(manifest_id(idx(args,i)));
	    }
	}

      set<url> targets;
      targets.insert (u);
      queueing_packet_writer qpw(app, targets);

      transaction_guard guard(app.db);

      for (set<manifest_id>::const_iterator i = roots.begin();
	   i != roots.end(); ++i)
	{
	  set<manifest_id> ancs;
	  find_oldest_ancestors (*i, ancs, app);
	  for (set<manifest_id>::const_iterator j = ancs.begin();
	       j != ancs.end(); ++j)
	    {
	      manifest_map empty, mm;
	      manifest_data dat;
	      patch_set ps;
  
	      // queue the ancestral state
	      app.db.get_manifest_version(*j, dat);
	      read_manifest_map(dat, mm);
	      manifests_to_patch_set(empty, mm, app, ps);
	      patch_set_to_packets(ps, app, qpw);
	      
	      // queue everything between here and there
	      reverse_queue rq(app.db);
	      qpw.rev.reset(rq);
	      write_ancestry_paths(*j, *i, app, qpw);
	      qpw.rev.reset();
	    }
	}
      guard.commit();
    }
  else 
    throw usage(name);
}

CMD(list, "informative", 
    "certs (file|manifest) ID\n"
    "keys [PATTERN]\n"
    "queue\n"
    "branches\n"
    "unknown\n"
    "ignored", 
    "show certs, keys, branches, unknown or intentionally ignored files")
{
  if (args.size() == 0)
    throw usage(name);

  vector<string>::const_iterator i = args.begin();
  ++i;
  vector<string> removed (i, args.end());
  if (idx(args, 0) == "certs")
    ls_certs(name, app, removed);
  else if (idx(args, 0) == "keys")
    ls_keys(name, app, removed);
  else if (idx(args, 0) == "queue")
    ls_queue(name, app);
  else if (idx(args, 0) == "branches")
    ls_branches(name, app, removed);
  else if (idx(args, 0) == "unknown")
    ls_unknown(app, false);
  else if (idx(args, 0) == "ignored")
    ls_unknown(app, true);
  else
    throw usage(name);
}

ALIAS(ls, list, "informative",  
      "certs (file|manifest) ID\n"
      "keys [PATTERN]\n"
      "branches\n"
      "unknown\n"
      "ignored", "show certs, keys, or branches")


  CMD(mdelta, "packet i/o", "OLDID NEWID", "write manifest delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_old_id, m_new_id; 
  manifest_data m_old_data, m_new_data;
  manifest_map m_old, m_new;
  patch_set ps;      

  complete(app, idx(args, 0), m_old_id);
  complete(app, idx(args, 1), m_new_id);

  app.db.get_manifest_version(m_old_id, m_old_data);
  app.db.get_manifest_version(m_new_id, m_new_data);
  read_manifest_map(m_old_data, m_old);
  read_manifest_map(m_new_data, m_new);
  manifests_to_patch_set(m_old, m_new, app, ps);
  patch_set_to_packets(ps, app, pw);  
  guard.commit();
}

CMD(fdelta, "packet i/o", "OLDID NEWID", "write file delta packet to stdout")
{
  if (args.size() != 2)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_old_id, f_new_id;
  file_data f_old_data, f_new_data;

  complete(app, idx(args, 0), f_old_id);
  complete(app, idx(args, 1), f_new_id);

  app.db.get_file_version(f_old_id, f_old_data);
  app.db.get_file_version(f_new_id, f_new_data);
  base64< gzip<delta> > del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));  
  guard.commit();
}

CMD(mdata, "packet i/o", "ID", "write manifest data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_id;
  manifest_data m_data;

  complete(app, idx(args, 0), m_id);

  app.db.get_manifest_version(m_id, m_data);
  pw.consume_manifest_data(m_id, m_data);  
  guard.commit();
}


CMD(fdata, "packet i/o", "ID", "write file data packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_id;
  file_data f_data;

  complete(app, idx(args, 0), f_id);

  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);  
  guard.commit();
}

CMD(mcerts, "packet i/o", "ID", "write manifest cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  manifest_id m_id;
  vector< manifest<cert> > certs;

  complete(app, idx(args, 0), m_id);

  app.db.get_manifest_certs(m_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_manifest_cert(idx(certs, i));
  guard.commit();
}

CMD(fcerts, "packet i/o", "ID", "write file cert packets to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);

  file_id f_id;
  vector< file<cert> > certs;

  complete(app, idx(args, 0), f_id);

  app.db.get_file_certs(f_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_file_cert(idx(certs, i));
  guard.commit();
}

CMD(pubkey, "packet i/o", "ID", "write public key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);
  rsa_keypair_id ident(idx(args, 0));
  base64< rsa_pub_key > key;
  app.db.get_key(ident, key);
  pw.consume_public_key(ident, key);
  guard.commit();
}

CMD(privkey, "packet i/o", "ID", "write private key packet to stdout")
{
  if (args.size() != 1)
    throw usage(name);

  transaction_guard guard(app.db);
  packet_writer pw(cout);
  rsa_keypair_id ident(idx(args, 0));
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
  N(count != 0, F("no packets found on stdin"));
  if (count == 1)
    P(F("read 1 packet\n"));
  else
    P(F("read %d packets\n") % count);
  guard.commit();
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

CMD(fetch, "network", "[URL]", "fetch recent changes from network")
{
  if (args.size() > 1)
    throw usage(name);

  set<url> sources;

  if (args.size() == 0)
    {
      if (! app.lua.hook_get_fetch_sources(app.branch_name, sources))
	{
	  P(F("fetching from all known URLs\n"));
	  app.db.get_all_known_sources(sources);
	}
    }
  else
    {
      sources.insert(url(idx(args, 0)));
    }
  
  fetch_queued_blobs_from_network(sources, app);
}

CMD(post, "network", "[URL]", "post queued changes to network")
{
  if (args.size() > 1)
    throw usage(name);

  set<url> targets;
  if (args.size() == 0)
    {
      P(F("no URL provided, posting all queued targets\n"));
      app.db.get_queued_targets(targets);
    }  
  else
    {
      targets.insert(url(idx(args, 0)));
    }

  post_queued_blobs_to_network(targets, app);
}


CMD(rcs_import, "rcs", "RCSFILE...", "import all versions in RCS files")
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


CMD(cvs_import, "rcs", "CVSROOT", "import all versions in CVS repository")
{
  if (args.size() != 1)
    throw usage(name);

  import_cvs_repo(fs::path(args.at(0)), app);
}

CMD(debug, "debug", "SQL", "issue SQL queries directly (dangerous)")
{
  if (args.size() != 1)
    throw usage(name);
  app.db.debug(idx(args, 0), cout);
}

CMD(db, "database", "init\ninfo\nversion\ndump\nload\nmigrate", "manipulate database state")
{
  if (args.size() != 1)
    throw usage(name);
  if (idx(args, 0) == "init")
    app.db.initialize();
  else if (idx(args, 0) == "info")
    app.db.info(cout);
  else if (idx(args, 0) == "version")
    app.db.version(cout);
  else if (idx(args, 0) == "dump")
    app.db.dump(cout);
  else if (idx(args, 0) == "load")
    app.db.load(cin);
  else if (idx(args, 0) == "migrate")
    app.db.migrate();
  else
    throw usage(name);
}


}; // namespace commands
