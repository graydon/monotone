// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <iostream>
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

using std::allocator;
using std::basic_ios;
using std::basic_stringbuf;
using std::char_traits;
using std::endl;
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
AUTOMATE(heads, N_("[BRANCH]"))
{
  if (args.size() > 1)
    throw usage(help_name);

  if (args.size() ==1 ) {
    // branchname was explicitly given, use that
    app.set_branch(idx(args, 0));
  }
  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    output << (*i).inner()() << endl;
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
AUTOMATE(ancestors, N_("REV1 [REV2 [REV3 [...]]]"))
{
  if (args.size() == 0)
    throw usage(help_name);

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
      output << (*i).inner()() << endl;
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
AUTOMATE(descendents, N_("REV1 [REV2 [REV3 [...]]]"))
{
  if (args.size() == 0)
    throw usage(help_name);

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
    output << (*i).inner()() << endl;
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
AUTOMATE(erase_ancestors, N_("[REV1 [REV2 [REV3 [...]]]]"))
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
    output << (*i).inner()() << endl;
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
AUTOMATE(attributes, N_("FILE"))
{
  if (args.size() != 1)
    throw usage(help_name);

  // this command requires a workspace to be run on
  app.require_workspace();

  // retrieve the path
  split_path path;
  file_path_external(idx(args,0)).split(path);

  roster_t base, current;
  temp_node_id_source nis;

  // get the base and the current roster of this workspace
  get_base_and_current_roster_shape(base, current, nis, app);

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
        full_attr_map_t::const_iterator j = prev_node->attrs.find(i->first());
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
              prev_node->attrs.find(i->first());
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
    st.push_str_pair(std::string("state"), state);
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
AUTOMATE(toposort, N_("[REV1 [REV2 [REV3 [...]]]]"))
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
    output << (*i).inner()() << endl;
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
AUTOMATE(ancestry_difference, N_("NEW_REV [OLD_REV1 [OLD_REV2 [...]]]"))
{
  if (args.size() == 0)
    throw usage(help_name);

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
    output << (*i).inner()() << endl;
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
AUTOMATE(leaves, "")
{
  if (args.size() != 0)
    throw usage(help_name);

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
    output << (*i).inner()() << endl;
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
AUTOMATE(parents, N_("REV"))
{
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> parents;
  app.db.get_revision_parents(rid, parents);
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << endl;
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
AUTOMATE(children, N_("REV"))
{
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> children;
  app.db.get_revision_children(rid, children);
  for (set<revision_id>::const_iterator i = children.begin();
       i != children.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << endl;
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
AUTOMATE(graph, "")
{
  if (args.size() != 0)
    throw usage(help_name);

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
        output << " " << (*j).inner()();
      output << endl;
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
AUTOMATE(select, N_("SELECTOR"))
{
  if (args.size() != 1)
    throw usage(help_name);

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(args[0](), app));

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  for (set<string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    output << *i << endl;
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

AUTOMATE(inventory, "")
{
  if (args.size() != 0)
    throw usage(help_name);

  app.require_workspace();

  temp_node_id_source nis;
  roster_t base, curr;
  inventory_map inventory;
  cset cs; MM(cs);
  path_set unchanged, changed, missing, known, unknown, ignored;

  get_base_and_current_roster_shape(base, curr, nis, app);
  make_cset(base, curr, cs);

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

  classify_roster_paths(curr, unchanged, changed, missing, app);
  curr.extract_path_set(known);

  path_restriction mask;
  file_itemizer u(app, known, unknown, ignored, mask);
  walk_tree(file_path(), u);

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

      if (curr.has_node(i->first))
        {
          // Explicitly skip the root dir for now. The trailing / dir
          // format isn't going to work here.
          node_t n = curr.get_node(i->first);
          if (is_root_dir_t(n)) continue;
          if (is_dir_t(n)) path_suffix = "/";
        }
      else if (directory_exists(file_path(i->first)))
        {
          path_suffix = "/";
        }

      switch (i->second.pre_state)
        {
        case inventory_item::UNCHANGED_PATH: output << " "; break;
        case inventory_item::DROPPED_PATH: output << "D"; break;
        case inventory_item::RENAMED_PATH: output << "R"; break;
        default: I(false); // invalid pre_state
        }

      switch (i->second.post_state)
        {
        case inventory_item::UNCHANGED_PATH: output << " "; break;
        case inventory_item::RENAMED_PATH: output << "R"; break;
        case inventory_item::ADDED_PATH:   output << "A"; break;
        default: I(false); // invalid post_state
        }

      switch (i->second.node_state)
        {
        case inventory_item::UNCHANGED_NODE: output << " "; break;
        case inventory_item::PATCHED_NODE: output << "P"; break;
        case inventory_item::UNKNOWN_NODE: output << "U"; break;
        case inventory_item::IGNORED_NODE: output << "I"; break;
        case inventory_item::MISSING_NODE: output << "M"; break;
        default: I(false); // invalid node_state
        }

      output << " " << i->second.pre_id
             << " " << i->second.post_id
             << " " << i->first;

      // FIXME: it's possible that a directory was deleted and a file
      // was added in it's place (or vice-versa) so we need something
      // like pre/post node type indicators rather than a simple path
      // suffix! ugh.

      output << path_suffix;

      output << endl;
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
AUTOMATE(get_revision, N_("[REVID]"))
{
  if (args.size() > 1)
    throw usage(help_name);

  temp_node_id_source nis;
  revision_data dat;
  revision_id ident;

  if (args.size() == 0)
    {
      roster_t old_roster, new_roster;
      revision_id old_revision_id;
      revision_t rev;

      app.require_workspace();
      get_base_and_current_roster_shape(old_roster, new_roster, nis, app);
      update_current_roster_from_filesystem(new_roster, app);

      get_revision_id(old_revision_id);
      make_revision(old_revision_id, old_roster, new_roster, rev);

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
AUTOMATE(get_base_revision_id, "")
{
  if (args.size() > 0)
    throw usage(help_name);

  app.require_workspace();

  revision_id rid;
  get_revision_id(rid);
  output << rid << endl;
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
AUTOMATE(get_current_revision_id, "")
{
  if (args.size() > 0)
    throw usage(help_name);

  app.require_workspace();

  roster_t old_roster, new_roster;
  revision_id old_revision_id, new_revision_id;
  revision_t rev;
  temp_node_id_source nis;

  app.require_workspace();
  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);
  update_current_roster_from_filesystem(new_roster, app);

  get_revision_id(old_revision_id);
  make_revision(old_revision_id, old_roster, new_roster, rev);

  calculate_ident(rev, new_revision_id);

  output << new_revision_id << endl;
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
AUTOMATE(get_manifest_of, N_("[REVID]"))
{
  if (args.size() > 1)
    throw usage(help_name);

  manifest_data dat;
  manifest_id mid;
  roster_t old_roster, new_roster;
  temp_node_id_source nis;

  if (args.size() == 0)
    {
      revision_id old_revision_id;

      app.require_workspace();
      get_base_and_current_roster_shape(old_roster, new_roster, nis, app);
      update_current_roster_from_filesystem(new_roster, app);
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


// Name: get_file
// Arguments:
//   1: a file id
// Added in: 1.0
// Purpose: Prints the contents of the specified file.
//
// Output format: The file contents are output without modification.
//
// Error conditions: If the file id specified is unknown or invalid prints
// an error message to stderr and exits with status 1.
AUTOMATE(get_file, N_("FILEID"))
{
  if (args.size() != 1)
    throw usage(help_name);

  file_id ident(idx(args, 0)());
  N(app.db.file_version_exists(ident),
    F("no file version %s found in database") % ident);

  file_data dat;
  L(FL("dumping file %s") % ident);
  app.db.get_file_version(ident, dat);
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
AUTOMATE(packet_for_rdata, N_("REVID"))
{
  if (args.size() != 1)
    throw usage(help_name);

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
AUTOMATE(packets_for_certs, N_("REVID"))
{
  if (args.size() != 1)
    throw usage(help_name);

  packet_writer pw(output);

  revision_id r_id(idx(args, 0)());
  vector< revision<cert> > certs;

  N(app.db.revision_exists(r_id),
    F("no such revision '%s'") % r_id);
  app.db.get_revision_certs(r_id, certs);
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
AUTOMATE(packet_for_fdata, N_("FILEID"))
{
  if (args.size() != 1)
    throw usage(help_name);

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
AUTOMATE(packet_for_fdelta, N_("OLD_FILE NEW_FILE"))
{
  if (args.size() != 2)
    throw usage(help_name);

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
AUTOMATE(common_ancestors, N_("REV1 [REV2 [REV3 [...]]]"))
{
  if (args.size() == 0)
    throw usage(help_name);

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
      output << (*i).inner()() << endl;
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
AUTOMATE(branches, "")
{
  if (args.size() > 0)
    throw usage(help_name);

  vector<string> names;

  app.db.get_branches(names);
  sort(names.begin(), names.end());

  for (vector<string>::const_iterator i = names.begin();
       i != names.end(); ++i)
    if (!app.lua.hook_ignore_branch(*i))
      output << (*i) << endl;
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
AUTOMATE(tags, N_("[BRANCH_PATTERN]"))
{
  utf8 incl("*");
  bool filtering(false);
  
  if (args.size() == 1) {
    incl = idx(args, 0);
    filtering = true;
  }
  else if (args.size() > 1)
    throw usage(name);

  globish_matcher match(incl, utf8());
  basic_io::printer prt;
  basic_io::stanza stz;
  stz.push_str_pair(symbol("format_version"), "1");
  prt.print_stanza(stz);
  
  vector<revision<cert> > tag_certs;
  app.db.get_revision_certs(tag_cert_name, tag_certs);

  for (vector<revision<cert> >::const_iterator i = tag_certs.begin();
       i != tag_certs.end(); ++i) {

    cert tagcert(i->inner());
    vector<revision<cert> > branch_certs;
    app.db.get_revision_certs(tagcert.ident, branch_cert_name, branch_certs);
    
    bool show(!filtering);
    vector<string> branch_names;

    for (vector<revision<cert> >::const_iterator j = branch_certs.begin();
         j != branch_certs.end(); ++j) {

      cert branchcert(j->inner());
      cert_value branch;
      decode_base64(branchcert.value, branch);
      string branch_name(branch());
      
      if (app.lua.hook_ignore_branch(branch_name))
        continue;
      
      if (!show && match(branch_name)) 
        show = true;
      branch_names.push_back(branch_name);
    }

    if (show) {
      basic_io::stanza stz;
      cert_value tag;
      decode_base64(tagcert.value, tag);
      stz.push_str_pair(symbol("tag"), tag());
      stz.push_hex_pair(symbol("revision"), tagcert.ident);
      stz.push_str_pair(symbol("signer"), tagcert.key());
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
AUTOMATE(genkey, N_("KEYID PASSPHRASE"))
{
  if (args.size() != 2)
    throw usage(help_name);

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
AUTOMATE(get_option, N_("OPTION"))
{
  if (!app.unknown && (args.size() < 1))
    throw usage(help_name);

  // this command requires a workspace to be run on
  app.require_workspace();

  utf8 result = app.options[args[0]()];
  N(result().size() > 0,
    F("option %s doesn't exist") % args[0]);
  output << result << endl;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
