// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/tuple/tuple.hpp>

#include "app_state.hh"
#include "basic_io.hh"
#include "cert.hh"
#include "cmd.hh"
#include "commands.hh"
#include "constants.hh"
#include "keys.hh"
#include "packet.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "globish.hh"
#include "charset.hh"
#include "safe_map.hh"

using std::allocator;
using std::basic_ios;
using std::basic_stringbuf;
using std::char_traits;
using std::inserter;
using std::make_pair;
using std::map;
using std::multimap;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::sort;
using std::streamsize;
using std::string;
using std::vector;


// Name: heads
// Arguments:
//   1: branch name (optional, default branch is used if non-existant)
// Added in: 0.0
// Purpose: Prints the heads of the given branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If the branch does not exist, prints nothing.  (There are
//   no heads.)
AUTOMATE(heads, N_("[BRANCH]"), options::opts::none)
{
  N(args.size() < 2,
    F("wrong argument count"));

  if (args.size() ==1 ) {
    // branchname was explicitly given, use that
    app.opts.branchname = branch_name(idx(args, 0)());
  }
  set<revision_id> heads;
  app.get_project().get_branch_heads(app.opts.branchname, heads);
  for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    output << (*i).inner()() << '\n';
}

// Name: ancestors
// Arguments:
//   1 or more: revision ids
// Added in: 0.2
// Purpose: Prints the ancestors (exclusive) of the given revisions
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
AUTOMATE(ancestors, N_("REV1 [REV2 [REV3 [...]]]"), options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));
  
  set<revision_id> ancestors;
  vector<revision_id> frontier;
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      frontier.push_back(rid);
    }
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if(!null_id(rid)) {
        set<revision_id> parents;
        app.db.get_revision_parents(rid, parents);
        for (set<revision_id>::const_iterator i = parents.begin();
             i != parents.end(); ++i)
          {
            if (ancestors.find(*i) == ancestors.end())
              {
                frontier.push_back(*i);
                ancestors.insert(*i);
              }
          }
      }
    }
  for (set<revision_id>::const_iterator i = ancestors.begin();
       i != ancestors.end(); ++i)
    if (!null_id(*i))
      output << (*i).inner()() << '\n';
}


// Name: descendents
// Arguments:
//   1 or more: revision ids
// Added in: 0.1
// Purpose: Prints the descendents (exclusive) of the given revisions
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
AUTOMATE(descendents, N_("REV1 [REV2 [REV3 [...]]]"), options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));

  set<revision_id> descendents;
  vector<revision_id> frontier;
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      frontier.push_back(rid);
    }
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      set<revision_id> children;
      app.db.get_revision_children(rid, children);
      for (set<revision_id>::const_iterator i = children.begin();
           i != children.end(); ++i)
        {
          if (descendents.find(*i) == descendents.end())
            {
              frontier.push_back(*i);
              descendents.insert(*i);
            }
        }
    }
  for (set<revision_id>::const_iterator i = descendents.begin();
       i != descendents.end(); ++i)
    output << (*i).inner()() << '\n';
}


// Name: erase_ancestors
// Arguments:
//   0 or more: revision ids
// Added in: 0.1
// Purpose: Prints all arguments, except those that are an ancestor of some
//   other argument.  One way to think about this is that it prints the
//   minimal elements of the given set, under the ordering imposed by the
//   "child of" relation.  Another way to think of it is if the arguments were
//   a branch, then we print the heads of that branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
AUTOMATE(erase_ancestors, N_("[REV1 [REV2 [REV3 [...]]]]"), options::opts::none)
{
  set<revision_id> revs;
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      revs.insert(rid);
    }
  erase_ancestors(revs, app);
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    output << (*i).inner()() << '\n';
}

// Name: attributes
// Arguments:
//   1: file name
// Added in: 1.0
// Purpose: Prints all attributes for a file
// Output format: basic_io formatted output, each attribute has its own stanza:
//
// 'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1" currently)
//         occurs: exactly once
// 'attr'
//         represents an attribute entry
//         format: ('attr', name, value), ('state', [unchanged|changed|added|dropped])
//         occurs: zero or more times
//
// Error conditions: If the file name has no attributes, prints only the 
//                   format version, if the file is unknown, escalates
AUTOMATE(attributes, N_("FILE"), options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));

  // this command requires a workspace to be run on
  app.require_workspace();

  // retrieve the path
  split_path path;
  file_path_external(idx(args,0)).split(path);

  roster_t base, current;
  parent_map parents;
  temp_node_id_source nis;

  // get the base and the current roster of this workspace
  app.work.get_current_roster_shape(current, app.db, nis);
  app.work.get_parent_rosters(parents, app.db);
  N(parents.size() == 1,
    F("this command can only be used in a single-parent workspace"));
  base = parent_roster(parents.begin());

  // escalate if the given path is unknown to the current roster
  N(current.has_node(path),
    F("file %s is unknown to the current workspace") % path);

  // create the printer
  basic_io::printer pr;
  
  // print the format version
  basic_io::stanza st;
  st.push_str_pair(basic_io::syms::format_version, "1");
  pr.print_stanza(st);
    
  // the current node holds all current attributes (unchanged and new ones)
  node_t n = current.get_node(path);
  for (full_attr_map_t::const_iterator i = n->attrs.begin(); 
       i != n->attrs.end(); ++i)
  {
    std::string value(i->second.second());
    std::string state("unchanged");
    
    // if if the first value of the value pair is false this marks a
    // dropped attribute
    if (!i->second.first)
      {
        // if the attribute is dropped, we should have a base roster
        // with that node. we need to check that for the attribute as well
        // because if it is dropped there as well it was already deleted
        // in any previous revision
        I(base.has_node(path));
        
        node_t prev_node = base.get_node(path);
        
        // find the attribute in there
        full_attr_map_t::const_iterator j = prev_node->attrs.find(i->first);
        I(j != prev_node->attrs.end());
        
        // was this dropped before? then ignore it
        if (!j->second.first) { continue; }
        
        state = "dropped";
        // output the previous (dropped) value later
        value = j->second.second();
      }
    // this marks either a new or an existing attribute
    else
      {
        if (base.has_node(path))
          {
            node_t prev_node = base.get_node(path);
            full_attr_map_t::const_iterator j = 
              prev_node->attrs.find(i->first);
            // attribute not found? this is new
            if (j == prev_node->attrs.end())
              {
                state = "added";
              }
            // check if this attribute has been changed 
            // (dropped and set again)
            else if (i->second.second() != j->second.second())
              {
                state = "changed";
              }
                
          }
        // its added since the whole node has been just added
        else
          {
            state = "added";
          }
      }
      
    basic_io::stanza st;
    st.push_str_triple(basic_io::syms::attr, i->first(), value);
    st.push_str_pair(symbol("state"), state);
    pr.print_stanza(st);
  }
  
  // print the output  
  output.write(pr.buf.data(), pr.buf.size());
}

