// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "cycle_detector.hh"
#include "database.hh"
#include "file_io.hh"
#include "interner.hh"
#include "manifest.hh"
#include "packet.hh"
#include "rcs_file.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

using namespace std;
using boost::shared_ptr;
using boost::scoped_ptr;

// cvs history recording stuff

typedef unsigned long cvs_branchname;
typedef unsigned long cvs_author;
typedef unsigned long cvs_changelog;
typedef unsigned long cvs_version;
typedef unsigned long cvs_path;

struct cvs_history;

struct 
cvs_key
{
  cvs_key() {}
  cvs_key(rcs_file const & r, 
	  string const & version, 
	  cvs_history & cvs);

  inline bool similar_enough(cvs_key const & other) const
  {
    if (changelog != other.changelog)
      return false;
    if (author != other.author)
      return false;
    if (labs(time - other.time) > constants::cvs_window)
      return false;
    return true;
  }

  inline bool operator<(cvs_key const & other) const
  {
    // nb: this must sort as > to construct the edges in the right direction
    return time > other.time ||

      (time == other.time 
       && author > other.author) ||

      (time == other.time 
       && author == other.author 
       && changelog > other.changelog) ||

      (time == other.time 
       && author == other.author 
       && changelog == other.changelog
       && branch > other.branch);
  }

  cvs_branchname branch;
  cvs_changelog changelog;
  cvs_author author;
  time_t time;
};

struct 
cvs_file_edge
{
  cvs_file_edge (file_id const & pv, 
		 file_path const & pp,
		 bool pl,
		 file_id const & cv, 
		 file_path const & cp,
		 bool cl,
		 cvs_history & cvs);
  cvs_version parent_version;
  cvs_path parent_path;
  bool parent_live_p;
  cvs_version child_version;
  cvs_path child_path;
  bool child_live_p;
  inline bool operator<(cvs_file_edge const & other) const
  {
#if 0
    return (parent_path < other.parent_path) 
                       || ((parent_path == other.parent_path) 
       && ((parent_version < other.parent_version) 
                       || ((parent_version == other.parent_version) 
       && ((parent_live_p < other.parent_live_p) 
                       || ((parent_live_p == other.parent_live_p) 
       && ((child_path < other.child_path) 
                       || ((child_path == other.child_path) 
       && ((child_version < other.child_version) 
                       || ((child_version == other.child_version) 
       && (child_live_p < other.child_live_p) )))))))));
#else
    return (parent_path < other.parent_path) 
    			|| ((parent_path == other.parent_path) 
    	&& ((parent_version < other.parent_version) 
    			|| ((parent_version == other.parent_version) 
    	&& ((parent_live_p < other.parent_live_p) 
    			|| ((parent_live_p == other.parent_live_p) 
    	&& ((child_path < other.child_path) 
    			|| ((child_path == other.child_path) 
    	&& ((child_version < other.child_version) 
    			|| ((child_version == other.child_version) 
    	&& (child_live_p < other.child_live_p) )))))))));
#endif
  }
};

struct 
cvs_state
{
  set<cvs_file_edge> in_edges;
  map< cvs_key, shared_ptr<cvs_state> > substates;
};

struct 
cvs_history
{

  interner<unsigned long> branch_interner;
  interner<unsigned long> author_interner;
  interner<unsigned long> changelog_interner;
  interner<unsigned long> file_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> manifest_version_interner;

  cycle_detector<unsigned long> manifest_cycle_detector;

  bool find_key_and_state(rcs_file const & r, 
			  string const & version,
			  cvs_key & key,
			  shared_ptr<cvs_state> & state);

  typedef stack< shared_ptr<cvs_state> > state_stack;

  map<unsigned long, 
      pair<cvs_key, 
	   shared_ptr<cvs_state> > > branchpoints;

  state_stack stk;
  file_path curr_file;
  manifest_map head_manifest;
  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
		    file_id const & ident);
  void push_branch(rcs_file const & r, 
		   string const & branchpoint_version,
		   string const & first_branch_version);
  void note_file_edge(rcs_file const & r, 
		      string const & prev_rcs_version_num,
		      string const & next_rcs_version_num,
		      file_id const & prev_version,
		      file_id const & next_version);
  void find_branchpoint(rcs_file const & r,
			string const & branchpoint_version,
			string const & first_branch_version,
			shared_ptr<cvs_state> & branchpoint);
  void pop_branch();
};


// piece table stuff

struct piece;

struct 
piece_store
{
  vector< boost::shared_ptr<rcs_deltatext> > texts;
  void index_deltatext(boost::shared_ptr<rcs_deltatext> const & dt,
		       vector<piece> & pieces);
  void build_string(vector<piece> const & pieces,
		    string & out);
  void reset() { texts.clear(); }
};

