// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
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
#include "keys.hh"
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
    L(F("Checking similarity of %d and %d\n") % id % other.id);
    if (changelog != other.changelog)
      return false;
    if (author != other.author)
      return false;
    if (labs(time - other.time) > constants::cvs_window)
      return false;
    for (map<file_path,string>::const_iterator it = files.begin(); it!=files.end(); it++)
      {
        map<file_path,string>::const_iterator otherit;
        
        L(F("checking %s %s\n") % it->first % it->second);
        otherit = other.files.find(it->first);
        if (otherit != other.files.end() && it->second!=otherit->second)
          {
            L(F("!similar_enough: %d/%d\n") % id % other.id);
            return false;
          }
        else if (otherit != other.files.end())
          {
            L(F("Same file, different version: %s and %s\n") % it->second % otherit->second);
          }
      }
    L(F("similar_enough: %d/%d\n") % id % other.id);
    return true;
  }

  inline bool operator==(cvs_key const & other) const
  {
    L(F("Checking equality of %d and %d\n") % id % other.id);
    return branch == other.branch &&
      changelog == other.changelog &&
      author == other.author &&
      time == other.time;
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

  inline void add_file(file_path const &file, string const &version)
  {
    L(F("Adding file %s version %s to %d\n") % file % version % id);
    files.insert( make_pair(file, version) );
  }

  cvs_branchname branch;
  cvs_changelog changelog;
  cvs_author author;
  time_t time;
  map<file_path, string> files; // Maps file to version
  int id; // Only used for debug output

  static int nextid; // Used to initialise id
};

int cvs_key::nextid = 0;

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
branch_point
{
  branch_point() {}
  branch_point(cvs_key const & k,
               shared_ptr<cvs_state> const & s,
               size_t l)
               : key(k), state(s), rev_comp_len(l) {}
  cvs_key key;
  shared_ptr<cvs_state> state;
  size_t rev_comp_len;
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

  map<unsigned long, branch_point> branchpoints;

  state_stack stk;
  file_path curr_file;
  
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
                        shared_ptr<cvs_state> & bp_state);
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


static void 
process_one_hunk(vector< piece > const & source,
                 vector< piece > & dest,
                 vector< piece >::const_iterator & i,
                 int & cursor)
{
  string directive = **i;
  assert(directive.size() > 1);
  ++i;

  try 
    {
      char code;
      int pos, len;
      if (sscanf(directive.c_str(), " %c %d %d", &code, &pos, &len) != 3)
	      throw oops("illformed directive '" + directive + "'");

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
                      delta const & del,
                      database & db)
{
  if (old_id == new_id)
    {
      L(F("skipping identity file edge\n"));
      return;
    }

  if (db.file_version_exists(old_id))
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
                          delta const & del,
                          database & db)
{
  if (old_id == new_id)
    {
      L(F("skipping identity manifest edge\n"));
      return;
    }

  if (db.manifest_version_exists(old_id))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(F("existing path to %s found, skipping\n") % old_id);
    }
  else
    {
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
  delta del;
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
  
  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      L(F("version %s has %d lines\n") % curr_version % curr_lines->size());
      
      string next_version = r.deltas.find(curr_version)->second->next;
      if (!next_version.empty())
      {  // construct this edge on our own branch
         L(F("following RCS edge %s -> %s\n") % curr_version % next_version);

         construct_version(*curr_lines, next_version, *next_lines, r);
         L(F("constructed RCS version %s, inserting into database\n") % 
           next_version);

         insert_into_db(curr_data, curr_id, 
                     *next_lines, next_data, next_id, db);

         cvs.note_file_edge (r, curr_version, next_version, 
                          file_id(curr_id), file_id(next_id));
      }
      else
      {  L(F("revision %s has no successor\n") % curr_version);
         if (curr_version=="1.1")
         {  // mark this file as newly present since this commit
            // (and as not present before)
            
            // perhaps this should get a member function of cvs_history ?
            L(F("marking %s as not present in older manifests\n") % curr_version);
            cvs_key k;
            shared_ptr<cvs_state> s;
            cvs.find_key_and_state(r, curr_version, k, s);
            I(r.deltas.find(curr_version) != r.deltas.end());
            bool live_p = r.deltas.find(curr_version)->second->state != "dead";
            s->in_edges.insert(cvs_file_edge(curr_id, cvs.curr_file, false,
                                             curr_id, cvs.curr_file, live_p,
                                             cvs));
            ++cvs.n_versions;
         }
      }

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

      if (!r.deltas.find(curr_version)->second->next.empty())
      {  // advance
         curr_data = next_data;
         curr_id = next_id;
         curr_version = next_version;
         swap(next_lines, curr_lines);
         next_lines->clear();
      }
      else break;
    }
} 