// Name: toposort
// Arguments:
//   0 or more: revision ids
// Added in: 0.1
// Purpose: Prints all arguments, topologically sorted.  I.e., if A is an
//   ancestor of B, then A will appear before B in the output list.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revisions are printed in topologically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
AUTOMATE(toposort, N_("[REV1 [REV2 [REV3 [...]]]]"), options::opts::none)
{
  set<revision_id> revs;
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      revs.insert(rid);
    }
  vector<revision_id> sorted;
  toposort(revs, sorted, app);
  for (vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << (*i).inner()() << '\n';
}

// Name: ancestry_difference
// Arguments:
//   1: a revision id
//   0 or more further arguments: also revision ids
// Added in: 0.1
// Purpose: Prints all ancestors of the first revision A, that are not also
//   ancestors of the other revision ids, the "Bs".  For purposes of this
//   command, "ancestor" is an inclusive term; that is, A is an ancestor of
//   one of the Bs, it will not be printed, but otherwise, it will be; and
//   none of the Bs will ever be printed.  If A is a new revision, and Bs are
//   revisions that you have processed before, then this command tells you
//   which revisions are new since then.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revisions are printed in topologically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
AUTOMATE(ancestry_difference, N_("NEW_REV [OLD_REV1 [OLD_REV2 [...]]]"), options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));
    
  revision_id a;
  set<revision_id> bs;
  vector<utf8>::const_iterator i = args.begin();
  a = revision_id((*i)());
  N(app.db.revision_exists(a), F("No such revision %s") % a);
  for (++i; i != args.end(); ++i)
    {
      revision_id b((*i)());
      N(app.db.revision_exists(b), F("No such revision %s") % b);
      bs.insert(b);
    }
  set<revision_id> ancestors;
  ancestry_difference(a, bs, ancestors, app);

  vector<revision_id> sorted;
  toposort(ancestors, sorted, app);
  for (vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << (*i).inner()() << '\n';
}

// Name: leaves
// Arguments:
//   None
// Added in: 0.1
// Purpose: Prints the leaves of the revision graph, i.e., all revisions that
//   have no children.  This is similar, but not identical to the
//   functionality of 'heads', which prints every revision in a branch, that
//   has no descendents in that branch.  If every revision in the database was
//   in the same branch, then they would be identical.  Generally, every leaf
//   is the head of some branch, but not every branch head is a leaf.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: None.
AUTOMATE(leaves, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  // this might be more efficient in SQL, but for now who cares.
  set<revision_id> leaves;
  app.db.get_revision_ids(leaves);
  multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  for (multimap<revision_id, revision_id>::const_iterator
         i = graph.begin(); i != graph.end(); ++i)
    leaves.erase(i->first);
  for (set<revision_id>::const_iterator i = leaves.begin();
       i != leaves.end(); ++i)
    output << (*i).inner()() << '\n';
}

// Name: parents
// Arguments:
//   1: a revision id
// Added in: 0.2
// Purpose: Prints the immediate ancestors of the given revision, i.e., the
//   parents.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If the revision does not exist, prints nothing to stdout,
//   prints an error message to stderr, and exits with status 1.
AUTOMATE(parents, N_("REV"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));
  
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> parents;
  app.db.get_revision_parents(rid, parents);
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << '\n';
}

// Name: children
// Arguments:
//   1: a revision id
// Added in: 0.2
// Purpose: Prints the immediate descendents of the given revision, i.e., the
//   children.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If the revision does not exist, prints nothing to stdout,
//   prints an error message to stderr, and exits with status 1.
AUTOMATE(children, N_("REV"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));
  
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> children;
  app.db.get_revision_children(rid, children);
  for (set<revision_id>::const_iterator i = children.begin();
       i != children.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << '\n';
}