// FIXME: kludge, I was lazy and did not make this
// a properly scoped variable. 

static piece_store global_pieces;


struct 
piece
{
  piece(string::size_type p, string::size_type l, unsigned long id) :
    pos(p), len(l), string_id(id) {}
  string::size_type pos;
  string::size_type len;
  unsigned long string_id;
  string operator*() const
  {
    return string(global_pieces.texts.at(string_id)->text.data() + pos, len);
  }
};


void 
piece_store::build_string(vector<piece> const & pieces,
			  string & out)
{
  out.clear();
  out.reserve(pieces.size() * 60);
  for(vector<piece>::const_iterator i = pieces.begin();
      i != pieces.end(); ++i)
    out.append(texts.at(i->string_id)->text, i->pos, i->len);
}

void 
piece_store::index_deltatext(boost::shared_ptr<rcs_deltatext> const & dt,
			     vector<piece> & pieces)
{
  pieces.clear();
  pieces.reserve(dt->text.size() / 30);  
  texts.push_back(dt);
  unsigned long id = texts.size() - 1;
  string::size_type begin = 0;
  string::size_type end = dt->text.find('\n');
  while(end != string::npos)
    {
      // nb: the piece includes the '\n'
      pieces.push_back(piece(begin, (end - begin) + 1, id));
      begin = end + 1;
      end = dt->text.find('\n', begin);
    }
  if (begin != dt->text.size())
    {
      // the text didn't end with '\n', so neither does the piece
      end = dt->text.size();
      pieces.push_back(piece(begin, end - begin, id));
    }
}


void 
process_one_hunk(vector< piece > const & source,
		 vector< piece > & dest,
		 vector< piece >::const_iterator & i,
		 int & cursor)
{
  string directive = **i;
  assert(directive.size() > 1);
  ++i;

  char code;
  int pos, len;
  sscanf(directive.c_str(), " %c %d %d", &code, &pos, &len);

  try 
    {
      if (code == 'a')
	{
	  // 'ax y' means "copy from source to dest until cursor == x, then
	  // copy y lines from delta, leaving cursor where it is"
	  while (cursor < pos)
	    dest.push_back(source.at(cursor++));
	  I(cursor == pos);
	  while (len--)
	    dest.push_back(*i++);
	}
      else if (code == 'd')
	{      
	  // 'dx y' means "copy from source to dest until cursor == x-1,
	  // then increment cursor by y, ignoring those y lines"
	  while (cursor < (pos - 1))
	    dest.push_back(source.at(cursor++));
	  I(cursor == pos - 1);
	  cursor += len;
	}
      else 
	throw oops("unknown directive '" + directive + "'");
    } 
  catch (std::out_of_range & oor)
    {
      throw oops("std::out_of_range while processing " + directive 
		 + " with source.size() == " 
		 + boost::lexical_cast<string>(source.size())
		 + " and cursor == "
		 + boost::lexical_cast<string>(cursor));
    }  
}

static void
construct_version(vector< piece > const & source_lines,
		  string const & dest_version, 
		  vector< piece > & dest_lines,
		  rcs_file const & r)
{
  dest_lines.clear();
  dest_lines.reserve(source_lines.size());

  I(r.deltas.find(dest_version) != r.deltas.end());
  shared_ptr<rcs_delta> delta = r.deltas.find(dest_version)->second;
  
  I(r.deltatexts.find(dest_version) != r.deltatexts.end());
  shared_ptr<rcs_deltatext> deltatext = r.deltatexts.find(dest_version)->second;
  
  vector<piece> deltalines;
  global_pieces.index_deltatext(deltatext, deltalines);
  
  int cursor = 0;
  for (vector<piece>::const_iterator i = deltalines.begin(); 
       i != deltalines.end(); )
    process_one_hunk(source_lines, dest_lines, i, cursor);
  while (cursor < static_cast<int>(source_lines.size()))
    dest_lines.push_back(source_lines[cursor++]);
}

// FIXME: should these be someplace else? using 'friend' to reach into the
// DB is stupid, but it's also stupid to put raw edge insert methods on the
// DB itself. or is it? hmm.. encapsulation vs. usage guidance..
void 
rcs_put_raw_file_edge(hexenc<id> const & old_id,
		      hexenc<id> const & new_id,
		      base64< gzip<delta> > const & del,
		      database & db)
{
  if (db.delta_exists(old_id, "file_deltas"))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(F("existing path to %s found, skipping\n") % old_id);
    }
  else
    {
      I(db.exists(new_id, "files")
	|| db.delta_exists(new_id, "file_deltas"));
      db.put_delta(old_id, new_id, del, "file_deltas");
    }
}