static void 
import_rcs_file_with_cvs(string const & filename, database & db, cvs_history & cvs)
{
  rcs_file r;
  L(F("parsing RCS file %s\n") % filename);
  parse_rcs_file(filename, r);
  L(F("parsed RCS file %s OK\n") % filename);

  {
    vector< piece > head_lines;  
    I(r.deltatexts.find(r.admin.head) != r.deltatexts.end());
    I(r.deltas.find(r.admin.head) != r.deltas.end());

    hexenc<id> id; 
    data dat(r.deltatexts.find(r.admin.head)->second->text);
    calculate_ident(dat, id);
    file_id fid = id;

    cvs.set_filename (filename, fid);

    if (! db.file_version_exists (fid))
      {
        db.put_file(fid, dat); 
      }
        
    {
      // create the head state in case it is a loner
      cvs_key k;
      shared_ptr<cvs_state> s;
      L(F("noting head version %s : %s\n") % cvs.curr_file % r.admin.head);
      cvs.find_key_and_state (r, r.admin.head, k, s);
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

static void
version_to_components(string const & version,
                      vector<string> & components)
{
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  boost::char_separator<char> sep(".");
  tokenizer tokens(version, sep);

  components.clear();
  copy(tokens.begin(), tokens.end(), back_inserter(components));
}

static string 
find_branch_for_version(multimap<string,string> const & symbols,
                        string const & version,
                        string const & base)
{
  typedef multimap<string,string>::const_iterator ity;

  L(F("looking up branch name for %s\n") % version);

  vector<string> components;
  version_to_components(version, components);

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
    // We need to initialize t to all zeros, because strptime has a habit of
    // leaving bits of the data structure alone, letting garbage sneak into
    // our output.
    memset(&t, 0, sizeof(t));
    char const * dp = delta->second->date.c_str();
    L(F("Calculating time of %s\n") % dp);
#ifdef WIN32
    I(sscanf(dp, "%d.%d.%d.%d.%d.%d", &(t.tm_year), &(t.tm_mon), 
             &(t.tm_mday), &(t.tm_hour), &(t.tm_min), &(t.tm_sec))==6);
    t.tm_mon--;
    // Apparently some RCS files have 2 digit years, others four; tm always
    // wants a 2 (or 3) digit year (years since 1900).
    if (t.tm_year > 1900)
        t.tm_year-=1900;
#else
    if (strptime(dp, "%y.%m.%d.%H.%M.%S", &t) == NULL)
      I(strptime(dp, "%Y.%m.%d.%H.%M.%S", &t) != NULL);
#endif
    time=mktime(&t);
    L(F("= %i\n") % time);
    id = nextid++;
  }

  string branch_name = find_branch_for_version(r.admin.symbols, 
                                               version, 
                                               cvs.base_branch);
  branch = cvs.branch_interner.intern(branch_name);
  changelog = cvs.changelog_interner.intern(deltatext->second->log);
  author = cvs.author_interner.intern(delta->second->author);
}


cvs_history::cvs_history() :
  n_versions("versions", "v", 1),
  n_tree_branches("branches", "b", 1)
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

  nk.add_file(curr_file, version);
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
          key.add_file(curr_file, version);
          substates.erase(i->first);
          substates.insert(make_pair(key, state));
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
                              shared_ptr<cvs_state> & bp_state)
{
  cvs_key k; 
  I(find_key_and_state(r, branchpoint_version, k, bp_state));

  string branch_name = find_branch_for_version(r.admin.symbols, 
                                               first_branch_version, 
                                               base_branch);

  unsigned long branch = branch_interner.intern(branch_name);
    
  map<unsigned long, branch_point>::const_iterator i 
    = branchpoints.find(branch);

  vector<string> new_components;
  version_to_components(branchpoint_version, new_components);
  size_t newlen = new_components.size();


  if (i == branchpoints.end())
    {
      ++n_tree_branches;
      L(F("beginning branch %s at %s : %s\n")
        % branch_name % curr_file % branchpoint_version);
      branchpoints.insert(make_pair(branch, 
                                    branch_point(k, bp_state, newlen)));
    }
  else
    {
      // if this version comes off the main trunk, but the previous branchpoint
      // version comes off a branch (such as an import 1.1.1.1), then we want
      // to take the one closest to the trunk. 
      // TODO perhaps we need to reconsider branching in general for cvs_import,
      // this is pretty much just a workaround for 1.1<->1.1.1.1 equivalence

      // take the earlier of the new key and the existing branchpoint
      if ((k.time < i->second.key.time && newlen <= i->second.rev_comp_len)
              || newlen < i->second.rev_comp_len)
        {
          L(F("moving branch %s back to %s : %s\n")
            % branch_name % curr_file % branchpoint_version);
          shared_ptr<cvs_state> old = i->second.state;
          set<cvs_key> moved;
          for (map< cvs_key, shared_ptr<cvs_state> >::const_iterator j = 
                 old->substates.begin(); j != old->substates.end(); ++j)
            {
              if (j->first.branch == branch)
                {
                  bp_state->substates.insert(*j);
                  moved.insert(j->first);
                }
            }
          for (set<cvs_key>::const_iterator j = moved.begin(); j != moved.end();
               ++j)
            {
              old->substates.erase(*j);
            }
          branchpoints[branch] = branch_point(k, bp_state, newlen);
        }
      else
        {
          L(F("using existing branchpoint for %s at %s : %s\n")
            % branch_name % curr_file % branchpoint_version);
          bp_state = i->second.state;
        }
    }
}

void 
cvs_history::push_branch(rcs_file const & r, 
                         string const & branchpoint_version,
                         string const & first_branch_version) 
{      
  shared_ptr<cvs_state> bp_state;
  I(stk.size() > 0);
  find_branchpoint(r, branchpoint_version, 
                   first_branch_version, bp_state);
  stk.push(bp_state);
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


static void 
store_manifest_edge(manifest_map const & parent,
                    manifest_map const & child,
                    manifest_id const & parent_mid,
                    manifest_id const & child_mid,
                    app_state & app,
                    cvs_history & cvs,
                    unsigned long depth,
                    bool head_manifest_p)
{

  if (depth == 0)
    L(F("storing trunk manifest %s (base %s)\n") % parent_mid % child_mid);
  else
    L(F("storing branch manifest %s (base %s)\n") % child_mid % parent_mid);

  if (depth == 0 && head_manifest_p)
    {
      L(F("storing trunk head %s\n") % child_mid);
      // the trunk branch has one very important manifest: the head.
      // this is the "newest" of all manifests within the import, and
      // we store it in its entirety.
      if (! app.db.manifest_version_exists(child_mid))
        {
          manifest_data mdat;
          write_manifest_map(child, mdat);
          app.db.put_manifest(child_mid, mdat);
        }
    }

  if (null_id(parent_mid))
    {
      L(F("skipping null manifest\n"));
      return;
    }

  unsigned long p, c, older, newer;
  p = cvs.manifest_version_interner.intern(parent_mid.inner()());
  c = cvs.manifest_version_interner.intern(child_mid.inner()());
  older = (depth == 0) ? p : c;
  newer = (depth == 0) ? c : p;
  if (cvs.manifest_cycle_detector.edge_makes_cycle(older,newer))        
    {
      if (depth == 0)
        {
          L(F("skipping cyclical trunk manifest delta %s -> %s\n") 
            % parent_mid % child_mid);
          // if this is on the trunk, we are potentially breaking the chain
          // one would use to get to p. we need to make sure p exists.
          if (!app.db.manifest_version_exists(parent_mid))
            {
              L(F("writing full manifest %s\n") % parent_mid);
              manifest_data mdat;
              write_manifest_map(parent, mdat);
              app.db.put_manifest(parent_mid, mdat);
            }
        }
      else
        {
          L(F("skipping cyclical branch manifest delta %s -> %s\n") 
            % child_mid % parent_mid);
          // if this is on a branch, we are potentially breaking the chain one
          // would use to get to c. we need to make sure c exists.
          if (!app.db.manifest_version_exists(child_mid))
            {
              L(F("writing full manifest %s\n") % child_mid);
              manifest_data mdat;
              write_manifest_map(child, mdat);
              app.db.put_manifest(child_mid, mdat);
            }
        }       
      return;
    }
  
  cvs.manifest_cycle_detector.put_edge(older,newer);        
  if (depth == 0)
    {
      L(F("storing trunk manifest delta %s -> %s\n") 
        % child_mid % parent_mid);
      
      // in this case, the ancestry-based 'child' is on a trunk, so it is
      // a 'new' version as far as the storage system is concerned; that
      // is to say that the ancestry-based 'parent' is a temporally older
      // tree version, which can be constructed from the 'newer' child. so
      // the delta should run from child (new) -> parent (old).
      
      delta del;
      diff(child, parent, del);
      rcs_put_raw_manifest_edge(parent_mid.inner(),
                                child_mid.inner(),
                                del, app.db);
    }
  else
    {
      L(F("storing branch manifest delta %s -> %s\n") 
        % parent_mid % child_mid);
      
      // in this case, the ancestry-based 'child' is on a branch, so it is
      // an 'old' version as far as the storage system is concerned; that
      // is to say it is constructed by first building a 'new' version (the
      // ancestry-based 'parent') and then following a delta to the
      // child. remember that the storage system assumes that all deltas go
      // from temporally new -> temporally old. so the delta should go from
      // parent (new) -> child (old)
      
      delta del;
      diff(parent, child, del);
      rcs_put_raw_manifest_edge(child_mid.inner(),
                                parent_mid.inner(),                             
                                del, app.db);
    }
}


static void 
store_auxiliary_certs(cvs_key const & key, 
                      revision_id const & id, 
                      app_state & app, 
                      cvs_history const & cvs)
{
  packet_db_writer dbw(app);
  cert_revision_in_branch(id, cert_value(cvs.branch_interner.lookup(key.branch)), app, dbw); 
  cert_revision_author(id, cvs.author_interner.lookup(key.author), app, dbw); 
  cert_revision_changelog(id, cvs.changelog_interner.lookup(key.changelog), app, dbw);
  cert_revision_date_time(id, key.time, app, dbw);
}

static void 
build_change_set(shared_ptr<cvs_state> state,
                 manifest_map const & state_map,
                 cvs_history & cvs,
                 change_set & cs)
{
  change_set empty;
  cs = empty;

  for (set<cvs_file_edge>::const_iterator f = state->in_edges.begin();
       f != state->in_edges.end(); ++f)
    {
      file_id fid(cvs.file_version_interner.lookup(f->child_version));
      file_path pth(cvs.path_interner.lookup(f->child_path));
      if (!f->child_live_p)
        {  
          if (f->parent_live_p)
            {
              L(F("deleting entry state '%s' on '%s'\n") % fid % pth);              
              cs.delete_file(pth);
            }
          else
            {
              // it can actually happen that we have a file that went from
              // dead to dead.  when a file is created on a branch, cvs first
              // _commits a deleted file_ on mainline, and then branches from
              // it and resurrects it.  In such cases, we should just ignore
              // the file, it doesn't actually exist.  So, in this block, we
              // do nothing.
            }
        }
      else 
        {
          manifest_map::const_iterator i = state_map.find(pth);
          if (i == state_map.end())
            {
              L(F("adding entry state '%s' on '%s'\n") % fid % pth);          
              cs.add_file(pth, fid);          
            }
          else if (manifest_entry_id(i) == fid)
            {
              L(F("skipping preserved entry state '%s' on '%s'\n")
                % fid % pth);         
            }
          else
            {
              L(F("applying state delta on '%s' : '%s' -> '%s'\n") 
                % pth % manifest_entry_id(i) % fid);          
              cs.apply_delta(pth, manifest_entry_id(i), fid);
            }
        }  
    }
  L(F("logical changeset from parent -> child has %d file state changes\n") 
    % state->in_edges.size());
}


static void 
import_states_recursive(ticker & n_edges, 
                        ticker & n_branches,
                        shared_ptr<cvs_state> state,
                        cvs_branchname branch_filter,
                        revision_id parent_rid,
                        manifest_id parent_mid,
                        manifest_map parent_map,
                        cvs_history & cvs,
                        app_state & app,
                        vector< pair<cvs_key, revision_set> > & revisions,
                        unsigned long depth);

static void 
import_states_by_branch(ticker & n_edges, 
                        ticker & n_branches,
                        shared_ptr<cvs_state> state,
                        revision_id const & parent_rid,
                        manifest_id const & parent_mid,
                        manifest_map const & parent_map,
                        cvs_history & cvs,
                        app_state & app,
                        vector< pair<cvs_key, revision_set> > & revisions,
                        unsigned long depth)
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
      import_states_recursive(n_edges, n_branches, state, *branch, 
                              parent_rid, parent_mid, parent_map, 
                              cvs, app, revisions, depth);
    }
}