// Name: graph
// Arguments:
//   None
// Added in: 0.2
// Purpose: Prints out the complete ancestry graph of this database.
// Output format:
//   Each line begins with a revision id.  Following this are zero or more
//   space-prefixed revision ids.  Each revision id after the first is a
//   parent (in the sense of 'automate parents') of the first.  For instance,
//   the following are valid lines:
//     07804171823d963f78d6a0ff1763d694dd74ff40
//     07804171823d963f78d6a0ff1763d694dd74ff40 79d755c197e54dd3db65751d3803833d4cbf0d01
//     07804171823d963f78d6a0ff1763d694dd74ff40 79d755c197e54dd3db65751d3803833d4cbf0d01 a02e7a1390e3e4745c31be922f03f56450c13dce
//   The first would indicate that 07804171823d963f78d6a0ff1763d694dd74ff40
//   was a root node; the second would indicate that it had one parent, and
//   the third would indicate that it had two parents, i.e., was a merge.
//
//   The output as a whole is alphabetically sorted; additionally, the parents
//   within each line are alphabetically sorted.
// Error conditions: None.
AUTOMATE(graph, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  multimap<revision_id, revision_id> edges_mmap;
  map<revision_id, set<revision_id> > child_to_parents;

  app.db.get_revision_ancestry(edges_mmap);

  for (multimap<revision_id, revision_id>::const_iterator i = edges_mmap.begin();
       i != edges_mmap.end(); ++i)
    {
      if (child_to_parents.find(i->second) == child_to_parents.end())
        child_to_parents.insert(make_pair(i->second, set<revision_id>()));
      if (null_id(i->first))
        continue;
      map<revision_id, set<revision_id> >::iterator
        j = child_to_parents.find(i->second);
      I(j->first == i->second);
      j->second.insert(i->first);
    }

  for (map<revision_id, set<revision_id> >::const_iterator
         i = child_to_parents.begin();
       i != child_to_parents.end(); ++i)
    {
      output << (i->first).inner()();
      for (set<revision_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        output << ' ' << (*j).inner()();
      output << '\n';
    }
}

// Name: select
// Arguments:
//   1: selector
// Added in: 0.2
// Purpose: Prints all the revisions that match the given selector.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: None.
AUTOMATE(select, N_("SELECTOR"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(args[0](), app));

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  for (set<string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    output << *i << '\n';
}

// consider a changeset with the following
//
// deletions
// renames from to
// additions
//
// pre-state  corresponds to deletions and the "from" side of renames
// post-state corresponds to the "to" side of renames and additions
// node-state corresponds to the state of the node with the given name
//
// pre/post state are related to the path rearrangement in _MTN/work
// node state is related to the details of the resulting path

struct inventory_item
{
  // pre/post rearrangement state
  enum pstate
    { UNCHANGED_PATH, ADDED_PATH, DROPPED_PATH, RENAMED_PATH }
    pre_state, post_state;

  enum nstate
    { UNCHANGED_NODE, PATCHED_NODE, MISSING_NODE,
      UNKNOWN_NODE, IGNORED_NODE }
    node_state;

  size_t pre_id, post_id;

  inventory_item():
    pre_state(UNCHANGED_PATH), post_state(UNCHANGED_PATH),
    node_state(UNCHANGED_NODE),
    pre_id(0), post_id(0) {}
};

typedef map<split_path, inventory_item> inventory_map;
typedef map<split_path, split_path> rename_map; // this might be good in cset.hh
typedef map<split_path, file_id> addition_map;  // ditto

static void
inventory_pre_state(inventory_map & inventory,
                    path_set const & paths,
                    inventory_item::pstate pre_state,
                    size_t rename_id)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(FL("%d %d %s") % inventory[*i].pre_state % pre_state % file_path(*i));
      I(inventory[*i].pre_state == inventory_item::UNCHANGED_PATH);
      inventory[*i].pre_state = pre_state;
      if (rename_id != 0)
        {
          I(inventory[*i].pre_id == 0);
          inventory[*i].pre_id = rename_id;
        }
    }
}

static void
inventory_post_state(inventory_map & inventory,
                     path_set const & paths,
                     inventory_item::pstate post_state,
                     size_t rename_id)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(FL("%d %d %s") % inventory[*i].post_state
        % post_state % file_path(*i));
      I(inventory[*i].post_state == inventory_item::UNCHANGED_PATH);
      inventory[*i].post_state = post_state;
      if (rename_id != 0)
        {
          I(inventory[*i].post_id == 0);
          inventory[*i].post_id = rename_id;
        }
    }
}

static void
inventory_node_state(inventory_map & inventory,
                     path_set const & paths,
                     inventory_item::nstate node_state)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(FL("%d %d %s") % inventory[*i].node_state
        % node_state % file_path(*i));
      I(inventory[*i].node_state == inventory_item::UNCHANGED_NODE);
      inventory[*i].node_state = node_state;
    }
}

static void
inventory_renames(inventory_map & inventory,
                  rename_map const & renames)
{
  path_set old_name;
  path_set new_name;

  static size_t rename_id = 1;

  for (rename_map::const_iterator i = renames.begin();
       i != renames.end(); i++)
    {
      old_name.clear();
      new_name.clear();

      old_name.insert(i->first);
      new_name.insert(i->second);

      inventory_pre_state(inventory, old_name,
                          inventory_item::RENAMED_PATH, rename_id);
      inventory_post_state(inventory, new_name,
                           inventory_item::RENAMED_PATH, rename_id);

      rename_id++;
    }
}

static void
extract_added_file_paths(addition_map const & additions, path_set & paths)
{
  for (addition_map::const_iterator i = additions.begin();
       i != additions.end(); ++i)
    {
      paths.insert(i->first);
    }
}


// Name: inventory
// Arguments: none
// Added in: 1.0

// Purpose: Prints a summary of every file found in the workspace or its
//   associated base manifest. Each unique path is listed on a line
//   prefixed by three status characters and two numeric values used
//   for identifying renames. The three status characters are as
//   follows.
//
//   column 1 pre-state
//         ' ' the path was unchanged in the pre-state
//         'D' the path was deleted from the pre-state
//         'R' the path was renamed from the pre-state name
//   column 2 post-state
//         ' ' the path was unchanged in the post-state
//         'R' the path was renamed to the post-state name
//         'A' the path was added to the post-state
//   column 3 node-state
//         ' ' the node is unchanged from the current roster
//         'P' the node is patched to a new version
//         'U' the node is unknown and not included in the roster
//         'I' the node is ignored and not included in the roster
//         'M' the node is missing but is included in the roster
//
// Output format: Each path is printed on its own line, prefixed by three
//   status characters as described above. The status is followed by a
//   single space and two numbers, each separated by a single space,
//   used for identifying renames.  The numbers are followed by a
//   single space and then the pathname, which includes the rest of
//   the line. Directory paths are identified as ending with the "/"
//   character, file paths do not end in this character.
//
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.

