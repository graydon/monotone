// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include "vector.hh"
#include <cstring> // memset

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include "lexical_cast.hh"
#include <boost/tokenizer.hpp>

#include "cert.hh"
#include "constants.hh"
#include "cycle_detector.hh"
#include "database.hh"
#include "file_io.hh"
#include "interner.hh"
#include "paths.hh"
#include "platform-wrapped.hh"
#include "project.hh"
#include "rcs_file.hh"
#include "revision.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "roster.hh"
#include "xdelta.hh"

using std::make_pair;
using std::map;
using std::multimap;
using std::out_of_range;
using std::pair;
using std::search;
using std::set;
using std::sscanf;
using std::stable_sort;
using std::stack;
using std::string;
using std::vector;

using boost::scoped_ptr;
using boost::shared_ptr;
using boost::lexical_cast;

// cvs history recording stuff

typedef unsigned long cvs_branchname;
typedef unsigned long cvs_author;
typedef unsigned long cvs_changelog;
typedef unsigned long cvs_version;
typedef unsigned long cvs_path;
typedef unsigned long cvs_tag;

struct cvs_history;

struct
cvs_commit
{
  cvs_commit(rcs_file const & r,
             string const & rcs_version,
             file_id const & ident,
             cvs_history & cvs);

  bool is_synthetic_branch_root;
  time_t time;
  bool alive;
  cvs_author author;
  cvs_changelog changelog;
  cvs_version version;
  cvs_path path;
  vector<cvs_tag> tags;

  bool operator<(cvs_commit const & other) const
  {
    return time < other.time;
  }
};

struct
cvs_branch
{
  bool has_a_branchpoint;
  bool has_a_commit;
  time_t last_branchpoint;
  time_t first_commit;

  map<cvs_path, cvs_version> live_at_beginning;
  vector<cvs_commit> lineage;

  cvs_branch()
    : has_a_branchpoint(false),
      has_a_commit(false),
      last_branchpoint(0),
      first_commit(0)
  {
  }

  void note_commit(time_t now)
  {
    if (!has_a_commit)
      {
        first_commit = now;
      }
    else
      {
        if (now < first_commit)
          first_commit = now;
      }
    has_a_commit = true;
  }

  void note_branchpoint(time_t now)
  {
    has_a_branchpoint = true;
    if (now > last_branchpoint)
      last_branchpoint = now;
  }

  time_t beginning() const
  {
    I(has_a_branchpoint || has_a_commit);
    if (has_a_commit)
      {
        I(first_commit != 0);
        return first_commit;
      }
    else
      {
        I(last_branchpoint != 0);
        return last_branchpoint;
      }
  }

  void append_commit(cvs_commit const & c)
  {
    I(c.time != 0);
    note_commit(c.time);
    lineage.push_back(c);
  }
};

struct
cvs_history
{

  interner<unsigned long> branch_interner;
  interner<unsigned long> author_interner;
  interner<unsigned long> changelog_interner;
  interner<unsigned long> file_version_interner;
  interner<unsigned long> path_interner;
  interner<unsigned long> tag_interner;

  // assume admin has foo:X.Y.0.N in it, then
  // this multimap contains entries of the form
  // X.Y   -> foo
  multimap<string, string> branchpoints;

  // and this map contains entries of the form
  // X.Y.N.1 -> foo
  map<string, string> branch_first_entries;

  // branch name -> branch
  map<string, shared_ptr<cvs_branch> > branches;
  shared_ptr<cvs_branch> trunk;

  // stack of branches we're injecting states into
  stack< shared_ptr<cvs_branch> > stk;
  stack< cvs_branchname > bstk;

  // tag -> time, revision
  //
  // used to resolve the *last* revision which has a given tag
  // applied; this is the revision which wins the tag.
  map<unsigned long, pair<time_t, revision_id> > resolved_tags;

  file_path curr_file;
  cvs_path curr_file_interned;

  string base_branch;

  ticker n_versions;
  ticker n_tree_branches;

  cvs_history();
  void set_filename(string const & file,
                    file_id const & ident);

  void index_branchpoint_symbols(rcs_file const & r);

  void push_branch(string const & branch_name, bool private_branch);
  void pop_branch();
};


static bool
is_sbr(shared_ptr<rcs_delta> dl,
       shared_ptr<rcs_deltatext> dt)
{

  // CVS abuses the RCS format a bit (ha!) when storing a file which
  // was only added on a branch: on the root of the branch there'll be
  // a commit with dead state, empty text, and a log message
  // containing the string "file foo was initially added on branch
  // bar". We recognize and ignore these cases, as they do not
  // "really" represent commits to be clustered together.

  if (dl->state != "dead")
    return false;

  if (!dt->text.empty())
    return false;

  string log_bit = "was initially added on branch";
  string::const_iterator i = search(dt->log.begin(),
                                    dt->log.end(),
                                    log_bit.begin(),
                                    log_bit.end());

  return i != dt->log.end();
}