static void 
import_states_recursive(ticker & n_edges, 
                        ticker & n_branches,
                        shared_ptr<cvs_state> state,
                        cvs_branchname branch_filter,
                        revision_id parent_rid,
                        manifest_id parent_mid,
                        manifest_map parent_map,
                        cvs_history & cvs,
                        app_state & app,
                        vector< pair<cvs_key, revision_set> > & revisions,
                        unsigned long depth)
{
  if (state->substates.size() > 0)
    ++n_branches;

  manifest_id child_mid;
  revision_id child_rid;
  manifest_map child_map = parent_map;
  
  string branchname = cvs.branch_interner.lookup(branch_filter);
  ui.set_tick_trailer("building branch " + branchname);

  // these are all sub-branches, so we look through them temporally
  // *backwards* from oldest to newest
  map< cvs_key, shared_ptr<cvs_state> >::reverse_iterator newest_branch_state;
  for (map< cvs_key, shared_ptr<cvs_state> >::reverse_iterator i = state->substates.rbegin();
       i != state->substates.rend(); ++i)
    {
      if (i->first.branch != branch_filter)
        continue;
      newest_branch_state = i;
    }

  for (map< cvs_key, shared_ptr<cvs_state> >::reverse_iterator i = state->substates.rbegin();
       i != state->substates.rend(); ++i)
    {
      if (i->first.branch != branch_filter)
        continue;

      revision_set rev;
      boost::shared_ptr<change_set> cs(new change_set());
      build_change_set(i->second, parent_map, cvs, *cs);

      apply_change_set(*cs, child_map);
      calculate_ident(child_map, child_mid);

      rev.new_manifest = child_mid;
      rev.edges.insert(make_pair(parent_rid, make_pair(parent_mid, cs)));
      calculate_ident(rev, child_rid);

      revisions.push_back(make_pair(i->first, rev));

      store_manifest_edge(parent_map, child_map, 
                          parent_mid, child_mid, 
                          app, cvs, depth, i == newest_branch_state);

      if (i->second->substates.size() > 0)
        import_states_by_branch(n_edges, n_branches, i->second, 
                                child_rid, child_mid, child_map, 
                                cvs, app, revisions, depth+1);

      // now apply same change set to parent_map, making parent_map == child_map
      apply_change_set(*cs, parent_map);
      parent_mid = child_mid;
      parent_rid = child_rid;
      ++n_edges;
    }
}