AUTOMATE(inventory, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  app.require_workspace();

  temp_node_id_source nis;
  roster_t curr, base;
  revision_t rev;
  inventory_map inventory;
  cset cs; MM(cs);
  path_set unchanged, changed, missing, unknown, ignored;

  app.work.get_current_roster_shape(curr, app.db, nis);
  app.work.get_work_rev(rev);
  N(rev.edges.size() == 1,
    F("this command can only be used in a single-parent workspace"));

  cs = edge_changes(rev.edges.begin());
  app.db.get_roster(edge_old_revision(rev.edges.begin()), base);

  // The current roster (curr) has the complete set of registered nodes
  // conveniently with unchanged sha1 hash values.

  // The cset (cs) has the list of drops/renames/adds that have
  // occurred between the two rosters along with an empty list of
  // deltas.  this list is empty only because the current roster used
  // to generate the cset does not have current hash values as
  // recorded on the filesystem (because get_..._shape was used to
  // build it).

  path_set nodes_added(cs.dirs_added);
  extract_added_file_paths(cs.files_added, nodes_added);

  inventory_pre_state(inventory, cs.nodes_deleted,
                      inventory_item::DROPPED_PATH, 0);
  inventory_renames(inventory, cs.nodes_renamed);
  inventory_post_state(inventory, nodes_added,
                       inventory_item::ADDED_PATH, 0);

  path_restriction mask;
  vector<file_path> roots;
  roots.push_back(file_path());

  app.work.classify_roster_paths(curr, unchanged, changed, missing);
  app.work.find_unknown_and_ignored(mask, roots, unknown, ignored, app.db);

  inventory_node_state(inventory, unchanged,
                       inventory_item::UNCHANGED_NODE);

  inventory_node_state(inventory, changed,
                       inventory_item::PATCHED_NODE);

  inventory_node_state(inventory, missing,
                       inventory_item::MISSING_NODE);

  inventory_node_state(inventory, unknown,
                       inventory_item::UNKNOWN_NODE);

  inventory_node_state(inventory, ignored,
                       inventory_item::IGNORED_NODE);

  // FIXME: do we want to report on attribute changes here?!?

  for (inventory_map::const_iterator i = inventory.begin();
       i != inventory.end(); ++i)
    {

      string path_suffix;

      // ensure that directory nodes always get a trailing slash even
      // if they're missing from the workspace or have been deleted
      // but skip the root node which do not get this trailing slash appended
      if (curr.has_node(i->first))
        {
          node_t n = curr.get_node(i->first);
          if (is_root_dir_t(n)) continue;
          if (is_dir_t(n)) path_suffix = "/";
        }
      else if (base.has_node(i->first))
        {
          node_t n = base.get_node(i->first);
          if (is_root_dir_t(n)) continue;
          if (is_dir_t(n)) path_suffix = "/";
        }
      else if (directory_exists(file_path(i->first)))
        {
          path_suffix = "/";
        }

      switch (i->second.pre_state)
        {
        case inventory_item::UNCHANGED_PATH: output << ' '; break;
        case inventory_item::DROPPED_PATH: output << 'D'; break;
        case inventory_item::RENAMED_PATH: output << 'R'; break;
        default: I(false); // invalid pre_state
        }

      switch (i->second.post_state)
        {
        case inventory_item::UNCHANGED_PATH: output << ' '; break;
        case inventory_item::RENAMED_PATH: output << 'R'; break;
        case inventory_item::ADDED_PATH:   output << 'A'; break;
        default: I(false); // invalid post_state
        }

      switch (i->second.node_state)
        {
        case inventory_item::UNCHANGED_NODE:
          if (i->second.post_state == inventory_item::ADDED_PATH)
            output << 'P';
          else
            output << ' ';
          break;
        case inventory_item::PATCHED_NODE: output << 'P'; break;
        case inventory_item::UNKNOWN_NODE: output << 'U'; break;
        case inventory_item::IGNORED_NODE: output << 'I'; break;
        case inventory_item::MISSING_NODE: output << 'M'; break;
        default: I(false); // invalid node_state
        }

      output << ' ' << i->second.pre_id
             << ' ' << i->second.post_id
             << ' ' << i->first;

      // FIXME: it's possible that a directory was deleted and a file
      // was added in it's place (or vice-versa) so we need something
      // like pre/post node type indicators rather than a simple path
      // suffix! ugh.

      output << path_suffix;

      output << '\n';
    }
}

// Name: get_revision
// Arguments:
//   1: a revision id (optional, determined from the workspace if
//      non-existant)
// Added in: 1.0

// Purpose: Prints change information for the specified revision id.
//   There are several changes that are described; each of these is
//   described by a different basic_io stanza. The first string pair
//   of each stanza indicates the type of change represented.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Possible values of this first value are along with an ordered list of
//   basic_io formatted stanzas that will be provided are:
//
//   'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1")
//         occurs: exactly once
//   'new_manifest'
//         represents the new manifest associated with the revision.
//         format: ('new_manifest', manifest id)
//         occurs: exactly one
//   'old_revision'
//         represents a parent revision.
//         format: ('old_revision', revision id)
//         occurs: either one or two times
//   'delete
//         represents a file or directory that was deleted.
//         format: ('delete', path)
//         occurs: zero or more times
//   'rename'
//         represents a file or directory that was renamed.
//         format: ('rename, old filename), ('to', new filename)
//         occurs: zero or more times
//   'add_dir'
//         represents a directory that was added.
//         format: ('add_dir, path)
//         occurs: zero or more times
//   'add_file'
//         represents a file that was added.
//         format: ('add_file', path), ('content', file id)
//         occurs: zero or more times
//   'patch'
//         represents a file that was modified.
//         format: ('patch', filename), ('from', file id), ('to', file id)
//         occurs: zero or more times
//   'clear'
//         represents an attr that was removed.
//         format: ('clear', filename), ('attr', attr name)
//         occurs: zero or more times
//   'set'
//         represents an attr whose value was changed.
//         format: ('set', filename), ('attr', attr name), ('value', attr value)
//         occurs: zero or more times
//
//   These stanzas will always occur in the order listed here; stanzas of
//   the same type will be sorted by the filename they refer to.
// Error conditions: If the revision specified is unknown or invalid
// prints an error message to stderr and exits with status 1.
AUTOMATE(get_revision, N_("[REVID]"), options::opts::none)
{
  N(args.size() < 2,
    F("wrong argument count"));

  temp_node_id_source nis;
  revision_data dat;
  revision_id ident;

  if (args.size() == 0)
    {
      roster_t new_roster;
      parent_map old_rosters;
      revision_t rev;

      app.require_workspace();
      app.work.get_parent_rosters(old_rosters, app.db);
      app.work.get_current_roster_shape(new_roster, app.db, nis);
      app.work.update_current_roster_from_filesystem(new_roster);

      make_revision(old_rosters, new_roster, rev);
      calculate_ident(rev, ident);
      write_revision(rev, dat);
    }
  else
    {
      ident = revision_id(idx(args, 0)());
      N(app.db.revision_exists(ident),
        F("no revision %s found in database") % ident);
      app.db.get_revision(ident, dat);
    }

  L(FL("dumping revision %s") % ident);
  output.write(dat.inner()().data(), dat.inner()().size());
}

