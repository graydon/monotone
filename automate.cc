// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2004 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iostream>
#include <iterator>
#include <vector>
#include <algorithm>

#include "app_state.hh"
#include "basic_io.hh"
#include "commands.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "vocab.hh"

static std::string const interface_version = "0.2";

// Name: interface_version
// Arguments: none
// Added in: 0.0
// Purpose: Prints version of automation interface.  Major number increments
//   whenever a backwards incompatible change is made; minor number increments
//   whenever any change is made (but is reset when major number increments).
// Output format: "<decimal number>.<decimal number>\n".  Always matches
//   "[0-9]+\.[0-9]+\n".
// Error conditions: None.
static void
automate_interface_version(std::vector<utf8> args,
                           std::string const & help_name,
                           app_state & app,
                           std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);
  
  output << interface_version << std::endl;
}

// Name: heads
// Arguments:
//   1: branch name (optional, default branch is used if non-existant)
// Added in: 0.0
// Purpose: Prints the heads of the given branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If the branch does not exist, prints nothing.  (There are
//   no heads.)
static void
automate_heads(std::vector<utf8> args,
               std::string const & help_name,
               app_state & app,
               std::ostream & output)
{
  if (args.size() > 1)
    throw usage(help_name);

  if (args.size() ==1 ) {
    // branchname was explicitly given, use that
    app.set_branch(idx(args, 0));
  }
  std::set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  for (std::set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_ancestors(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output)
{
  if (args.size() == 0)
    throw usage(help_name);

  std::set<revision_id> ancestors;
  std::vector<revision_id> frontier;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
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
        std::set<revision_id> parents;
        app.db.get_revision_parents(rid, parents);
        for (std::set<revision_id>::const_iterator i = parents.begin();
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
  for (std::set<revision_id>::const_iterator i = ancestors.begin();
       i != ancestors.end(); ++i)
    if (!null_id(*i))
      output << (*i).inner()() << std::endl;
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
static void
automate_descendents(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output)
{
  if (args.size() == 0)
    throw usage(help_name);

  std::set<revision_id> descendents;
  std::vector<revision_id> frontier;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      frontier.push_back(rid);
    }
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      std::set<revision_id> children;
      app.db.get_revision_children(rid, children);
      for (std::set<revision_id>::const_iterator i = children.begin();
           i != children.end(); ++i)
        {
          if (descendents.find(*i) == descendents.end())
            {
              frontier.push_back(*i);
              descendents.insert(*i);
            }
        }
    }
  for (std::set<revision_id>::const_iterator i = descendents.begin();
       i != descendents.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_erase_ancestors(std::vector<utf8> args,
                         std::string const & help_name,
                         app_state & app,
                         std::ostream & output)
{
  std::set<revision_id> revs;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      revs.insert(rid);
    }
  erase_ancestors(revs, app);
  for (std::set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_toposort(std::vector<utf8> args,
                  std::string const & help_name,
                  app_state & app,
                  std::ostream & output)
{
  std::set<revision_id> revs;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid((*i)());
      N(app.db.revision_exists(rid), F("No such revision %s") % rid);
      revs.insert(rid);
    }
  std::vector<revision_id> sorted;
  toposort(revs, sorted, app);
  for (std::vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_ancestry_difference(std::vector<utf8> args,
                             std::string const & help_name,
                             app_state & app,
                             std::ostream & output)
{
  if (args.size() == 0)
    throw usage(help_name);

  revision_id a;
  std::set<revision_id> bs;
  std::vector<utf8>::const_iterator i = args.begin();
  a = revision_id((*i)());
  N(app.db.revision_exists(a), F("No such revision %s") % a);
  for (++i; i != args.end(); ++i)
    {
      revision_id b((*i)());
      N(app.db.revision_exists(b), F("No such revision %s") % b);
      bs.insert(b);
    }
  std::set<revision_id> ancestors;
  ancestry_difference(a, bs, ancestors, app);

  std::vector<revision_id> sorted;
  toposort(ancestors, sorted, app);
  for (std::vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_leaves(std::vector<utf8> args,
               std::string const & help_name,
               app_state & app,
               std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);

  // this might be more efficient in SQL, but for now who cares.
  std::set<revision_id> leaves;
  app.db.get_revision_ids(leaves);
  std::multimap<revision_id, revision_id> graph;
  app.db.get_revision_ancestry(graph);
  for (std::multimap<revision_id, revision_id>::const_iterator i = graph.begin();
       i != graph.end(); ++i)
    leaves.erase(i->first);
  for (std::set<revision_id>::const_iterator i = leaves.begin(); i != leaves.end(); ++i)
    output << (*i).inner()() << std::endl;
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
static void
automate_parents(std::vector<utf8> args,
                 std::string const & help_name,
                 app_state & app,
                 std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  std::set<revision_id> parents;
  app.db.get_revision_parents(rid, parents);
  for (std::set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << std::endl;
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
static void
automate_children(std::vector<utf8> args,
                  std::string const & help_name,
                  app_state & app,
                  std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  std::set<revision_id> children;
  app.db.get_revision_children(rid, children);
  for (std::set<revision_id>::const_iterator i = children.begin();
       i != children.end(); ++i)
      if (!null_id(*i))
          output << (*i).inner()() << std::endl;
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
static void
automate_graph(std::vector<utf8> args,
               std::string const & help_name,
               app_state & app,
               std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);

  std::multimap<revision_id, revision_id> edges_mmap;
  std::map<revision_id, std::set<revision_id> > child_to_parents;

  app.db.get_revision_ancestry(edges_mmap);

  for (std::multimap<revision_id, revision_id>::const_iterator i = edges_mmap.begin();
       i != edges_mmap.end(); ++i)
    {
      if (child_to_parents.find(i->second) == child_to_parents.end())
        child_to_parents.insert(std::make_pair(i->second, std::set<revision_id>()));
      if (null_id(i->first))
        continue;
      std::map<revision_id, std::set<revision_id> >::iterator
        j = child_to_parents.find(i->second);
      I(j->first == i->second);
      j->second.insert(i->first);
    }

  for (std::map<revision_id, std::set<revision_id> >::const_iterator i = child_to_parents.begin();
       i != child_to_parents.end(); ++i)
    {
      output << (i->first).inner()();
      for (std::set<revision_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        output << " " << (*j).inner()();
      output << std::endl;
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
static void
automate_select(std::vector<utf8> args,
               std::string const & help_name,
               app_state & app,
               std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);

  std::vector<std::pair<selectors::selector_type, std::string> >
    sels(selectors::parse_selector(args[0](), app));

  // we jam through an "empty" selection on sel_ident type
  std::set<std::string> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  for (std::set<std::string>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    output << *i << std::endl;
}

struct inventory_item
{
  enum pstat 
    { UNCHANGED_PATH, ADDED_PATH, DROPPED_PATH, RENAMED_PATH, UNKNOWN_PATH, IGNORED_PATH } 
    path_status;

  enum dstat 
    { UNCHANGED_DATA, PATCHED_DATA, MISSING_DATA } 
    data_status;

  enum ptype
    { FILE, DIRECTORY } 
    path_type;

  file_path old_path;

  inventory_item():
    path_status(UNCHANGED_PATH), data_status(UNCHANGED_DATA), path_type(FILE), old_path() {}
};

typedef std::map<file_path, inventory_item> inventory_map;

static void
inventory_paths(inventory_map & inventory,
                path_set const & paths,
                inventory_item::pstat path_status, 
                inventory_item::ptype path_type = inventory_item::FILE)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(F("%d %d %s\n") % inventory[*i].path_status % path_status % *i);
      I(inventory[*i].path_status == inventory_item::UNCHANGED_PATH);
      inventory[*i].path_status = path_status;
      inventory[*i].path_type = path_type;
    }
}

static void
inventory_paths(inventory_map & inventory,
                path_set const & paths,
                inventory_item::dstat data_status)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(F("%d %d %s\n") % inventory[*i].data_status % data_status % *i);
      I(inventory[*i].data_status == inventory_item::UNCHANGED_DATA);
      inventory[*i].data_status = data_status;
    }
}

static void
inventory_paths(inventory_map & inventory,
                std::map<file_path,file_path> const & renames,
                inventory_item::pstat path_status, 
                inventory_item::ptype path_type = inventory_item::FILE)
{
  for (std::map<file_path,file_path>::const_iterator i = renames.begin(); 
       i != renames.end(); i++)
    {
      L(F("%d %d %s %s\n") % inventory[i->second].path_status % path_status % i->first % i->second);
      I(inventory[i->second].path_status == inventory_item::UNCHANGED_PATH);
      inventory[i->second].path_status = inventory_item::RENAMED_PATH;
      inventory[i->second].path_type = path_type;
      inventory[i->second].old_path = i->first;
    }
}
               
// Name: inventory
// Arguments: none
// Added in: 0.2
// Purpose: Prints all the files found in a working copy or current manifest
//   prefixed by 2 status code characters. The first status code character
//   indicates the status of the path itsself and is drawn from the following 
//   set:
//
//   ' ' the path is unchanged from the current manifest 
//   '+' the path has been added to the current manifest
//   '-' the path has been dropped from the current manifest
//   '%' the path has been renamed in the current manifest, both the old and new name are listed
//   '?' the path is unknown, it exists in the working copy but not in the current manifest
//   '~' the path is ignored by the current ignore_file lua hook setting
//  
//   The second status code character indicates the status of the data associated
//   with the path and is drawn from the following set:
//
//   ' ' the data is unchanged, its sha1 version matches the version in the base manifest
//   '#' the data is changed, its sha1 version differs from the version in the base manifest
//   '!' the data is missing and its sha1 version cannot be computed
//
// Output format: Each file is printed on its own line, prefixed by a
//   two character status code and a single space character. All filenames are
//   quoted with double quotes (") to support filenames containg spaces. Intervening quotes
//   are escaped with \". Directories are identified by paths ending with '/' characters. 
//   Rename lines list the old name first, followed by the new name.
// Error conditions: If no working copy book keeping MT directory is found,
//   prints an error message to stderr, and exits with status 1.
static void
automate_inventory(std::vector<utf8> args,
                   std::string const & help_name,
                   app_state & app,
                   std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);

  manifest_id old_manifest_id;
  revision_id old_revision_id;
  manifest_map m_old;
  path_set old_paths, new_paths, empty;
  change_set::path_rearrangement included, excluded;
  path_set missing, changed, unchanged, unknown, ignored;
  inventory_map inventory;

  app.require_working_copy();

  calculate_restricted_rearrangement(app, args, 
                                     old_manifest_id, old_revision_id,
                                     m_old, old_paths, new_paths,
                                     included, excluded);

  file_itemizer u(app, new_paths, unknown, ignored);
  walk_tree(u);

  // remove deleted paths from the set of unknown paths

  for (path_set::const_iterator i = included.deleted_files.begin();
         i != included.deleted_files.end(); ++i)
    unknown.erase(*i);

  for (path_set::const_iterator i = included.deleted_dirs.begin();
         i != included.deleted_dirs.end(); ++i)
    unknown.erase(*i);

  classify_paths(app, new_paths, m_old, missing, changed, unchanged);

  inventory_paths(inventory, missing, inventory_item::MISSING_DATA);

  inventory_paths(inventory, included.deleted_files, inventory_item::DROPPED_PATH);
  inventory_paths(inventory, included.deleted_dirs, inventory_item::DROPPED_PATH, inventory_item::DIRECTORY);

  inventory_paths(inventory, included.renamed_files, inventory_item::RENAMED_PATH);
  inventory_paths(inventory, included.renamed_dirs, inventory_item::RENAMED_PATH, inventory_item::DIRECTORY);

  inventory_paths(inventory, included.added_files, inventory_item::ADDED_PATH);
  inventory_paths(inventory, changed, inventory_item::PATCHED_DATA);
  
  inventory_paths(inventory, unchanged, inventory_item::UNCHANGED_DATA);
  inventory_paths(inventory, unknown, inventory_item::UNKNOWN_PATH);
  inventory_paths(inventory, ignored, inventory_item::IGNORED_PATH);

  for (inventory_map::const_iterator i = inventory.begin(); i != inventory.end(); ++i)
    {
      switch (inventory[i->first].path_status) 
        {
        case inventory_item::UNCHANGED_PATH: output << " "; break;
        case inventory_item::ADDED_PATH:     output << "+"; break;
        case inventory_item::DROPPED_PATH:   output << "-"; break;
        case inventory_item::RENAMED_PATH:   output << "%"; break;
        case inventory_item::UNKNOWN_PATH:   output << "?"; break;
        case inventory_item::IGNORED_PATH:   output << "~"; break;
        }

      switch (inventory[i->first].data_status) 
        {
        case inventory_item::UNCHANGED_DATA: output << " "; break;
        case inventory_item::PATCHED_DATA:   output << "#"; break;
        case inventory_item::MISSING_DATA:   output << "!"; break;
        }

      output << " ";

      switch (inventory[i->first].path_type) 
        {
        case inventory_item::FILE: 
          if (inventory[i->first].path_status == inventory_item::RENAMED_PATH)
            output << basic_io::escape(inventory[i->first].old_path()) << " "; 
          
          output << basic_io::escape(i->first()); 
          break;

        case inventory_item::DIRECTORY: 
          if (inventory[i->first].path_status == inventory_item::RENAMED_PATH)
            output << basic_io::escape(inventory[i->first].old_path() + "/") << " "; 
         
          output << basic_io::escape(i->first() + "/"); 
          break;
        }
      
      output << std::endl;
    }
 
}

void
automate_command(utf8 cmd, std::vector<utf8> args,
                 std::string const & root_cmd_name,
                 app_state & app,
                 std::ostream & output)
{
  if (cmd() == "interface_version")
    automate_interface_version(args, root_cmd_name, app, output);
  else if (cmd() == "heads")
    automate_heads(args, root_cmd_name, app, output);
  else if (cmd() == "ancestors")
    automate_ancestors(args, root_cmd_name, app, output);
  else if (cmd() == "descendents")
    automate_descendents(args, root_cmd_name, app, output);
  else if (cmd() == "erase_ancestors")
    automate_erase_ancestors(args, root_cmd_name, app, output);
  else if (cmd() == "toposort")
    automate_toposort(args, root_cmd_name, app, output);
  else if (cmd() == "ancestry_difference")
    automate_ancestry_difference(args, root_cmd_name, app, output);
  else if (cmd() == "leaves")
    automate_leaves(args, root_cmd_name, app, output);
  else if (cmd() == "parents")
    automate_parents(args, root_cmd_name, app, output);
  else if (cmd() == "children")
    automate_children(args, root_cmd_name, app, output);
  else if (cmd() == "graph")
    automate_graph(args, root_cmd_name, app, output);
  else if (cmd() == "select")
    automate_select(args, root_cmd_name, app, output);
  else if (cmd() == "inventory")
    automate_inventory(args, root_cmd_name, app, output);
  else
    throw usage(root_cmd_name);
}