void 
rcs_put_raw_manifest_edge(hexenc<id> const & old_id,
			  hexenc<id> const & new_id,
			  base64< gzip<delta> > const & del,
			  database & db)
{
  if (db.delta_exists(old_id, "manifest_deltas"))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(F("existing path to %s found, skipping\n") % old_id);
    }
  else
    {
      I(db.exists(new_id, "manifests")
	|| db.delta_exists(new_id, "manifest_deltas"));
      db.put_delta(old_id, new_id, del, "manifest_deltas");
    }
}


static void
insert_into_db(data const & curr_data,
	       hexenc<id> const & curr_id,
	       vector< piece > const & next_lines,
	       data & next_data,
	       hexenc<id> & next_id,
	       database & db)
{
  // inserting into the DB
  // note: curr_lines is a "new" (base) version
  //       and next_lines is an "old" (derived) version.
  //       all storage edges go from new -> old.
  {
    string tmp;
    global_pieces.build_string(next_lines, tmp);
    next_data = tmp;
  }
  base64< gzip<delta> > del;
  diff(curr_data, next_data, del);
  calculate_ident(next_data, next_id);
  rcs_put_raw_file_edge(next_id, curr_id, del, db);
}


static void 
process_branch(string const & begin_version, 
	       vector< piece > const & begin_lines,
	       data const & begin_data,
	       hexenc<id> const & begin_id,
	       rcs_file const & r, 
	       database & db,
	       cvs_history & cvs)
{
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece> 
					   (begin_lines.begin(),
					    begin_lines.end()));  
  data curr_data(begin_data), next_data;
  hexenc<id> curr_id(begin_id), next_id;
  
  while(! (r.deltas.find(curr_version) == r.deltas.end() ||
	   r.deltas.find(curr_version)->second->next.empty()))
    {
      L(F("version %s has %d lines\n") % curr_version % curr_lines->size());
      
      // construct this edge on our own branch
      string next_version = r.deltas.find(curr_version)->second->next;
      L(F("following RCS edge %s -> %s\n") % curr_version % next_version);

      construct_version(*curr_lines, next_version, *next_lines, r);
      L(F("constructed RCS version %s, inserting into database\n") % 
	next_version);

      insert_into_db(curr_data, curr_id, 
		     *next_lines, next_data, next_id, db);

      cvs.note_file_edge (r, curr_version, next_version, 
			  file_id(curr_id), file_id(next_id));

      // recursively follow any branches rooted here
      boost::shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(vector<string>::const_iterator i = curr_delta->branches.begin();
	  i != curr_delta->branches.end(); ++i)
	{
	  L(F("following RCS branch %s\n") % (*i));
	  vector< piece > branch_lines;
	  construct_version(*curr_lines, *i, branch_lines, r);
	  
	  data branch_data;
	  hexenc<id> branch_id;
	  insert_into_db(curr_data, curr_id, 
			 branch_lines, branch_data, branch_id, db);
	  cvs.push_branch (r, curr_version, *i);

	  cvs.note_file_edge (r, curr_version, *i,
			      file_id(curr_id), file_id(branch_id));

	  process_branch(*i, branch_lines, branch_data, branch_id, r, db, cvs);
	  cvs.pop_branch();
	  L(F("finished RCS branch %s\n") % (*i));
	}

      // advance
      curr_data = next_data;
      curr_id = next_id;
      curr_version = next_version;
      swap(next_lines, curr_lines);
      next_lines->clear();
    }
} 


void 
import_rcs_file_with_cvs(string const & filename, database & db, cvs_history & cvs)
{
  rcs_file r;
  L(F("parsing RCS file %s\n") % filename);
  parse_rcs_file(filename, r);
  L(F("parsed RCS file %s OK\n") % filename);

  {
    vector< piece > head_lines;  
    I(r.deltatexts.find(r.admin.head) != r.deltatexts.end());

    hexenc<id> id; 
    base64< gzip<data> > packed;
    data dat(r.deltatexts.find(r.admin.head)->second->text);
    calculate_ident(dat, id);
    file_id fid = id;

    cvs.set_filename (filename, fid);

    if (! db.file_version_exists (fid))
      {
	pack(dat, packed);
	file_data fdat = packed;
	db.put_file(fid, fdat);	
      }
    
    
    {
      // create the head state in case it is a loner
      cvs_key k;
      shared_ptr<cvs_state> s;
      L(F("noting head version %s : %s\n") % cvs.curr_file % r.admin.head);
      cvs.find_key_and_state (r, r.admin.head, k, s);

      // add this file and youngest version to the head manifest
      I(cvs.head_manifest.find(cvs.curr_file) ==  cvs.head_manifest.end());
      if (r.deltas[r.admin.head]->state != "dead")
          cvs.head_manifest.insert(make_pair(cvs.curr_file, fid));
      else
         L(F("not adding %s to manifest since state is '%s'\n")
         	% cvs.curr_file % r.deltas[r.admin.head]->state);
    }

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second, head_lines);
    process_branch(r.admin.head, head_lines, dat, id, r, db, cvs);
    global_pieces.reset();
  }

  ui.set_tick_trailer("");
}