// Name: get_base_revision_id
// Arguments: none
// Added in: 2.0
// Purpose: Prints the revision id the current workspace is based
//   on. This is the value stored in _MTN/revision
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.
AUTOMATE(get_base_revision_id, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  app.require_workspace();

  parent_map parents;
  app.work.get_parent_rosters(parents, app.db);
  N(parents.size() == 1,
    F("this command can only be used in a single-parent workspace"));

  output << parent_id(parents.begin()) << '\n';
}

// Name: get_current_revision_id
// Arguments: none
// Added in: 2.0
// Purpose: Prints the revision id of the current workspace. This is the
//   id of the revision that would be committed by an unrestricted
//   commit calculated from _MTN/revision, _MTN/work and any edits to
//   files in the workspace.
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.
AUTOMATE(get_current_revision_id, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  app.require_workspace();

  parent_map parents;
  roster_t new_roster;
  revision_id new_revision_id;
  revision_t rev;
  temp_node_id_source nis;

  app.require_workspace();
  app.work.get_current_roster_shape(new_roster, app.db, nis);
  app.work.update_current_roster_from_filesystem(new_roster);

  app.work.get_parent_rosters(parents, app.db);
  make_revision(parents, new_roster, rev);

  calculate_ident(rev, new_revision_id);

  output << new_revision_id << '\n';
}

// Name: get_manifest_of
// Arguments:
//   1: a revision id (optional, determined from the workspace if not given)
// Added in: 2.0
// Purpose: Prints the contents of the manifest associated with the
//   given revision ID.
//
// Output format:
//   There is one basic_io stanza for each file or directory in the
//   manifest.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Possible values of this first value are along with an ordered list of
//   basic_io formatted stanzas that will be provided are:
//
//   'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1")
//         occurs: exactly once
//   'dir':
//         represents a directory.  The path "" (the empty string) is used
//         to represent the root of the tree.
//         format: ('dir', pathname)
//         occurs: one or more times
//   'file':
//         represents a file.
//         format: ('file', pathname), ('content', file id)
//         occurs: zero or more times
//
//   In addition, 'dir' and 'file' stanzas may have attr information
//   included.  These are appended to the stanza below the basic
//   dir/file information, with one line describing each attr.  These
//   lines take the form ('attr', attr name, attr value).
//
//   Stanzas are sorted by the path string.
//
// Error conditions: If the revision ID specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
AUTOMATE(get_manifest_of, N_("[REVID]"), options::opts::none)
{
  N(args.size() < 2,
    F("wrong argument count"));

  manifest_data dat;
  manifest_id mid;
  roster_t new_roster;

  if (args.size() == 0)
    {
      temp_node_id_source nis;

      app.require_workspace();
      app.work.get_current_roster_shape(new_roster, app.db, nis);
      app.work.update_current_roster_from_filesystem(new_roster);
    }
  else
    {
      revision_id rid = revision_id(idx(args, 0)());
      N(app.db.revision_exists(rid),
        F("no revision %s found in database") % rid);
      app.db.get_roster(rid, new_roster);
    }

  calculate_ident(new_roster, mid);
  write_manifest_of_roster(new_roster, dat);
  L(FL("dumping manifest %s") % mid);
  output.write(dat.inner()().data(), dat.inner()().size());
}