cvs_commit::cvs_commit(rcs_file const & r,
                       string const & rcs_version,
                       file_id const & ident,
                       cvs_history & cvs)
{
  map<string, shared_ptr<rcs_delta> >::const_iterator delta =
    r.deltas.find(rcs_version);
  I(delta != r.deltas.end());

  map<string, shared_ptr<rcs_deltatext> >::const_iterator deltatext =
    r.deltatexts.find(rcs_version);
  I(deltatext != r.deltatexts.end());

  struct tm t;
  // We need to initialize t to all zeros, because strptime has a habit of
  // leaving bits of the data structure alone, letting garbage sneak into
  // our output.
  memset(&t, 0, sizeof(t));
  char const * dp = delta->second->date.c_str();
  L(FL("Calculating time of %s") % dp);
#ifdef HAVE_STRPTIME
  if (strptime(dp, "%y.%m.%d.%H.%M.%S", &t) == NULL)
    I(strptime(dp, "%Y.%m.%d.%H.%M.%S", &t) != NULL);
#else
  I(sscanf(dp, "%d.%d.%d.%d.%d.%d", &(t.tm_year), &(t.tm_mon),
           &(t.tm_mday), &(t.tm_hour), &(t.tm_min), &(t.tm_sec))==6);
  t.tm_mon--;
  // Apparently some RCS files have 2 digit years, others four; tm always
  // wants a 2 (or 3) digit year (years since 1900).
  if (t.tm_year > 1900)
    t.tm_year-=1900;
#endif
  time = mktime(&t);
  L(FL("= %i") % time);

  is_synthetic_branch_root = is_sbr(delta->second,
                                    deltatext->second);

  alive = delta->second->state != "dead";
  if (is_synthetic_branch_root)
    changelog = cvs.changelog_interner.intern("synthetic branch root changelog");
  else
    changelog = cvs.changelog_interner.intern(deltatext->second->log);
  author = cvs.author_interner.intern(delta->second->author);
  path = cvs.curr_file_interned;
  version = cvs.file_version_interner.intern(ident.inner()());

  typedef multimap<string,string>::const_iterator ity;
  pair<ity,ity> range = r.admin.symbols.equal_range(rcs_version);
  for (ity i = range.first; i != range.second; ++i)
    {
      if (i->first == rcs_version)
        {
          L(FL("version %s -> tag %s") % rcs_version % i->second);
          tags.push_back(cvs.tag_interner.intern(i->second));
        }
    }

}


// piece table stuff

struct piece;