void 
import_rcs_file(fs::path const & filename, database & db)
{
  cvs_history cvs;

  I(! fs::is_directory(filename));
  I(! filename.empty());

  fs::path leaf = mkpath(filename.leaf());
  fs::path branch = mkpath(filename.branch_path().string());

  I(! branch.empty());
  I(! leaf.empty());
  I( fs::is_directory(branch));
  I( fs::exists(branch));

  I(chdir(filename.branch_path().native_directory_string().c_str()) == 0); 

  I(fs::exists(leaf));

  import_rcs_file_with_cvs(leaf.native_file_string(), db, cvs);
}


// CVS importing stuff follows

/*

  we define a "cvs key" as a triple of author, commit time and
  changelog. the equality of keys is a bit blurry due to a window of time
  in which "the same" commit may begin and end. the window is evaluated
  during the multimap walk though; for insertion in the multimap a true >
  is used. a key identifies a particular commit.

  we reconstruct the history of a CVS archive by accumulating file edges
  into archive nodes. each node is called a "cvs_state", but it is really a
  collection of file *edges* leading into that archive state. we accumulate
  file edges by walking up the trunk and down the branches of each RCS file.

  once we've got all the edges accumulated into archive nodes, we walk the
  tree of cvs_states, up through the trunk and down through the branches,
  carrying a manifest_map with us during the walk. for each edge, we
  construct either the parent or child state of the edge (depending on
  which way we're walking) and then calculate and write out a manifest
  delta for the difference between the previous and current manifest map. we
  also write out manifest certs, though the direction of ancestry changes
  depending on whether we're going up the trunk or down the branches.

 */

cvs_file_edge::cvs_file_edge (file_id const & pv, file_path const & pp, bool pl,
			      file_id const & cv, file_path const & cp, bool cl,
			      cvs_history & cvs) :
  parent_version(cvs.file_version_interner.intern(pv.inner()())), 
  parent_path(cvs.path_interner.intern(pp())),
  parent_live_p(pl),
  child_version(cvs.file_version_interner.intern(cv.inner()())), 
  child_path(cvs.path_interner.intern(cp())),
  child_live_p(cl)
{
}


string 
find_branch_for_version(multimap<string,string> const & symbols,
			string const & version,
			string const & base)
{
  typedef multimap<string,string>::const_iterator ity;
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;

  L(F("looking up branch name for %s\n") % version);

  boost::char_separator<char> sep(".");
  tokenizer tokens(version, sep);
  vector<string> components;
  copy(tokens.begin(), tokens.end(), back_inserter(components));

  if (components.size() < 4)
    {
      L(F("version %s has too few components, using branch %s\n")
	% version % base);
      return base;
    }
  
  string branch_version;
  components[components.size() - 1] = components[components.size() - 2];
  components[components.size() - 2] = "0";
  for (size_t i = 0; i < components.size(); ++i)
    {
      if (i != 0)
	branch_version += ".";
      branch_version += components[i];
    }

  pair<ity,ity> range = symbols.equal_range(branch_version);
  if (range.first == symbols.end())
    {
      L(F("no branch %s found, using base '%s'\n") 
	% branch_version % base);
      return base;
    }
  else
    {
      string res = base;
      res += ".";
      res += range.first->second;
      int num_results = 0;
      while (range.first != range.second)
	{ range.first++; num_results++; }

      if (num_results > 1)
	W(F("multiple entries (%d) for branch %s found, using: '%s'\n")
	  % num_results % branch_version % res);
      else
	L(F("unique entry for branch %s found: '%s'\n") 
	  % branch_version % res);
      return res;
    }
}