// Name: packet_for_rdata
// Arguments:
//   1: a revision id
// Added in: 2.0
// Purpose: Prints the revision data in packet format
//
// Output format: revision data in "monotone read" compatible packet
//   format
//
// Error conditions: If the revision id specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
AUTOMATE(packet_for_rdata, N_("REVID"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  packet_writer pw(output);

  revision_id r_id(idx(args, 0)());
  revision_data r_data;

  N(app.db.revision_exists(r_id),
    F("no such revision '%s'") % r_id);
  app.db.get_revision(r_id, r_data);
  pw.consume_revision_data(r_id,r_data);
}

// Name: packets_for_certs
// Arguments:
//   1: a revision id
// Added in: 2.0
// Purpose: Prints the certs associated with a revision in packet format
//
// Output format: certs in "monotone read" compatible packet format
//
// Error conditions: If the revision id specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
AUTOMATE(packets_for_certs, N_("REVID"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  packet_writer pw(output);

  revision_id r_id(idx(args, 0)());
  vector< revision<cert> > certs;

  N(app.db.revision_exists(r_id),
    F("no such revision '%s'") % r_id);
  app.get_project().get_revision_certs(r_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_revision_cert(idx(certs,i));
}

// Name: packet_for_fdata
// Arguments:
//   1: a file id
// Added in: 2.0
// Purpose: Prints the file data in packet format
//
// Output format: file data in "monotone read" compatible packet format
//
// Error conditions: If the file id specified is unknown or invalid
// prints an error message to stderr and exits with status 1.
AUTOMATE(packet_for_fdata, N_("FILEID"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  packet_writer pw(output);

  file_id f_id(idx(args, 0)());
  file_data f_data;

  N(app.db.file_version_exists(f_id),
    F("no such file '%s'") % f_id);
  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id,f_data);
}

// Name: packet_for_fdelta
// Arguments:
//   1: a file id
//   2: a file id
// Added in: 2.0
// Purpose: Prints the file delta in packet format
//
// Output format: file delta in "monotone read" compatible packet format
//
// Error conditions: If any of the file ids specified are unknown or
// invalid prints an error message to stderr and exits with status 1.
AUTOMATE(packet_for_fdelta, N_("OLD_FILE NEW_FILE"), options::opts::none)
{
  N(args.size() == 2,
    F("wrong argument count"));

  packet_writer pw(output);

  file_id f_old_id(idx(args, 0)());
  file_id f_new_id(idx(args, 1)());
  file_data f_old_data, f_new_data;

  N(app.db.file_version_exists(f_old_id),
    F("no such revision '%s'") % f_old_id);
  N(app.db.file_version_exists(f_new_id),
    F("no such revision '%s'") % f_new_id);
  app.db.get_file_version(f_old_id, f_old_data);
  app.db.get_file_version(f_new_id, f_new_data);
  delta del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));
}

// Name: common_ancestors
// Arguments:
//   1 or more revision ids
// Added in: 2.1
// Purpose: Prints all revisions which are ancestors of all of the
//   revisions given as arguments.
// Output format: A list of revision ids, in hexadecimal, each
//   followed by a newline.  Revisions are printed in alphabetically
//   sorted order.
// Error conditions: If any of the revisions do not exist, prints
//   nothing to stdout, prints an error message to stderr, and exits
//   with status 1.
AUTOMATE(common_ancestors, N_("REV1 [REV2 [REV3 [...]]]"), options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));

  set<revision_id> ancestors, common_ancestors;
  vector<revision_id> frontier;
  for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      ancestors.clear();
      ancestors.insert(rid);
      frontier.push_back(rid);
      while (!frontier.empty())
        {
          revision_id rid = frontier.back();
          frontier.pop_back();
          if(!null_id(rid))
            {
              set<revision_id> parents;
              app.db.get_revision_parents(rid, parents);
              for (set<revision_id>::const_iterator i = parents.begin();
                   i != parents.end(); ++i)
                {
                  if (ancestors.find(*i) == ancestors.end())
                    {
                      frontier.push_back(*i);
                      ancestors.insert(*i);
                    }
                }
            }
        }
      if (common_ancestors.empty())
        common_ancestors = ancestors;
      else
        {
          set<revision_id> common;
          set_intersection(ancestors.begin(), ancestors.end(),
                         common_ancestors.begin(), common_ancestors.end(),
                         inserter(common, common.begin()));
          common_ancestors = common;
        }
    }

  for (set<revision_id>::const_iterator i = common_ancestors.begin();
       i != common_ancestors.end(); ++i)
    if (!null_id(*i))
      output << (*i).inner()() << '\n';
}

// Name: branches
// Arguments:
//   None
// Added in: 2.2
// Purpose:
//   Prints all branch certs present in the revision graph, that are not
//   excluded by the lua hook 'ignore_branch'.
// Output format:
//   Zero or more lines, each the name of a branch. The lines are printed
//   in alphabetically sorted order.
// Error conditions:
//   None.
AUTOMATE(branches, "", options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  set<branch_name> names;

  app.get_project().get_branch_list(names);

  for (set<branch_name>::const_iterator i = names.begin();
       i != names.end(); ++i)
    {
      if (!app.lua.hook_ignore_branch(*i))
        output << (*i) << '\n';
    }
}

// Name: tags
// Arguments:
//   A branch pattern (optional).
// Added in: 2.2
// Purpose:
//   If a branch pattern is given, prints all tags that are attached to
//   revisions on branches matched by the pattern; otherwise prints all tags
//   of the revision graph.
//
//   If a branch name is ignored by means of the lua hook 'ignore_branch',
//   it is neither printed, nor can it be matched by a pattern.
// Output format:
//   There is one basic_io stanza for each tag.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Each stanza has exactly the following four entries:
//
//   'tag'
//         the value of the tag cert, i.e. the name of the tag
//   'revision'
//         the hexadecimal id of the revision the tag is attached to
//   'signer'
//         the name of the key used to sign the tag cert
//   'branches'
//         a (possibly empty) list of all branches the tagged revision is on
//
//   Stanzas are printed in arbitrary order.
// Error conditions:
//   A run-time exception is thrown for illegal patterns.
AUTOMATE(tags, N_("[BRANCH_PATTERN]"), options::opts::none)
{
  N(args.size() < 2,
    F("wrong argument count"));

  globish incl("*");
  bool filtering(false);
  
  if (args.size() == 1) {
    incl = globish(idx(args, 0)());
    filtering = true;
  }

  globish_matcher match(incl, globish());
  basic_io::printer prt;
  basic_io::stanza stz;
  stz.push_str_pair(symbol("format_version"), "1");
  prt.print_stanza(stz);
  
  set<tag_t> tags;
  app.get_project().get_tags(tags);

  for (set<tag_t>::const_iterator tag = tags.begin();
       tag != tags.end(); ++tag)
    {
      set<branch_name> branches;
      app.get_project().get_revision_branches(tag->ident, branches);
    
      bool show(!filtering);
      vector<string> branch_names;

      for (set<branch_name>::const_iterator branch = branches.begin();
           branch != branches.end(); ++branch)
        {
          if (app.lua.hook_ignore_branch(*branch))
            continue;
      
          if (!show && match((*branch)()))
            show = true;
          branch_names.push_back((*branch)());
        }

      if (show)
        {
          basic_io::stanza stz;
          stz.push_str_pair(symbol("tag"), tag->name());
          stz.push_hex_pair(symbol("revision"), tag->ident.inner());
          stz.push_str_pair(symbol("signer"), tag->key());
          stz.push_str_multi(symbol("branches"), branch_names);
          prt.print_stanza(stz);
        }
    }
  output.write(prt.buf.data(), prt.buf.size());
}

namespace
{
  namespace syms
  {
    symbol const key("key");
    symbol const signature("signature");
    symbol const name("name");
    symbol const value("value");
    symbol const trust("trust");

    symbol const public_hash("public_hash");
    symbol const private_hash("private_hash");
    symbol const public_location("public_location");
    symbol const private_location("private_location");
  }
};

