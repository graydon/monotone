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
#include <boost/lexical_cast.hpp>
#include <boost/tuple/tuple.hpp>

#include "app_state.hh"
#include "basic_io.hh"
#include "cert.hh"
#include "cmd.hh"
#include "commands.hh"
#include "constants.hh"
#include "inodeprint.hh"
#include "keys.hh"
#include "localized_file_io.hh"
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

using boost::lexical_cast;

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
  if (args.size() > 1)
    throw usage(help_name);

  if (args.size() ==1 ) {
    // branchname was explicitly given, use that
    app.opts.branch_name = idx(args, 0);
  }
  set<revision_id> heads;
  get_branch_heads(app.opts.branch_name(), app, heads);
  for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    output << (*i).inner()() << "\n";
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
      output << (*i).inner()() << "\n";
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
    output << (*i).inner()() << "\n";
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
    output << (*i).inner()() << "\n";
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
  app.work.get_base_and_current_roster_shape(base, current, nis);

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
    output << (*i).inner()() << "\n";
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
    output << (*i).inner()() << "\n";
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
    output << (*i).inner()() << "\n";
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
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> parents;
  app.db.get_revision_parents(rid, parents);
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << "\n";
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
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  set<revision_id> children;
  app.db.get_revision_children(rid, children);
  for (set<revision_id>::const_iterator i = children.begin();
       i != children.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << "\n";
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
      output << "\n";
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
    output << *i << "\n";
}

struct node_info 
{ 
  bool exists;
  node_id id;
  path::status type;
  file_id ident;
  full_attr_map_t attrs;

  node_info() : exists(false), type(path::nonexistent) {}
};

static void
get_node_info(roster_t const & roster, split_path const & path, node_info & info)
{
  if (roster.has_node(path))
    {
      node_t node = roster.get_node(path);
      info.exists = true;
      info.id = node->self;
      info.attrs = node->attrs;
      if (is_file_t(node))
        {
          info.type = path::file;
          info.ident = downcast_to_file_t(node)->content;
        }
      else if (is_dir_t(node))
        info.type = path::directory;
      else
        I(false);
    }
}

struct inventory_item
{
  node_info old_node;
  node_info new_node;

  path::status fs_type;
  file_id fs_ident;

  inventory_item() : fs_type(path::nonexistent) {}
};

typedef std::map<split_path, inventory_item> inventory_map;

static void
inventory_rosters(roster_t const & old_roster, 
                  roster_t const & new_roster,
                  inventory_map & inventory)
{
  node_map const & old_nodes = old_roster.all_nodes();
  for (node_map::const_iterator i = old_nodes.begin(); i != old_nodes.end(); ++i)
    {
      split_path sp;
      old_roster.get_name(i->first, sp);
      get_node_info(old_roster, sp, inventory[sp].old_node);
    }

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      split_path sp;
      new_roster.get_name(i->first, sp);
      get_node_info(new_roster, sp, inventory[sp].new_node);
    }
  
}

struct inventory_itemizer : public tree_walker
{
  app_state & app;
  inventory_map & inventory;
  inodeprint_map ipm;

  inventory_itemizer(app_state & a, inventory_map & i) : 
    app(a), inventory(i)
  {
    if (app.work.in_inodeprints_mode())
      {
        data dat;
        app.work.read_inodeprints(dat);
        read_inodeprint_map(dat, ipm);
      }
  }
  virtual void visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

void
inventory_itemizer::visit_dir(file_path const & path)
{
  split_path sp;
  path.split(sp);
  inventory[sp].fs_type = path::directory;
}

void
inventory_itemizer::visit_file(file_path const & path)
{
  split_path sp;
  path.split(sp);

  inventory_item & item = inventory[sp];

  item.fs_type = path::file;

  if (item.new_node.exists)
    {
      if (inodeprint_unchanged(ipm, path))
        item.fs_ident = item.old_node.ident;
      else
        ident_existing_file(path, item.fs_ident, app.lua);
    }
}

static void
inventory_filesystem(app_state & app, inventory_map & inventory)
{
  inventory_itemizer itemizer(app, inventory);
  walk_tree(file_path(), itemizer);
}

namespace
{
  namespace syms
  {
    symbol const path("path");
    symbol const old_node("old_node");
    symbol const new_node("new_node");
    symbol const fs_type("fs_type");
    symbol const status("status");
    symbol const changes("changes");
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
  if (args.size() != 0)
    throw usage(help_name);