cvs_key::cvs_key(rcs_file const & r, string const & version,
		 cvs_history & cvs) 
{
  map<string, shared_ptr<rcs_delta> >::const_iterator delta = 
    r.deltas.find(version);
  I(delta != r.deltas.end());

  map<string, shared_ptr<rcs_deltatext> >::const_iterator deltatext = 
    r.deltatexts.find(version);
  I(deltatext != r.deltatexts.end());

  {    
    struct tm t;
    char const * dp = delta->second->date.c_str();
    if (strptime(dp, "%y.%m.%d.%H.%M.%S", &t) == NULL)
      I(strptime(dp, "%Y.%m.%d.%H.%M.%S", &t) != NULL);
    time=mktime(&t);
  }

  string branch_name = find_branch_for_version(r.admin.symbols, 
					       version, 
					       cvs.base_branch);
  branch = cvs.branch_interner.intern(branch_name);
  changelog = cvs.changelog_interner.intern(deltatext->second->log);
  author = cvs.author_interner.intern(delta->second->author);
}


cvs_history::cvs_history() :
  n_versions("versions", 1),
  n_tree_branches("branches", 1)
{
  stk.push(shared_ptr<cvs_state>(new cvs_state()));  
}

void 
cvs_history::set_filename(string const & file,
			  file_id const & ident) 
{
  L(F("importing file '%s'\n") % file);
  I(file.size() > 2);
  I(file.substr(file.size() - 2) == string(",v"));
  string ss = file;
  ui.set_tick_trailer(ss);
  ss.resize(ss.size() - 2);
  // remove Attic/ if present
  std::string::size_type last_slash=ss.rfind('/');
  if (last_slash!=std::string::npos && last_slash>=5
  	&& ss.substr(last_slash-5,6)=="Attic/")
     ss.erase(last_slash-5,6);
  curr_file = file_path(ss);
}

bool 
cvs_history::find_key_and_state(rcs_file const & r, 
				string const & version,
				cvs_key & key,
				shared_ptr<cvs_state> & state)
{
  I(stk.size() > 0);
  map< cvs_key, shared_ptr<cvs_state> > & substates = stk.top()->substates;
  cvs_key nk(r, version, *this);

  // key+(window/2) is in the future, key-(window/2) is in the past. the
  // past is considered "greater than" the future in this map, so we take:
  // 
  //  - new, the lower bound of key+(window/2) in the map
  //  - old, the upper bound of key-(window/2) in the map
  //
  // and search all the nodes inside this section, from new to old bound.

  map< cvs_key, shared_ptr<cvs_state> >::const_iterator i_new, i_old, i;
  cvs_key k_new(nk), k_old(nk);

  if (static_cast<time_t>(k_new.time + constants::cvs_window / 2) > k_new.time)
    k_new.time += constants::cvs_window / 2;

  if (static_cast<time_t>(k_old.time - constants::cvs_window / 2) < k_old.time)
    k_old.time -= constants::cvs_window / 2;
  
  i_new = substates.lower_bound(k_new);
  i_old = substates.upper_bound(k_old);

  for (i = i_new; i != i_old; ++i)
    {
      if (i->first.similar_enough(nk))
	{
	  key = i->first;
	  state = i->second;
	  return true;
	}
    }
  key = nk;
  state = shared_ptr<cvs_state>(new cvs_state());
  substates.insert(make_pair(key, state));
  return false;
}

void
cvs_history::find_branchpoint(rcs_file const & r,
			      string const & branchpoint_version,
			      string const & first_branch_version,
			      shared_ptr<cvs_state> & branchpoint)
{
  cvs_key k; 
  I(find_key_and_state(r, branchpoint_version, k, branchpoint));

  string branch_name = find_branch_for_version(r.admin.symbols, 
					       first_branch_version, 
					       base_branch);

  unsigned long branch = branch_interner.intern(branch_name);
    
  map<unsigned long, 
    pair<cvs_key, shared_ptr<cvs_state> > >::const_iterator i 
    = branchpoints.find(branch);

  if (i == branchpoints.end())
    {
      ++n_tree_branches;
      L(F("beginning branch %s at %s : %s\n")
	% branch_name % curr_file % branchpoint_version);
      branchpoints.insert(make_pair(branch, 
				    make_pair(k, branchpoint)));
    }
  else
    {
      // take the earlier of the new key and the existing branchpoint
      if (k.time < i->second.first.time)
	{
	  L(F("moving branch %s back to %s : %s\n")
	    % branch_name % curr_file % branchpoint_version);
	  shared_ptr<cvs_state> old = i->second.second;
	  set<cvs_key> moved;
	  for (map< cvs_key, shared_ptr<cvs_state> >::const_iterator j = 
		 old->substates.begin(); j != old->substates.end(); ++j)
	    {
	      if (j->first.branch == branch)
		{
		  branchpoint->substates.insert(*j);
		  moved.insert(j->first);
		}
	    }
	  for (set<cvs_key>::const_iterator j = moved.begin(); j != moved.end();
	       ++j)
	    {
	      old->substates.erase(*j);
	    }
	  branchpoints[branch] = make_pair(k, branchpoint);
	}
      else
	{
	  L(F("using existing branchpoint for %s at %s : %s\n")
	    % branch_name % curr_file % branchpoint_version);
	  branchpoint = i->second.second;
	}
    }
}