void 
import_cvs_repo(fs::path const & cvsroot, 
                app_state & app)
{
  N(!fs::exists(cvsroot / "CVSROOT"),
    F("%s appears to be a CVS repository root directory\n"
      "try importing a module instead, with 'cvs_import %s/<module_name>")
    % cvsroot.native_directory_string() % cvsroot.native_directory_string());
  
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    require_password(key, app);
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

  vector< pair<cvs_key, revision_set> > revisions;
  {
    ticker n_branches("finished branches", "b", 1);
    ticker n_edges("finished edges", "e", 1);
    transaction_guard guard(app.db);
    manifest_map root_manifest;
    manifest_id root_mid;
    revision_id root_rid; 

    import_states_by_branch(n_edges, n_branches, state, 
                            root_rid, root_mid,
                            root_manifest, cvs, app, revisions, 0);
    P(F("phase 2 (ancestry reconstruction) complete\n"));
    guard.commit();
  }
  
  {
    ticker n_revisions("written revisions", "r", 1);
    ui.set_tick_trailer("");
    transaction_guard guard(app.db);
    for (vector< pair<cvs_key, revision_set> >::const_iterator
           i = revisions.begin(); i != revisions.end(); ++i)
      {
        revision_id rid;
        calculate_ident(i->second, rid);
        if (! app.db.revision_exists(rid))
          app.db.put_revision(rid, i->second);
        store_auxiliary_certs(i->first, rid, app, cvs);
        ++n_revisions;
      }
    P(F("phase 3 (writing revisions) complete\n"));
    guard.commit();
  }
}