// Name: genkey
// Arguments:
//   1: the key ID
//   2: the key passphrase
// Added in: 3.1
// Purpose: Generates a key with the given ID and passphrase
//
// Output format: a basic_io stanza for the new key, as for ls keys
//
// Sample output:
//               name "tbrownaw@gmail.com"
//        public_hash [475055ec71ad48f5dfaf875b0fea597b5cbbee64]
//       private_hash [7f76dae3f91bb48f80f1871856d9d519770b7f8a]
//    public_location "database" "keystore"
//   private_location "keystore"
//
// Error conditions: If the passphrase is empty or the key already exists,
// prints an error message to stderr and exits with status 1.
AUTOMATE(genkey, N_("KEYID PASSPHRASE"), options::opts::none)
{
  N(args.size() == 2,
    F("wrong argument count"));

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  utf8 passphrase = idx(args, 1);

  bool exists = app.keys.key_pair_exists(ident);
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      exists = exists || app.db.public_key_exists(ident);
      guard.commit();
    }

  N(!exists, F("key '%s' already exists") % ident);

  keypair kp;
  P(F("generating key-pair '%s'") % ident);
  generate_key_pair(kp, passphrase);
  P(F("storing key-pair '%s' in %s/") 
    % ident % app.keys.get_key_dir());
  app.keys.put_key_pair(ident, kp);

  basic_io::printer prt;
  basic_io::stanza stz;
  hexenc<id> privhash, pubhash;
  vector<string> publocs, privlocs;
  key_hash_code(ident, kp.pub, pubhash);
  key_hash_code(ident, kp.priv, privhash);

  publocs.push_back("keystore");
  privlocs.push_back("keystore");

  stz.push_str_pair(syms::name, ident());
  stz.push_hex_pair(syms::public_hash, pubhash);
  stz.push_hex_pair(syms::private_hash, privhash);
  stz.push_str_multi(syms::public_location, publocs);
  stz.push_str_multi(syms::private_location, privlocs);
  prt.print_stanza(stz);

  output.write(prt.buf.data(), prt.buf.size());

}