void 
cvs_history::push_branch(rcs_file const & r, 
			 string const & branchpoint_version,
			 string const & first_branch_version) 
{      
  shared_ptr<cvs_state> branchpoint;
  I(stk.size() > 0);
  find_branchpoint(r, branchpoint_version, 
		   first_branch_version, branchpoint);
  stk.push(branchpoint);
}

void 
cvs_history::note_file_edge(rcs_file const & r, 
			    string const & prev_rcs_version_num,
			    string const & next_rcs_version_num,
			    file_id const & prev_version,
			    file_id const & next_version) 
{

  cvs_key k;
  shared_ptr<cvs_state> s;

  I(stk.size() > 0);
  I(! curr_file().empty());
  
  // we can't use operator[] since it is non-const
  std::map<std::string, boost::shared_ptr<rcs_delta> >::const_iterator
  	prev_delta = r.deltas.find(prev_rcs_version_num),
  	next_delta = r.deltas.find(next_rcs_version_num);
  I(prev_delta!=r.deltas.end());
  I(next_delta!=r.deltas.end());
  bool prev_alive = prev_delta->second->state!="dead";
  bool next_alive = next_delta->second->state!="dead";
  
  L(F("note_file_edge %s %d -> %s %d\n") % prev_rcs_version_num % prev_alive
  		% next_rcs_version_num % next_alive);

  // we always aggregate in-edges in children, but we will also create
  // parents as we encounter them.
  if (stk.size() == 1)
    {
      // we are on the trunk, prev is child, next is parent.
      L(F("noting trunk edge %s : %s -> %s\n") % curr_file
	% next_rcs_version_num
	% prev_rcs_version_num);
      find_key_and_state (r, next_rcs_version_num, k, s); // just to create it if necessary
      find_key_and_state (r, prev_rcs_version_num, k, s);
      
      s->in_edges.insert(cvs_file_edge(next_version, curr_file, next_alive,
				       prev_version, curr_file, prev_alive,
				       *this));
    }
  else
    {
      // we are on a branch, prev is parent, next is child.
      L(F("noting branch edge %s : %s -> %s\n") % curr_file
	% prev_rcs_version_num
	% next_rcs_version_num);
      find_key_and_state (r, next_rcs_version_num, k, s);
      s->in_edges.insert(cvs_file_edge(prev_version, curr_file, prev_alive,
				       next_version, curr_file, next_alive,
				       *this));
    }
    
  ++n_versions;
}

void 
cvs_history::pop_branch() 
{
  I(stk.size() > 1);
  I(stk.top()->substates.size() > 0);
  stk.pop();
}


class 
cvs_tree_walker 
  : public tree_walker
{
  cvs_history & cvs;
  database & db;
public:
  cvs_tree_walker(cvs_history & c, database & d) : 
    cvs(c), db(d) 
  {
  }
  virtual void visit_file(file_path const & path)
  {
    string file = path();
    if (file.substr(file.size() - 2) == string(",v"))      
      {
	import_rcs_file_with_cvs(file, db, cvs);
      }
    else
      L(F("skipping non-RCS file %s\n") % file);
  }
  virtual ~cvs_tree_walker() {}
};


// this is for a branch edge, in which the ancestry is the opposite
// direction as the storage delta. nb: the terms 'parent' and 'child' are
// *always* ancestry terms; we refer to 'old' and 'new' for storage system
// terms (where, perhaps confusingly, old versions are those constructed by
// applying deltas from new versions, so in fact we are successively
// creating older and older versions here. 'old' and 'new' do not refer to
// the execution time of this program, but rather the storage system's view
// of time.

void 
store_branch_manifest_edge(manifest_map const & parent,
			   manifest_map const & child,
			   manifest_id const & parent_id,
			   manifest_id const & child_id,
			   app_state & app,
			   cvs_history & cvs)
{

  unsigned long p, c;
  p = cvs.manifest_version_interner.intern(parent_id.inner()());
  c = cvs.manifest_version_interner.intern(child_id.inner()());

  if (cvs.manifest_cycle_detector.edge_makes_cycle(p,c))
    {
      L(F("skipping cyclical branch edge %s -> %s\n")
	% parent_id % child_id);
    }
  else
    {
      L(F("storing branch manifest edge %s -> %s\n") 
	% parent_id % child_id);

      cvs.manifest_cycle_detector.put_edge(p,c);
      
      // in this case, the ancestry-based 'child' is on a branch, so it is
      // an 'old' version as far as the storage system is concerned; that
      // is to say it is constructed by first building a 'new' version (the
      // ancestry-based 'parent') and then following a delta to the
      // child. remember that the storage system assumes that all deltas go
      // from temporally new -> temporally old. so the delta should go from
      // parent (new) -> child (old)

      base64< gzip<delta> > del;	      
      diff(parent, child, del);
      rcs_put_raw_manifest_edge(child_id.inner(),
				parent_id.inner(),				
				del, app.db);
      packet_db_writer dbw(app);
      cert_manifest_ancestor(parent_id, child_id, app, dbw);
    }  
}