  app.require_workspace();

  temp_node_id_source nis;
  roster_t old_roster, new_roster;

  app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);

  inventory_map inventory;

  inventory_rosters(old_roster, new_roster, inventory);
  inventory_filesystem(app, inventory);

  basic_io::printer pr;

  for (inventory_map::const_iterator i = inventory.begin(); i != inventory.end(); 
       ++i)
    {
      basic_io::stanza st;
      inventory_item const & item = i->second;

      st.push_file_pair(syms::path, i->first);

      if (item.old_node.exists)
        {
          string id = lexical_cast<string>(item.old_node.id);
//           st.push_str_pair("old_id", lexical_cast<string>(item.old_node.id));
          switch (item.old_node.type)
            {
            case path::file: st.push_str_triple(syms::old_node, id, "file"); break;
            case path::directory: st.push_str_triple(syms::old_node, id, "directory"); break;
//             case path::file: st.push_str_pair("old_type", "file"); break;
//             case path::directory: st.push_str_pair("old_type", "directory"); break;
            case path::nonexistent: I(false);
            }
        }

      if (item.new_node.exists)
        {
          string id = lexical_cast<string>(item.new_node.id);
//           st.push_str_pair("new_id", lexical_cast<string>(item.new_node.id));
          switch (item.new_node.type)
            {
            case path::file: st.push_str_triple(syms::new_node, id, "file"); break;
            case path::directory: st.push_str_triple(syms::new_node, id, "directory"); break;
//             case path::file: st.push_str_pair("new_type", "file"); break;
//             case path::directory: st.push_str_pair("new_type", "directory"); break;
            case path::nonexistent: I(false);
            }
        }

      switch (item.fs_type)
        {
        case path::file: st.push_str_pair(syms::fs_type, "file"); break;
        case path::directory: st.push_str_pair(syms::fs_type, "directory"); break;
        case path::nonexistent: st.push_str_pair(syms::fs_type, "none"); break;
        }

      if (item.fs_type == path::nonexistent)
        {
          if (item.new_node.exists)
            st.push_str_pair(syms::status, "missing");
        }
      else // exists on filesystem
        {
          if (!item.new_node.exists)
            {
              if (app.lua.hook_ignore_file(i->first))
                st.push_str_pair(syms::status, "ignored");
              else 
                st.push_str_pair(syms::status, "unknown");
            }
          else if (item.new_node.type != item.fs_type)
            st.push_str_pair(syms::status, "invalid");
          // TODO: would an ls_invalid command be good for listing these paths?
          else
            st.push_str_pair(syms::status, "known");
        }

      // note that we have three sources of information here
      //
      // the old roster
      // the new roster
      // the filesystem
      //
      // the new roster is synthesised from the old roster and the contents of
      // _MTN/work and has *not* been updated with content hashes from the
      // filesystem.
      //
      // one path can represent different nodes in the old and new rosters and
      // the two different nodes can potentially be different types (file vs dir).
      //
      // we're interested in comparing the content and attributes of the current
      // path in the new roster against their corresponding values in the old
      // roster.
      // 
      // the new content hash comes from the filesystem since the new roster has
      // not been updated. the new attributes can come directly from the new
      // roster.
      //
      // the old content hash and attributes both come from the old roster but
      // we must use the node id of the path in the new roster to get the node
      // from the old roster to compare against.

      if (item.new_node.exists)
        {
          std::vector<string> changes;

          if (item.new_node.type == path::file && old_roster.has_node(item.new_node.id))
            {
              file_t old_file = downcast_to_file_t(old_roster.get_node(item.new_node.id));
              old_file->content;
              if (item.fs_type == path::file && !(item.fs_ident == old_file->content))
                changes.push_back("content");
            }

          if (old_roster.has_node(item.new_node.id))
            {
              node_t old_node = old_roster.get_node(item.new_node.id);
              if (old_node->attrs != item.new_node.attrs)
                changes.push_back("attrs");
            }

          if (!changes.empty())
            st.push_str_multi(syms::changes, changes);
        }

      pr.print_stanza(st);
    }