// Name: get_option
// Arguments:
//   1: an options name
// Added in: 3.1
// Purpose: Show the value of the named option in _MTN/options
//
// Output format: A string
//
// Sample output (for 'mtn automate get_option branch:
//   net.venge.monotone
//
AUTOMATE(get_option, N_("OPTION"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  // this command requires a workspace to be run on
  app.require_workspace();

  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;
  app.work.get_ws_options(database_option, branch_option,
                          key_option, keydir_option);

  string opt = args[0]();

  if (opt == "database")
    output << database_option << '\n'; 
  else if (opt == "branch")
    output << branch_option << '\n';
  else if (opt == "key")
    output << key_option << '\n';
  else if (opt == "keydir")
    output << keydir_option << '\n';
  else
    N(false, F("'%s' is not a recognized workspace option") % opt);
}

// Name: get_content_changed
// Arguments:
//   1: a revision ID
//   2: a file name
// Added in: 3.1
// Purpose: Returns a list of revision IDs in which the content 
// was most recently changed, relative to the revision ID specified 
// in argument 1. This equates to a content mark following 
// the *-merge algorithm.
//
// Output format: Zero or more basic_io stanzas, each specifying a 
// revision ID for which a content mark is set.
//
//   Each stanza has exactly one entry:
//
//   'content_mark'
//         the hexadecimal id of the revision the content mark is attached to
// Sample output (for 'mtn automate get_content_changed 3bccff99d08421df72519b61a4dded16d1139c33 ChangeLog):
//   content_mark [276264b0b3f1e70fc1835a700e6e61bdbe4c3f2f]
//
AUTOMATE(get_content_changed, N_("REV FILE"), options::opts::none)
{
  N(args.size() == 2,
    F("wrong argument count"));

  roster_t new_roster;
  revision_id ident;
  marking_map mm;

  ident = revision_id(idx(args, 0)());
  N(app.db.revision_exists(ident),
    F("no revision %s found in database") % ident);
  app.db.get_roster(ident, new_roster, mm);

  split_path path;
  file_path_external(idx(args,1)).split(path);
  N(new_roster.has_node(path),
    F("file %s is unknown for revision %s") % path % ident);

  node_t node = new_roster.get_node(path);
  marking_map::const_iterator m = mm.find(node->self);
  I(m != mm.end());
  marking_t mark = m->second;

  basic_io::printer prt;
  for (set<revision_id>::const_iterator i = mark.file_content.begin();
       i != mark.file_content.end(); ++i)
    {
      basic_io::stanza st;
      st.push_hex_pair(basic_io::syms::content_mark, i->inner());
      prt.print_stanza(st);
    }
    output.write(prt.buf.data(), prt.buf.size());
}

// Name: get_corresponding_path
// Arguments:
//   1: a source revision ID
//   2: a file name (in the source revision)
//   3: a target revision ID
// Added in: 3.1
// Purpose: Given a the file name in the source revision, a filename 
// will if possible be returned naming the file in the target revision. 
// This allows the same file to be matched between revisions, accounting 
// for renames and other changes.
//
// Output format: Zero or one basic_io stanzas. Zero stanzas will be 
// output if the file does not exist within the target revision; this is 
// not considered an error.
// If the file does exist in the target revision, a single stanza with the 
// following details is output.
//
//   The stanza has exactly one entry:
//
//   'file'
//         the file name corresponding to "file name" (arg 2) in the target revision
//
// Sample output (for automate get_corresponding_path 91f25c8ee830b11b52dd356c925161848d4274d0 foo2 dae0d8e3f944c82a9688bcd6af99f5b837b41968; see automate_get_corresponding_path test)
// file "foo"
AUTOMATE(get_corresponding_path, N_("REV1 FILE REV2"), options::opts::none)
{
  N(args.size() == 3,
    F("wrong argument count"));

  roster_t new_roster, old_roster;
  revision_id ident, old_ident;

  ident = revision_id(idx(args, 0)());
  N(app.db.revision_exists(ident),
    F("no revision %s found in database") % ident);
  app.db.get_roster(ident, new_roster);

  old_ident = revision_id(idx(args, 2)());
  N(app.db.revision_exists(old_ident),
    F("no revision %s found in database") % old_ident);
  app.db.get_roster(old_ident, old_roster);

  split_path path;
  file_path_external(idx(args,1)).split(path);
  N(new_roster.has_node(path),
    F("file %s is unknown for revision %s") % path % ident);

  node_t node = new_roster.get_node(path);
  basic_io::printer prt;
  if (old_roster.has_node(node->self))
    {
      split_path old_path;
      basic_io::stanza st;
      old_roster.get_name(node->self, old_path);
      file_path fp = file_path(old_path);
      st.push_file_pair(basic_io::syms::file, fp);  
      prt.print_stanza(st);
    }
  output.write(prt.buf.data(), prt.buf.size());
}

// Name: put_file
// Arguments:
//   base FILEID (optional)
//   file contents (binary, intended for automate stdio use)
// Added in: 4.1
// Purpose:
//   Store a file in the database.
//   Optionally encode it as a file_delta
// Output format:
//   The ID of the new file (40 digit hex string)
// Error conditions:
//   a runtime exception is thrown if base revision is not available
AUTOMATE(put_file, N_("[FILEID] CONTENTS"), options::opts::none)
{
  N(args.size() == 1 || args.size() == 2,
    F("wrong argument count"));

  file_id sha1sum;
  transaction_guard tr(app.db);
  if (args.size() == 1)
    {
      file_data dat(idx(args, 0)());
      calculate_ident(dat, sha1sum);
      
      if (!app.db.file_version_exists(sha1sum))
        app.db.put_file(sha1sum, dat);
    }
  else if (args.size() == 2)
    {
      file_data dat(idx(args, 1)());
      calculate_ident(dat, sha1sum);
      file_id base_id(idx(args, 0)());
      N(app.db.file_version_exists(base_id),
        F("no file version %s found in database") % base_id);
     
      if (!app.db.file_version_exists(sha1sum))
        {
          file_data olddat;
          app.db.get_file_version(base_id, olddat);
          delta del;
          diff(olddat.inner(), dat.inner(), del);
          L(FL("data size %d, delta size %d") % dat.inner()().size() % del().size());
          if (dat.inner()().size() <= del().size())
            // the data is smaller or of equal size to the patch
            app.db.put_file(sha1sum, dat);
          else
            app.db.put_file_version(base_id, sha1sum, file_delta(del));
        }
    }
  else I(false);

  tr.commit();
  output << sha1sum << '\n';
}

// Name: put_revision
// Arguments:
//   revision-data
// Added in: 4.1
// Purpose:
//   Store a revision into the database.
// Output format:
//   The ID of the new revision
// Error conditions:
//   none
AUTOMATE(put_revision, N_("REVISION-DATA"), options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  revision_t rev;
  read_revision(revision_data(idx(args, 0)()), rev);

  // recalculate manifest
  temp_node_id_source nis;
  rev.new_manifest = manifest_id();
  for (edge_map::const_iterator e = rev.edges.begin(); e != rev.edges.end(); ++e)
    {
      // calculate new manifest
      roster_t old_roster;
      if (!null_id(e->first)) app.db.get_roster(e->first, old_roster);
      roster_t new_roster = old_roster;
      editable_roster_base eros(new_roster, nis);
      e->second->apply_to(eros);
      if (null_id(rev.new_manifest))
        // first edge, initialize manifest
        calculate_ident(new_roster, rev.new_manifest);
      else
        // following edge, make sure that all csets end at the same manifest
        {
          manifest_id calculated;
          calculate_ident(new_roster, calculated);
          I(calculated == rev.new_manifest);
        }
    }

  revision_id id;
  calculate_ident(rev, id);

  if (app.db.revision_exists(id))
    P(F("revision %s already present in the database, skipping") % id);
  else
    {
      transaction_guard tr(app.db);
      rev.made_for = made_for_database;
      app.db.put_revision(id, rev);
      tr.commit();
    }

  output << id << '\n';
}

// Name: cert
// Arguments:
//   revision ID
//   certificate name
//   certificate value
// Added in: 4.1
// Purpose:
//   Add a revision certificate (like mtn cert).
// Output format:
//   nothing
// Error conditions:
//   none
AUTOMATE(cert, N_("REVISION-ID NAME VALUE"), options::opts::none)
{
  N(args.size() == 3,
    F("wrong argument count"));

  cert c;
  revision_id rid(idx(args, 0)());

  transaction_guard guard(app.db);
  N(app.db.revision_exists(rid),
    F("no such revision '%s'") % rid);
  make_simple_cert(rid.inner(), cert_name(idx(args, 1)()),
                   cert_value(idx(args, 2)()), app, c);
  revision<cert> rc(c);
  packet_db_writer dbw(app);
  dbw.consume_revision_cert(rc);
  guard.commit();
}

// Name: db_set
// Arguments:
//   variable domain
//   variable name
//   veriable value
// Added in: 4.1
// Purpose:
//   Set a database variable (like mtn database set)
// Output format:
//   nothing
// Error conditions:
//   none
AUTOMATE(db_set, N_("DOMAIN NAME VALUE"), options::opts::none)
{
  N(args.size() == 3,
    F("wrong argument count"));
  
  var_domain domain = var_domain(idx(args, 0)());
  utf8 name = idx(args, 1);
  utf8 value = idx(args, 2);
  var_key key(domain, var_name(name()));
  app.db.set_var(key, var_value(value()));
}

// Name: db_get
// Arguments:
//   variable domain
//   variable name
// Added in: 4.1
// Purpose:
//   Get a database variable (like mtn database ls vars | grep NAME)
// Output format:
//   variable value
// Error conditions:
//   a runtime exception is thrown if the variable is not set
AUTOMATE(db_get, N_("DOMAIN NAME"), options::opts::none)
{
  N(args.size() == 2,
    F("wrong argument count"));

  var_domain domain = var_domain(idx(args, 0)());
  utf8 name = idx(args, 1);
  var_key key(domain, var_name(name()));
  var_value value;
  try
    {
      app.db.get_var(key, value);
    }
  catch (std::logic_error)
    {
      N(false, F("variable not found"));
    }
  output << value();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