// this is for a trunk edge, in which the ancestry is the
// same direction as the storage delta
void 
store_trunk_manifest_edge(manifest_map const & parent,
			  manifest_map const & child,
			  manifest_id const & parent_id,
			  manifest_id const & child_id,
			  app_state & app,
			  cvs_history & cvs)
{

  unsigned long p, c;
  p = cvs.manifest_version_interner.intern(parent_id.inner()());
  c = cvs.manifest_version_interner.intern(child_id.inner()());

  if (cvs.manifest_cycle_detector.edge_makes_cycle(p,c))
    {
      L(F("skipping cyclical trunk edge %s -> %s\n")
	% parent_id % child_id);
    }
  else
    {
      L(F("storing trunk manifest edge %s -> %s\n") 
	% parent_id % child_id);

      cvs.manifest_cycle_detector.put_edge(p,c);

      // in this case, the ancestry-based 'child' is on a trunk, so it is
      // a 'new' version as far as the storage system is concerned; that
      // is to say that the ancestry-based 'parent' is a temporally older
      // tree version, which can be constructed from the 'newer' child. so
      // the delta should run from child (new) -> parent (old).

      base64< gzip<delta> > del;	      
      diff(child, parent, del);
      rcs_put_raw_manifest_edge(parent_id.inner(),
				child_id.inner(),
				del, app.db);
      packet_db_writer dbw(app);
      cert_manifest_ancestor(parent_id, child_id, app, dbw);
    }  
}

void 
store_auxiliary_certs(cvs_key const & key, 
		      manifest_id const & id, 
		      app_state & app, 
		      cvs_history const & cvs)
{
  packet_db_writer dbw(app);
  cert_manifest_in_branch(id, cert_value(cvs.branch_interner.lookup(key.branch)), app, dbw); 
  cert_manifest_author(id, cvs.author_interner.lookup(key.author), app, dbw); 
  cert_manifest_changelog(id, cvs.changelog_interner.lookup(key.changelog), app, dbw);
  cert_manifest_date_time(id, key.time, app, dbw);
}

// we call this when we're going child -> parent, i.e. when we're walking
// up the trunk.
void 
build_parent_state(shared_ptr<cvs_state> state,
		   manifest_map & state_map,
		   cvs_history & cvs)
{
  for (set<cvs_file_edge>::const_iterator f = state->in_edges.begin();
       f != state->in_edges.end(); ++f)
    {
      file_id fid(cvs.file_version_interner.lookup(f->parent_version));
      file_path pth(cvs.path_interner.lookup(f->parent_path));
      if (!f->parent_live_p)
      {  manifest_map::iterator elem=state_map.find(pth);
         if (elem != state_map.end())
            state_map.erase(elem);
         else 
            L(F("could not find file %s for removal from manifest\n") 
            	% pth);
      }
      else 
         state_map[pth] = fid;
    }  
  L(F("logical changeset from child -> parent has %d file deltas\n")
    % state->in_edges.size());
}

// we call this when we're going parent -> child, i.e. when we're walking
// down a branch.
void 
build_child_state(shared_ptr<cvs_state> state,
		  manifest_map & state_map,
		  cvs_history & cvs)
{
  for (set<cvs_file_edge>::const_iterator f = state->in_edges.begin();
       f != state->in_edges.end(); ++f)
    {
      file_id fid(cvs.file_version_interner.lookup(f->child_version));
      file_path pth(cvs.path_interner.lookup(f->child_path));
      if (!f->child_live_p)
      {  manifest_map::iterator elem=state_map.find(pth);
         if (elem != state_map.end())
            state_map.erase(elem);
         else 
            L(F("could not find file %s for removal from manifest\n") 
            	% pth);
      }
      else 
         state_map[pth] = fid;
    }  
  L(F("logical changeset from parent -> child has %d file deltas\n") 
    % state->in_edges.size());
}