  output.write(pr.buf.data(), pr.buf.size());
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
      app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
      app.work.update_current_roster_from_filesystem(new_roster);

      app.work.get_revision_id(old_revision_id);
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
AUTOMATE(get_base_revision_id, "", options::opts::none)
{
  if (args.size() > 0)
    throw usage(help_name);

  app.require_workspace();

  revision_id rid;
  app.work.get_revision_id(rid);
  output << rid << "\n";
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
  if (args.size() > 0)
    throw usage(help_name);

  app.require_workspace();

  roster_t old_roster, new_roster;
  revision_id old_revision_id, new_revision_id;
  revision_t rev;
  temp_node_id_source nis;

  app.require_workspace();
  app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
  app.work.update_current_roster_from_filesystem(new_roster);

  app.work.get_revision_id(old_revision_id);
  make_revision(old_revision_id, old_roster, new_roster, rev);

  calculate_ident(rev, new_revision_id);

  output << new_revision_id << "\n";
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
      app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
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
AUTOMATE(get_file, N_("FILEID"), options::opts::none)
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
AUTOMATE(packet_for_rdata, N_("REVID"), options::opts::none)
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
AUTOMATE(packets_for_certs, N_("REVID"), options::opts::none)
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
AUTOMATE(packet_for_fdata, N_("FILEID"), options::opts::none)
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
AUTOMATE(packet_for_fdelta, N_("OLD_FILE NEW_FILE"), options::opts::none)
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
AUTOMATE(common_ancestors, N_("REV1 [REV2 [REV3 [...]]]"), options::opts::none)
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
      output << (*i).inner()() << "\n";
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
  if (args.size() > 0)
    throw usage(help_name);

  vector<string> names;

  app.db.get_branches(names);
  sort(names.begin(), names.end());

  for (vector<string>::const_iterator i = names.begin();
       i != names.end(); ++i)
    if (!app.lua.hook_ignore_branch(*i))
      output << (*i) << "\n";
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
AUTOMATE(genkey, N_("KEYID PASSPHRASE"), options::opts::none)
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
AUTOMATE(get_option, N_("OPTION"), options::opts::none)
{
  if (!app.opts.unknown && (args.size() < 1))
    throw usage(help_name);

  // this command requires a workspace to be run on
  app.require_workspace();

  utf8 database_option, branch_option, key_option, keydir_option;
  app.work.get_ws_options(database_option, branch_option,
                          key_option, keydir_option);

  string opt = args[0]();

  if (opt == "database")
    output << database_option << "\n"; 
  else if (opt == "branch")
    output << branch_option << "\n";
  else if (opt == "key")
    output << key_option << "\n";
  else if (opt == "keydir")
    output << keydir_option << "\n";
  else
    N(false, F("'%s' is not a recognized workspace option") % opt);
}

// Name: get_content_changed
// Arguments:
//   1: a revision ID
//   2: a file name
// Added in: 3.2
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
  if (args.size() != 2)
    throw usage(help_name);

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
      revision_id old_ident = i->inner();
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
// Added in: 3.2
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
  if (args.size() != 3)
    throw usage(help_name);

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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