struct
piece_store
{
  vector< shared_ptr<rcs_deltatext> > texts;
  void index_deltatext(shared_ptr<rcs_deltatext> const & dt,
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
piece_store::index_deltatext(shared_ptr<rcs_deltatext> const & dt,
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
  catch (out_of_range &)
    {
      throw oops("out_of_range while processing " + directive
                 + " with source.size() == "
                 + lexical_cast<string>(source.size())
                 + " and cursor == "
                 + lexical_cast<string>(cursor));
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
rcs_put_raw_file_edge(database & db,
                      file_id const & old_id,
                      file_id const & new_id,
                      delta const & del)
{
  if (old_id == new_id)
    {
      L(FL("skipping identity file edge"));
      return;
    }

  if (db.file_version_exists(old_id))
    {
      // we already have a way to get to this old version,
      // no need to insert another reconstruction path
      L(FL("existing path to %s found, skipping") % old_id);
    }
  else
    {
      I(db.file_or_manifest_base_exists(new_id, "files")
        || db.delta_exists(new_id.inner(), "file_deltas"));
      db.put_file_delta(old_id, new_id, file_delta(del));
    }
}


static void
insert_into_db(database & db, data const & curr_data,
               file_id const & curr_id,
               vector< piece > const & next_lines,
               data & next_data,
               file_id & next_id)
{
  // inserting into the DB
  // note: curr_lines is a "new" (base) version
  //       and next_lines is an "old" (derived) version.
  //       all storage edges go from new -> old.
  {
    string tmp;
    global_pieces.build_string(next_lines, tmp);
    next_data = data(tmp);
  }
  delta del;
  diff(curr_data, next_data, del);
  calculate_ident(file_data(next_data), next_id);
  rcs_put_raw_file_edge(db, next_id, curr_id, del);
}



/*

please read this exhaustingly long comment and understand it
before mucking with the branch inference logic.

we are processing a file version. a branch might begin here. if
the current version is X.Y, then there is a branch B starting
here iff there is a symbol in the admin section called X.Y.0.Z,
where Z is the branch number (or if there is a private branch
called X.Y.Z, which is either an import branch or some private
RCS cruft).

the version X.Y is then considered the branchpoint of B in the
current file. this does *not* mean that the CVS key -- an
abstraction representing whole-tree operations -- of X.Y is the
branchpoint across the CVS archive we're processing.

in fact, CVS does not record the occurrence of a branching
action (tag -b). we have no idea who executed that command and
when. what we know instead is the commit X.Y immediately
preceeding the branch -- CVS consideres this the branchpoint --
in this file's reduced view of history. we also know the first
commit X.Y.Z.1 inside the branch (which might not exist).

our old strategy was to consider all branches nested in a
hierarchy, which was a super-tree of all the branch trees in all
the CVS files in a repository. this involved considering X.Y as
the parent version of branch X.Y.Z, an selecting "the"
branchpoint connecting the two as the least CVS key X.Y.Z.1
committed inside the branch B.

this was a mistake, for two significant reasons.

first, some files do not *have* any commit inside the branch B,
only a branchpoint X.Y.0.Z. this branchpoint is actually the
last commit *before* the user branched, and could be a very old
commit, long before the branch was formed, so it is useless in
determining the branch structure.

second, some files do not have a branch B, or worse, have
branched into B from an "ancestor" branch A, where a different
file branches into B from a different ancestor branch C. in
other words, while there *is* a tree structure within the X.Y.Z
branches of each file, there is *no* shared tree structure
between the branch names across a repository. in one file A can
be an ancestor of B, in another file B can be an ancestor of A.

thus, we give up on establishing a hierarchy between branches
altogether. all branches exist in a flat namespace, and all are
direct descendents of the empty revision at the root of
history. each branchpoint symbol mentioned in the
administrative section of a file is considered the root of a new
lineage.

*/


static void
process_branch(database & db,
               string const & begin_version,
               vector< piece > const & begin_lines,
               data const & begin_data,
               file_id const & begin_id,
               rcs_file const & r,
               cvs_history & cvs)
{
  string curr_version = begin_version;
  scoped_ptr< vector< piece > > next_lines(new vector<piece>);
  scoped_ptr< vector< piece > > curr_lines(new vector<piece>
                                           (begin_lines.begin(),
                                            begin_lines.end()));
  data curr_data(begin_data), next_data;
  file_id curr_id(begin_id), next_id;

  while(! (r.deltas.find(curr_version) == r.deltas.end()))
    {
      L(FL("version %s has %d lines") % curr_version % curr_lines->size());

      cvs_commit curr_commit(r, curr_version, file_id(curr_id), cvs);
      if (!curr_commit.is_synthetic_branch_root)
        {
          cvs.stk.top()->append_commit(curr_commit);
          ++cvs.n_versions;
        }

      string next_version = r.deltas.find(curr_version)->second->next;

      if (! next_version.empty())
      {
         L(FL("following RCS edge %s -> %s") % curr_version % next_version);

         construct_version(*curr_lines, next_version, *next_lines, r);
         L(FL("constructed RCS version %s, inserting into database") %
           next_version);

         insert_into_db(db, curr_data, curr_id,
                        *next_lines, next_data, next_id);
      }

      // mark the beginning-of-branch time and state of this file if
      // we're at a branchpoint
      typedef multimap<string,string>::const_iterator ity;
      pair<ity,ity> range = cvs.branchpoints.equal_range(curr_version);
      if (range.first != cvs.branchpoints.end()
          && range.first->first == curr_version)
        {
          for (ity i = range.first; i != range.second; ++i)
            {
              cvs.push_branch(i->second, false);
              shared_ptr<cvs_branch> b = cvs.stk.top();
              if (curr_commit.alive)
                b->live_at_beginning[cvs.curr_file_interned] = curr_commit.version;
              b->note_branchpoint(curr_commit.time);
              cvs.pop_branch();
            }
        }


      // recursively follow any branch commits coming from the branchpoint
      shared_ptr<rcs_delta> curr_delta = r.deltas.find(curr_version)->second;
      for(vector<string>::const_iterator i = curr_delta->branches.begin();
          i != curr_delta->branches.end(); ++i)
        {
          string branch;
          data branch_data;
          file_id branch_id;
          vector< piece > branch_lines;
          bool priv = false;
          map<string, string>::const_iterator be = cvs.branch_first_entries.find(*i);

          if (be != cvs.branch_first_entries.end())
            branch = be->second;
          else
            priv = true;

          L(FL("following RCS branch %s = '%s'") % (*i) % branch);

          construct_version(*curr_lines, *i, branch_lines, r);
          insert_into_db(db, curr_data, curr_id,
                         branch_lines, branch_data, branch_id);

          cvs.push_branch(branch, priv);
          process_branch(db, *i, branch_lines, branch_data, branch_id, r, cvs);
          cvs.pop_branch();

          L(FL("finished RCS branch %s = '%s'") % (*i) % branch);
        }

      if (!r.deltas.find(curr_version)->second->next.empty())
        {
          // advance
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
import_rcs_file_with_cvs(database & db, string const & filename,
                         cvs_history & cvs)
{
  rcs_file r;
  L(FL("parsing RCS file %s") % filename);
  parse_rcs_file(filename, r);
  L(FL("parsed RCS file %s OK") % filename);

  {
    vector< piece > head_lines;
    I(r.deltatexts.find(r.admin.head) != r.deltatexts.end());
    I(r.deltas.find(r.admin.head) != r.deltas.end());

    file_id fid;
    file_data dat(r.deltatexts.find(r.admin.head)->second->text);
    calculate_ident(dat, fid);

    cvs.set_filename(filename, fid);
    cvs.index_branchpoint_symbols (r);
    db.put_file(fid, dat);

    {
      // create the head state in case it is a loner
      //       cvs_key k;
      //       shared_ptr<cvs_state> s;
      //       L(FL("noting head version %s : %s") % cvs.curr_file % r.admin.head);
      //       cvs.find_key_and_state (r, r.admin.head, k, s);
    }

    global_pieces.reset();
    global_pieces.index_deltatext(r.deltatexts.find(r.admin.head)->second, head_lines);
    process_branch(db, r.admin.head, head_lines, dat.inner(), fid, r, cvs);
    global_pieces.reset();
  }

  ui.set_tick_trailer("");
}

void
test_parse_rcs_file(system_path const & filename)
{
  cvs_history cvs;

  I(! filename.empty());
  assert_path_is_file(filename);

  P(F("parsing RCS file %s") % filename);
  rcs_file r;
  parse_rcs_file(filename.as_external(), r);
  P(F("parsed RCS file %s OK") % filename);
}


// CVS importing stuff follows


static void
split_version(string const & v, vector<string> & vs)
{
  vs.clear();
  boost::char_separator<char> sep(".");
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
  tokenizer tokens(v, sep);
  copy(tokens.begin(), tokens.end(), back_inserter(vs));
}

static void
join_version(vector<string> const & vs, string & v)
{
  v.clear();
  for (vector<string>::const_iterator i = vs.begin();
       i != vs.end(); ++i)
    {
      if (i != vs.begin())
        v += ".";
      v += *i;
    }
}

cvs_history::cvs_history() :
  n_versions("versions", "v", 1),
  n_tree_branches("branches", "b", 1)
{
}

void
cvs_history::set_filename(string const & file,
                          file_id const & ident)
{
  L(FL("importing file '%s'") % file);
  I(file.size() > 2);
  I(file.substr(file.size() - 2) == string(",v"));
  string ss = file;
  ui.set_tick_trailer(ss);
  ss.resize(ss.size() - 2);
  // remove Attic/ if present
  string::size_type last_slash=ss.rfind('/');
  if (last_slash!=string::npos && last_slash>=5
        && ss.substr(last_slash-5,6)=="Attic/")
     ss.erase(last_slash-5,6);
  curr_file = file_path_internal(ss);
  curr_file_interned = path_interner.intern(ss);
}

void cvs_history::index_branchpoint_symbols(rcs_file const & r)
{
  branchpoints.clear();
  branch_first_entries.clear();

  for (multimap<string, string>::const_iterator i =
         r.admin.symbols.begin(); i != r.admin.symbols.end(); ++i)
    {
      string const & num = i->first;
      string const & sym = i->second;

      vector<string> components;
      split_version(num, components);

      vector<string> first_entry_components;
      vector<string> branchpoint_components;

      if (components.size() > 2 &&
          (components.size() % 2 == 1))
        {
          // this is a "vendor" branch
          //
          // such as "1.1.1", where "1.1" is the branchpoint and
          // "1.1.1.1" will be the first commit on it.

          first_entry_components = components;
          first_entry_components.push_back("1");

          branchpoint_components = components;
          branchpoint_components.erase(branchpoint_components.end() - 1,
                                       branchpoint_components.end());

        }

      else if (components.size() > 2 &&
               (components.size() % 2 == 0) &&
               components[components.size() - 2] == string("0"))
        {
          // this is a "normal" branch
          //
          // such as "1.3.0.2", where "1.3" is the branchpoint and
          // "1.3.2.1"

          first_entry_components = components;
          first_entry_components[first_entry_components.size() - 2]
            = first_entry_components[first_entry_components.size() - 1];
          first_entry_components[first_entry_components.size() - 1]
            = string("1");

          branchpoint_components = components;
          branchpoint_components.erase(branchpoint_components.end() - 2,
                                       branchpoint_components.end());
        }

      string first_entry_version;
      join_version(first_entry_components, first_entry_version);

      L(FL("first version in branch %s would be %s")
        % sym % first_entry_version);
      branch_first_entries.insert(make_pair(first_entry_version, sym));

      string branchpoint_version;
      join_version(branchpoint_components, branchpoint_version);

      L(FL("file branchpoint for %s at %s") % sym % branchpoint_version);
      branchpoints.insert(make_pair(branchpoint_version, sym));
    }
}



void
cvs_history::push_branch(string const & branch_name, bool private_branch)
{
  shared_ptr<cvs_branch> branch;

  string bname = base_branch + "." + branch_name;
  I(stk.size() > 0);

  if (private_branch)
    {
      branch = shared_ptr<cvs_branch>(new cvs_branch());
      stk.push(branch);
      bstk.push(branch_interner.intern(""));
      return;
    }
  else
    {
      map<string, shared_ptr<cvs_branch> >::const_iterator b = branches.find(bname);
      if (b == branches.end())
        {
          branch = shared_ptr<cvs_branch>(new cvs_branch());
          branches.insert(make_pair(bname, branch));
          ++n_tree_branches;
        }
      else
        branch = b->second;

      stk.push(branch);
      bstk.push(branch_interner.intern(bname));
    }
}

void
cvs_history::pop_branch()
{
  I(stk.size() > 1);
  stk.pop();
  bstk.pop();
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
    string file = path.as_external();
    if (file.substr(file.size() - 2) == string(",v"))
      {
        try
          {
            import_rcs_file_with_cvs(db, file, cvs);
          }
        catch (oops const & o)
          {
            W(F("error reading RCS file %s: %s") % file % o.what());
          }
      }
    else
      L(FL("skipping non-RCS file %s") % file);
  }
  virtual ~cvs_tree_walker() {}
};




//
// our task here is to produce a sequence of revision descriptions
// from the per-file commit records we have. we do this by rolling
// forwards through the temporally sorted file-commit list
// accumulating file-commits into revisions and flushing the
// revisions when we feel they are "complete".
//
// revisions have to have a time associated with them. this time
// will be the first time of any commit associated with the
// revision. they have an author and a changelog, which is shared
// by all the file-commits in the revision.
//
// there might be multiple revisions overlapping in time. this is
// legal wrt. CVS. we keep a set, and search all members of the set
// for the best match.
//
// consider this situation of overlapping revisions:
//
//    +---------------+   +---------------+   +---------------+
//    | rev #1 @ 0011 |   | rev #2 @ 0012 |   | rev #3 @ 0013 |
//    |~~~~~~~~~~~~~~~|   |~~~~~~~~~~~~~~~|   |~~~~~~~~~~~~~~~|
//    | patch foo.txt |   | patch bar.txt |   | patch baz.txt |
//    +---------------+   +---------------+   +---------------+
//
// suppose you have this situation and you run across a "patch
// bar.txt" commit at timestamp 0014. what do you do?
//
// - you know that rev #2 cannot accept this commit, simply because
//   two commits on the same file makes *two* revisions, not one.
//
// - perhaps rev #3 could accept it; after all, it could be that the
//   commit associated with rev #2 released its commit lock, and the
//   commit associated with rev #3 quickly updated and committed at
//   0013, finishing off at 0014.
//
// - can rev #1 accept it? no. because CVS calcualted the version it
//   expected to see in bar.txt before calling up the server, when
//   committing rev #1. the version it expected to see was the version
//   in bar.txt *before* time 0012; that is, before rev #2 had any affect
//   on bar.txt. when it contacted the server, the commit associated
//   with rev #1 would have aborted if it had seen any other number.
//   so rev #1 could not start before an edit to bar.txt and then
//   include its own edit to bar.txt.
//
// so we have only one case where bar.txt can be accepted. if the
// commit is not accepted into a legal rev (outside the window,
// wrong changelog/author) it starts a new revision.
//
// as we scan forwards, if we hit timestamps which lie beyond rev #n's
// window, we flush rev #n.
//
// if there are multiple coincident and legal revs to direct a
// commit to (all with the same author/changelog), we direct the
// commit to the rev with the closest initial timestamp. that is,
// the *latest* beginning time.

struct
cvs_cluster
{
  time_t first_time;
  cvs_author author;
  cvs_changelog changelog;
  set<cvs_tag> tags;

  cvs_cluster(time_t t,
              cvs_author a,
              cvs_changelog c)
    : first_time(t),
      author(a),
      changelog(c)
  {}

  struct entry
  {
    bool live;
    cvs_version version;
    time_t time;
    entry(bool l, cvs_version v, time_t t)
      : live(l),
        version(v),
        time(t)
    {}
  };

  typedef map<cvs_path, entry> entry_map;
  entry_map entries;
};


struct
cluster_consumer
{
  cvs_history & cvs;
  key_store & keys;
  project_t & project;

  string const & branchname;
  cvs_branch const & branch;
  set<file_path> created_dirs;
  map<cvs_path, cvs_version> live_files;
  ticker & n_revisions;

  struct prepared_revision
  {
    prepared_revision(revision_id i,
                      shared_ptr<revision_t> r,
                      cvs_cluster const & c);
    revision_id rid;
    shared_ptr<revision_t> rev;
    time_t time;
    cvs_author author;
    cvs_changelog changelog;
    vector<cvs_tag> tags;
  };

  vector<prepared_revision> preps;

  roster_t ros;
  temp_node_id_source nis;
  editable_roster_base editable_ros;
  revision_id parent_rid, child_rid;

  cluster_consumer(project_t & project,
                   key_store & keys,
                   cvs_history & cvs,
                   string const & branchname,
                   cvs_branch const & branch,
                   ticker & n_revs);

  void consume_cluster(cvs_cluster const & c);
  void add_missing_parents(file_path const & sp, cset & cs);
  void build_cset(cvs_cluster const & c, cset & cs);
  void store_auxiliary_certs(prepared_revision const & p);
  void store_revisions();
};

typedef shared_ptr<cvs_cluster>
cluster_ptr;

struct
cluster_ptr_lt
{
  bool operator()(cluster_ptr const & a,
                  cluster_ptr const & b) const
  {
    return a->first_time < b->first_time;
  }
};

typedef set<cluster_ptr, cluster_ptr_lt>
cluster_set;

void
import_branch(project_t & project,
              key_store & keys,
              cvs_history & cvs,
              string const & branchname,
              shared_ptr<cvs_branch> const & branch,
              ticker & n_revs)
{
  cluster_set clusters;
  cluster_consumer cons(project, keys, cvs, branchname, *branch, n_revs);
  unsigned long commits_remaining = branch->lineage.size();

  // step 1: sort the lineage
  stable_sort(branch->lineage.begin(), branch->lineage.end());

  for (vector<cvs_commit>::const_iterator i = branch->lineage.begin();
       i != branch->lineage.end(); ++i)
    {
      commits_remaining--;

      L(FL("examining next commit [t:%d] [p:%s] [a:%s] [c:%s]")
        % i->time
        % cvs.path_interner.lookup(i->path)
        % cvs.author_interner.lookup(i->author)
        % cvs.changelog_interner.lookup(i->changelog));

      // step 2: expire all clusters from the beginning of the set which
      // have passed the window size
      while (!clusters.empty())
        {
          cluster_set::iterator j = clusters.begin();
          if ((*j)->first_time + constants::cvs_window < i->time)
            {
              L(FL("expiring cluster"));
              cons.consume_cluster(**j);
              clusters.erase(j);
            }
          else
            break;
        }

      // step 3: find the last still-live cluster to have touched this
      // file
      time_t time_of_last_cluster_touching_this_file = 0;

      unsigned clu = 0;
      for (cluster_set::const_iterator j = clusters.begin();
           j != clusters.end(); ++j)
        {
          L(FL("examining cluster %d to see if it touched %d")
            % clu++
            % i->path);

          cvs_cluster::entry_map::const_iterator k = (*j)->entries.find(i->path);
          if ((k != (*j)->entries.end())
              && (k->second.time > time_of_last_cluster_touching_this_file))
            {
              L(FL("found cluster touching %d: [t:%d] [a:%d] [c:%d]")
                % i->path
                % (*j)->first_time
                % (*j)->author
                % (*j)->changelog);
              time_of_last_cluster_touching_this_file = (*j)->first_time;
            }
        }
      L(FL("last modification time is %d")
        % time_of_last_cluster_touching_this_file);

      // step 4: find a cluster which starts on or after the
      // last_modify_time, which doesn't modify the file in question,
      // and which contains the same author and changelog as our
      // commit
      cluster_ptr target;
      for (cluster_set::const_iterator j = clusters.begin();
           j != clusters.end(); ++j)
        {
          if (((*j)->first_time >= time_of_last_cluster_touching_this_file)
              && ((*j)->author == i->author)
              && ((*j)->changelog == i->changelog)
              && ((*j)->entries.find(i->path) == (*j)->entries.end()))
            {
              L(FL("picked existing cluster [t:%d] [a:%d] [c:%d]")
                % (*j)->first_time
                % (*j)->author
                % (*j)->changelog);

              target = (*j);
            }
        }

      // if we're still not finding an active cluster,
      // this is probably the first commit in it. make
      // a new one.
      if (!target)
        {
          L(FL("building new cluster [t:%d] [a:%d] [c:%d]")
            % i->time
            % i->author
            % i->changelog);

          target = cluster_ptr(new cvs_cluster(i->time,
                                               i->author,
                                               i->changelog));
          clusters.insert(target);
        }

      I(target);
      target->entries.insert(make_pair(i->path,
                                       cvs_cluster::entry(i->alive,
                                                          i->version,
                                                          i->time)));
      for (vector<cvs_tag>::const_iterator j = i->tags.begin();
           j != i->tags.end(); ++j)
        {
          target->tags.insert(*j);
        }
    }


  // now we are done this lineage; flush all remaining clusters
  L(FL("finished branch commits, writing all pending clusters"));
  while (!clusters.empty())
    {
      cons.consume_cluster(**clusters.begin());
      clusters.erase(clusters.begin());
    }
  L(FL("finished writing pending clusters"));

  cons.store_revisions();

}

void
import_cvs_repo(project_t & project,
                key_store & keys,
                system_path const & cvsroot,
                branch_name const & branchname)

{
  N(!directory_exists(cvsroot / "CVSROOT"),
    F("%s appears to be a CVS repository root directory\n"
      "try importing a module instead, with 'cvs_import %s/<module_name>")
    % cvsroot % cvsroot);

  cvs_history cvs;

  cvs.base_branch = branchname();

  // push the trunk
  cvs.trunk = shared_ptr<cvs_branch>(new cvs_branch());
  cvs.stk.push(cvs.trunk);
  cvs.bstk.push(cvs.branch_interner.intern(cvs.base_branch));

  {
    transaction_guard guard(project.db);
    cvs_tree_walker walker(cvs, project.db);
    project.db.ensure_open();
    change_current_working_dir(cvsroot);
    walk_tree(file_path(), walker);
    guard.commit();
  }

  I(cvs.stk.size() == 1);

  ticker n_revs(_("revisions"), "r", 1);

  while (cvs.branches.size() > 0)
    {
      transaction_guard guard(project.db);
      map<string, shared_ptr<cvs_branch> >::const_iterator i = cvs.branches.begin();
      string branchname = i->first;
      shared_ptr<cvs_branch> branch = i->second;
      L(FL("branch %s has %d entries") % branchname % branch->lineage.size());
      import_branch(project, keys, cvs, branchname, branch, n_revs);

      // free up some memory
      cvs.branches.erase(branchname);
      guard.commit();
    }

  {
    transaction_guard guard(project.db);
    L(FL("trunk has %d entries") % cvs.trunk->lineage.size());
    import_branch(project, keys, cvs, cvs.base_branch, cvs.trunk, n_revs);
    guard.commit();
  }

  // now we have a "last" rev for each tag
  {
    ticker n_tags(_("tags"), "t", 1);
    transaction_guard guard(project.db);
    for (map<unsigned long, pair<time_t, revision_id> >::const_iterator i = cvs.resolved_tags.begin();
         i != cvs.resolved_tags.end(); ++i)
      {
        string tag = cvs.tag_interner.lookup(i->first);
        ui.set_tick_trailer("marking tag " + tag);
        project.put_tag(keys, i->second.second, tag);
        ++n_tags;
      }
    guard.commit();
  }
}

cluster_consumer::cluster_consumer(project_t & project,
                                   key_store & keys,
                                   cvs_history & cvs,
                                   string const & branchname,
                                   cvs_branch const & branch,
                                   ticker & n_revs)
  : cvs(cvs),
    keys(keys),
    project(project),
    branchname(branchname),
    branch(branch),
    n_revisions(n_revs),
    editable_ros(ros, nis)
{
  if (!branch.live_at_beginning.empty())
    {
      cvs_author synthetic_author =
        cvs.author_interner.intern("cvs_import");

      cvs_changelog synthetic_cl =
        cvs.changelog_interner.intern("beginning of branch "
                                      + branchname);

      time_t synthetic_time = branch.beginning();
      cvs_cluster initial_cluster(synthetic_time,
                                  synthetic_author,
                                  synthetic_cl);

      L(FL("initial cluster on branch %s has %d live entries") %
        branchname % branch.live_at_beginning.size());

      for (map<cvs_path, cvs_version>::const_iterator i = branch.live_at_beginning.begin();
           i != branch.live_at_beginning.end(); ++i)
        {
          cvs_cluster::entry e(true, i->second, synthetic_time);
          L(FL("initial cluster contains %s at %s") %
            cvs.path_interner.lookup(i->first) %
            cvs.file_version_interner.lookup(i->second));
          initial_cluster.entries.insert(make_pair(i->first, e));
        }
      consume_cluster(initial_cluster);
    }
}

cluster_consumer::prepared_revision::prepared_revision(revision_id i,
                                                       shared_ptr<revision_t> r,
                                                       cvs_cluster const & c)
  : rid(i),
    rev(r),
    time(c.first_time),
    author(c.author),
    changelog(c.changelog)
{
  for (set<cvs_tag>::const_iterator i = c.tags.begin();
       i != c.tags.end(); ++i)
    {
      tags.push_back(*i);
    }
}


void
cluster_consumer::store_revisions()
{
  for (vector<prepared_revision>::const_iterator i = preps.begin();
       i != preps.end(); ++i)
    if (project.db.put_revision(i->rid, *(i->rev)))
      {
        store_auxiliary_certs(*i);
        ++n_revisions;
      }
}

void
cluster_consumer::store_auxiliary_certs(prepared_revision const & p)
{
  for (vector<cvs_tag>::const_iterator i = p.tags.begin();
       i != p.tags.end(); ++i)
    {
      map<unsigned long, pair<time_t, revision_id> >::const_iterator j
        = cvs.resolved_tags.find(*i);

      if (j != cvs.resolved_tags.end())
        {
          if (j->second.first < p.time)
            {
              // move the tag forwards
              cvs.resolved_tags.erase(*i);
              cvs.resolved_tags.insert(make_pair(*i, make_pair(p.time, p.rid)));
            }
        }
      else
        {
          cvs.resolved_tags.insert(make_pair(*i, make_pair(p.time, p.rid)));
        }
    }

  project.put_standard_certs(keys, p.rid,
                             branch_name(branchname),
                             utf8(cvs.changelog_interner.lookup(p.changelog)),
                             date_t::from_unix_epoch(p.time),
                             cvs.author_interner.lookup(p.author));
}

void
cluster_consumer::add_missing_parents(file_path const & path, cset & cs)
{
  if (created_dirs.find(path) != created_dirs.end())
    return;

  if (!path.empty())
    add_missing_parents(path.dirname(), cs);

  safe_insert(created_dirs, path);
  safe_insert(cs.dirs_added, path);
}

void
cluster_consumer::build_cset(cvs_cluster const & c,
                             cset & cs)
{
  for (cvs_cluster::entry_map::const_iterator i = c.entries.begin();
       i != c.entries.end(); ++i)
    {
      file_path pth = file_path_internal(cvs.path_interner.lookup(i->first));

      file_id fid(cvs.file_version_interner.lookup(i->second.version));
      if (i->second.live)
        {
          map<cvs_path, cvs_version>::const_iterator e = live_files.find(i->first);
          if (e == live_files.end())
            {
              add_missing_parents(pth.dirname(), cs);
              L(FL("adding entry state '%s' on '%s'")
                % fid % pth);
              safe_insert(cs.files_added, make_pair(pth, fid));
              live_files[i->first] = i->second.version;
            }
          else if (e->second != i->second.version)
            {
              file_id old_fid(cvs.file_version_interner.lookup(e->second));
              L(FL("applying state delta on '%s' : '%s' -> '%s'")
                % pth
                % old_fid
                % fid);
              safe_insert(cs.deltas_applied,
                          make_pair(pth, make_pair(old_fid, fid)));
              live_files[i->first] = i->second.version;
            }
        }
      else
        {
          map<cvs_path, cvs_version>::const_iterator e = live_files.find(i->first);
          if (e != live_files.end())
            {
              L(FL("deleting entry state '%s' on '%s'")
                % fid % pth);
              safe_insert(cs.nodes_deleted, pth);
              live_files.erase(i->first);
            }
        }
    }
}

void
cluster_consumer::consume_cluster(cvs_cluster const & c)
{
  // we should never have an empty cluster; it's *possible* to have
  // an empty changeset (say on a vendor import) but every cluster
  // should have been created by at least one file commit, even
  // if the commit made no changes. it's a logical inconsistency if
  // you have an empty cluster.
  I(!c.entries.empty());

  shared_ptr<revision_t> rev(new revision_t());
  shared_ptr<cset> cs(new cset());
  build_cset(c, *cs);

  cs->apply_to(editable_ros);
  manifest_id child_mid;
  calculate_ident(ros, child_mid);
  rev->made_for = made_for_database;
  rev->new_manifest = child_mid;
  rev->edges.insert(make_pair(parent_rid, cs));
  calculate_ident(*rev, child_rid);

  preps.push_back(prepared_revision(child_rid, rev, c));

  parent_rid = child_rid;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