void
import_substates(ticker & n_edges, 
		 ticker & n_branches,
		 shared_ptr<cvs_state> state,
		 cvs_branchname branch_filter,
		 manifest_map parent_map,
		 cvs_history & cvs,
		 app_state & app);

void 
import_substates_by_branch(ticker & n_edges, 
			   ticker & n_branches,
			   shared_ptr<cvs_state> state,
			   manifest_map const & parent_map,
			   cvs_history & cvs,
			   app_state & app)
{
  set<cvs_branchname> branches;

  // collect all the branches
  for (map< cvs_key, shared_ptr<cvs_state> >::reverse_iterator i = state->substates.rbegin();
       i != state->substates.rend(); ++i)
    branches.insert(i->first.branch);

  // walk each sub-branch in order
  for (set<cvs_branchname>::const_iterator branch = branches.begin();
       branch != branches.end(); ++branch)
    {
      import_substates(n_edges, n_branches, state, *branch, parent_map, cvs, app);
    }
}


void 
import_substates(ticker & n_edges, 
		 ticker & n_branches,
		 shared_ptr<cvs_state> state,
		 cvs_branchname branch_filter,
		 manifest_map parent_map,
		 cvs_history & cvs,
		 app_state & app)
{
  manifest_id parent_id;
  calculate_ident(parent_map, parent_id);
  manifest_map child_map = parent_map;

  if (state->substates.size() > 0)
    ++n_branches;

  // these are all sub-branches, so we look through them temporally
  // *backwards* from oldest to newest
  for (map< cvs_key, shared_ptr<cvs_state> >::reverse_iterator i = state->substates.rbegin();
       i != state->substates.rend(); ++i)
    {
      if (i->first.branch != branch_filter)
	continue;

      manifest_id child_id;
      build_child_state(i->second, child_map, cvs);
      calculate_ident(child_map, child_id);
      store_branch_manifest_edge(parent_map, child_map, parent_id, child_id, app, cvs);
      store_auxiliary_certs(i->first, child_id, app, cvs);
      if (i->second->substates.size() > 0)
	import_substates_by_branch(n_edges, n_branches, i->second, child_map, cvs, app);

      // now apply the edge to the parent, too, making parent = child
      build_child_state(i->second, parent_map, cvs);
      parent_id = child_id;
      ++n_edges;
    }
}


void 
import_cvs_repo(fs::path const & cvsroot, 
		app_state & app)
{
  
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    N(app.db.private_key_exists(key),
      F("no private key '%s' found in database") % key);
  }

  cvs_history cvs;
  N(app.branch_name() != "", F("need base --branch argument for importing"));
  cvs.base_branch = app.branch_name();

  {
    transaction_guard guard(app.db);
    cvs_tree_walker walker(cvs, app.db);
    N( fs::exists(cvsroot),
       F("path %s does not exist") % cvsroot.string());
    N( fs::is_directory(cvsroot),
       F("path %s is not a directory") % cvsroot.string());
    app.db.ensure_open();
    N(chdir(cvsroot.native_directory_string().c_str()) == 0,
      F("could not change directory to %s") % cvsroot.string());
    walk_tree(walker);
    guard.commit();
  }

  P(F("phase 1 (version import) complete\n"));

  I(cvs.stk.size() == 1);
  shared_ptr<cvs_state> state = cvs.stk.top();
  manifest_map child_map = cvs.head_manifest;
  manifest_map parent_map = child_map;
  manifest_id child_id;
  calculate_ident (child_map, child_id);


  {
    ticker n_branches("finished branches", 1), n_edges("finished edges", 1);
    transaction_guard guard(app.db);
    
    // write the trunk head version
    if (!app.db.manifest_version_exists (child_id))
      {
	manifest_data child_data;
	write_manifest_map(child_map, child_data);
	app.db.put_manifest(child_id, child_data);
      }

    // these are all versions on the main trunk, so we look through them from
    // newest to oldest
    for (map< cvs_key, shared_ptr<cvs_state> >::const_iterator i = state->substates.begin();
	 i != state->substates.end(); ++i)
      {
	manifest_id parent_id;
	build_parent_state(i->second, parent_map, cvs);
	calculate_ident(parent_map, parent_id);
	store_trunk_manifest_edge(parent_map, child_map, parent_id, child_id, app, cvs);
	store_auxiliary_certs(i->first, child_id, app, cvs);
	if (i->second->substates.size() > 0)
	  import_substates_by_branch(n_edges, n_branches, i->second, parent_map, cvs, app);

	// now apply the edge to the child, too, making child = parent
	build_parent_state(i->second, child_map, cvs);
	child_id = parent_id;
	++n_edges;
      }
    
    P(F("phase 2 (ancestry reconstruction) complete\n"));

    guard.commit();
  }
}

