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
#include <sstream>
#include <unistd.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/tuple/tuple.hpp>

#include "app_state.hh"
#include "basic_io.hh"
#include "commands.hh"
#include "constants.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "keys.hh"

static std::string const interface_version = "1.1";

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

// Name: attributes
// Arguments:
//   1: file name (optional, if non-existant prints all files with attributes)
// Added in: 1.0
// Purpose: Prints all attributes for a file, or all  all files with attributes
//   if a file name provided.
// Output format: A list of file names in alphabetically sorted order,
//   or a list of attributes if a file name provided.
// Error conditions: If the file name has no attributes, prints nothing.
static void
automate_attributes(std::vector<utf8> args,
                    std::string const & help_name,
                    app_state & app,
                    std::ostream & output)
{
  if (args.size() > 1)
    throw usage(help_name);

  // is there an .mt-attrs?
  file_path attr_path;
  get_attr_path(attr_path);
  if (!file_exists(attr_path)) return;

  // read attribute map 
  data attr_data;
  attr_map attrs;

  read_data(attr_path, attr_data);
  read_attr_map(attr_data, attrs);

  if (args.size() == 1) {
    // a filename was given, if it has attributes, print them
    file_path path = file_path_external(idx(args,0));
    attr_map::const_iterator i = attrs.find(path);
    if (i == attrs.end()) return;

    for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
         j != i->second.end(); ++j)
      output << j->first << std::endl;
  }
  else {
    for (attr_map::const_iterator i = attrs.begin(); i != attrs.end(); ++i)
      {
        output << (*i).first << std::endl;
      }
  }
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

// consider a changeset with the following
//
// deletions
// renames from to
// additions
//
// pre-state  corresponds to deletions and the "from" side of renames
// post-state corresponds to the "to" side of renames and additions
// file-state corresponds to the state of the file with the given name
//
// pre and post state are related to the path rearrangement specified in MT/work
// file state is related to the details of the resulting file

struct inventory_item
{
  enum pstate 
    { KNOWN_PATH, ADDED_PATH, DROPPED_PATH, RENAMED_PATH } 
    pre_state, post_state;

  enum fstate
    { KNOWN_FILE, PATCHED_FILE, MISSING_FILE, UNKNOWN_FILE, IGNORED_FILE } 
    file_state;

  enum ptype
    { FILE, DIRECTORY } 
    path_type;

  size_t pre_id, post_id;

  inventory_item():
    pre_state(KNOWN_PATH), post_state(KNOWN_PATH), 
    file_state(KNOWN_FILE), 
    path_type(FILE),
    pre_id(0), post_id(0) {}
};

typedef std::map<file_path, inventory_item> inventory_map;

static void
inventory_pre_state(inventory_map & inventory,
                    path_set const & paths,
                    inventory_item::pstate pre_state, 
                    size_t id = 0,
                    inventory_item::ptype path_type = inventory_item::FILE)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(F("%d %d %s\n") % inventory[*i].pre_state % pre_state % *i);
      I(inventory[*i].pre_state == inventory_item::KNOWN_PATH);
      inventory[*i].pre_state = pre_state;
      inventory[*i].path_type = path_type;
      if (id != 0) 
        {
          I(inventory[*i].pre_id == 0);
          inventory[*i].pre_id = id;
        }
    }
}

static void
inventory_post_state(inventory_map & inventory,
                     path_set const & paths,
                     inventory_item::pstate post_state, 
                     size_t id = 0,
                     inventory_item::ptype path_type = inventory_item::FILE)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(F("%d %d %s\n") % inventory[*i].post_state % post_state % *i);
      I(inventory[*i].post_state == inventory_item::KNOWN_PATH);
      inventory[*i].post_state = post_state;
      inventory[*i].path_type = path_type;
      if (id != 0) 
        {
          I(inventory[*i].post_id == 0);
          inventory[*i].post_id = id;
        }
    }
}

static void
inventory_file_state(inventory_map & inventory,
                     path_set const & paths,
                     inventory_item::fstate file_state)
{
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); i++)
    {
      L(F("%d %d %s\n") % inventory[*i].file_state % file_state % *i);
      I(inventory[*i].file_state == inventory_item::KNOWN_FILE);
      inventory[*i].file_state = file_state;
    }
}

static void
inventory_renames(inventory_map & inventory,
                  std::map<file_path,file_path> const & renames,
                  inventory_item::ptype path_type = inventory_item::FILE)
{
  path_set old_name;
  path_set new_name;

  static size_t id = 1;

  for (std::map<file_path,file_path>::const_iterator i = renames.begin(); 
       i != renames.end(); i++)
    {
      old_name.insert(i->first);
      new_name.insert(i->second);

      inventory_pre_state(inventory, old_name, inventory_item::RENAMED_PATH, id, path_type);
      inventory_post_state(inventory, new_name, inventory_item::RENAMED_PATH, id, path_type);

      id++;

      old_name.clear();
      new_name.clear();
    }
}
               
// Name: inventory
// Arguments: none
// Added in: 1.0
// Purpose: Prints a summary of every file found in the working copy or its
//   associated base manifest. Each unique path is listed on a line prefixed by
//   three status characters and two numeric values used for identifying
//   renames. The three status characters are as follows.
//
//   column 1 pre-state
//         ' ' the path was unchanged in the pre-state
//         'D' the path was deleted from the pre-state
//         'R' the path was renamed from the pre-state name
//   column 2 post-state
//         ' ' the path was unchanged in the post-state
//         'R' the path was renamed to the post-state name
//         'A' the path was added to the post-state
//   column 3 file-state
//         ' ' the file is known and unchanged from the current manifest version
//         'P' the file is patched to a new version
//         'U' the file is unknown and not included in the current manifest
//         'I' the file is ignored and not included in the current manifest
//         'M' the file is missing but is included in the current manifest
//
// Output format: Each path is printed on its own line, prefixed by three status
//   characters as described above. The status is followed by a single space and
//   two numbers, each separated by a single space, used for identifying renames.
//   The numbers are followed by a single space and then the pathname, which 
//   includes the rest of the line. Directory paths are identified as ending with
//   the "/" character, file paths do not end in this character.
//
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
  manifest_map m_old, m_new;
  path_set old_paths, new_paths, empty;
  change_set::path_rearrangement included, excluded;
  change_set cs;
  path_set missing, changed, unchanged, unknown, ignored;
  inventory_map inventory;
  app.require_working_copy();

  calculate_restricted_rearrangement(app, args, 
                                     old_manifest_id, old_revision_id,
                                     m_old, old_paths, new_paths,
                                     included, excluded);

  // this is a bit screwey. we need to rearrange the old manifest 
  // according to the included rearrangement and for that we need
  // a complete changeset, which is normally obtained from both 
  // the old and the new manifest. we can't do that because there 
  // may be missing files, so instead we add our own set of deltas
  // below.

  // we have the rearrangement of the changeset from above
  // now we need to build up the deltas for the added files

  cs.rearrangement = included;

  hexenc<id> null_ident;

  for (path_set::const_iterator 
         i = included.added_files.begin();
       i != included.added_files.end(); ++i)
    {
      if (path_exists(*i))
        {
          // add path from [] to [xxx]
          hexenc<id> ident;
          calculate_ident(*i, ident, app.lua);
          cs.deltas.insert(std::make_pair(*i,std::make_pair(null_ident, ident)));
        }
      else
        {
          // remove missing files from the added list since they have not deltas
          missing.insert(*i);
          cs.rearrangement.added_files.erase(*i);
        }
    }

  apply_change_set(m_old, cs, m_new);

  classify_manifest_paths(app, m_new, missing, changed, unchanged);

  // remove the remaining added files from the unchanged set since they have been 
  // changed in the deltas construction above. also, only consider the file as 
  // changed if its not missing

  for (path_set::const_iterator 
         i = included.added_files.begin();
       i != included.added_files.end(); ++i)
    {
      unchanged.erase(*i);
      if (missing.find(*i) == missing.end()) 
        changed.insert(*i);
    }

  file_itemizer u(app, new_paths, unknown, ignored);
  walk_tree(file_path(), u);

  inventory_file_state(inventory, missing, inventory_item::MISSING_FILE);

  inventory_pre_state(inventory, included.deleted_files, inventory_item::DROPPED_PATH);
  inventory_pre_state(inventory, included.deleted_dirs, 
                      inventory_item::DROPPED_PATH, inventory_item::DIRECTORY);

  inventory_renames(inventory, included.renamed_files);
  inventory_renames(inventory, included.renamed_dirs, inventory_item::DIRECTORY);

  inventory_post_state(inventory, included.added_files, inventory_item::ADDED_PATH);

  inventory_file_state(inventory, changed, inventory_item::PATCHED_FILE);
  inventory_file_state(inventory, unchanged, inventory_item::KNOWN_FILE);
  inventory_file_state(inventory, unknown, inventory_item::UNKNOWN_FILE);
  inventory_file_state(inventory, ignored, inventory_item::IGNORED_FILE);

  for (inventory_map::const_iterator i = inventory.begin(); i != inventory.end(); ++i)
    {
      switch (inventory[i->first].pre_state) 
        {
        case inventory_item::KNOWN_PATH:   output << " "; break;
        case inventory_item::DROPPED_PATH: output << "D"; break;
        case inventory_item::RENAMED_PATH: output << "R"; break;
        default: I(false); // invalid pre_state
        }

      switch (inventory[i->first].post_state) 
        {
        case inventory_item::KNOWN_PATH:   output << " "; break;
        case inventory_item::RENAMED_PATH: output << "R"; break;
        case inventory_item::ADDED_PATH:   output << "A"; break;
        default: I(false); // invalid post_state
        }

      switch (inventory[i->first].file_state) 
        {
        case inventory_item::KNOWN_FILE:   output << " "; break;
        case inventory_item::PATCHED_FILE: output << "P"; break;
        case inventory_item::UNKNOWN_FILE: output << "U"; break;
        case inventory_item::IGNORED_FILE: output << "I"; break;
        case inventory_item::MISSING_FILE: output << "M"; break;
        }

      // need directory indicators

      output << " " << inventory[i->first].pre_id 
             << " " << inventory[i->first].post_id 
             << " " << i->first;

      if (inventory[i->first].path_type  == inventory_item::DIRECTORY)
        output << "/";

      output << std::endl;
    }
 
}

// Name: certs
// Arguments:
//   1: a revision id
// Added in: 1.0
// Purpose: Prints all certificates associated with the given revision ID.
//   Each certificate is contained in a basic IO stanza. For each certificate, 
//   the following values are provided:
//   
//   'key' : a string indicating the key used to sign this certificate.
//   'signature': a string indicating the status of the signature. Possible 
//   values of this string are:
//     'ok'        : the signature is correct
//     'bad'       : the signature is invalid
//     'unknown'   : signature was made with an unknown key
//   'name' : the name of this certificate
//   'value' : the value of this certificate
//   'trust' : is this certificate trusted by the defined trust metric
//   Possible values of this string are:
//     'trusted'   : this certificate is trusted
//     'untrusted' : this certificate is not trusted
//
// Output format: All stanzas are formatted by basic_io. Stanzas are seperated 
// by a blank line. Values will be escaped, '\' -> '\\' and '"' -> '\"'.
//
// Error conditions: If a certificate is signed with an unknown public key, a 
// warning message is printed to stderr. If the revision specified is unknown 
// or invalid prints an error message to stderr and exits with status 1.
static void
automate_certs(std::vector<utf8> args,
                 std::string const & help_name,
                 app_state & app,
                 std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);

  std::vector<cert> certs;
  
  transaction_guard guard(app.db);
  
  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  hexenc<id> ident(rid.inner());

  std::vector< revision<cert> > ts;
  app.db.get_revision_certs(rid, ts);
  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    std::set<rsa_keypair_id> checked;      
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !app.db.public_key_exists(idx(certs, i).key))
          P(F("warning: no public key '%s' found in database\n")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }
        
  // Make the output deterministic; this is useful for the test suite, in
  // particular.
  std::sort(certs.begin(), certs.end());

  basic_io::printer pr(output);

  for (size_t i = 0; i < certs.size(); ++i)
    {
      basic_io::stanza st;
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;      
      cert_name name = idx(certs, i).name();
      std::set<rsa_keypair_id> signers;

      decode_base64(idx(certs, i).value, tv);

      rsa_keypair_id keyid = idx(certs, i).key();
      signers.insert(keyid);

      bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                          name, tv);

      st.push_str_pair("key", keyid());

      std::string stat;
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
      st.push_str_pair("signature", stat);

      st.push_str_pair("name", name());
      st.push_str_pair("value", tv());
      st.push_str_pair("trust", (trusted ? "trusted" : "untrusted"));

      pr.print_stanza(st);
    }

  guard.commit();
}

// Name: get_revision
// Arguments:
//   1: a revision id (optional, determined from working directory if non-existant)
// Added in: 1.0
// Purpose: Prints changeset information for the specified revision id.
//
// There are several changes that are described; each of these is described by 
// a different basic_io stanza. The first string pair of each stanza indicates the 
// type of change represented. 
//
// Possible values of this first value are along with an ordered list of 
// basic_io formatted string pairs that will be provided are:
//
//  'old_revision' : represents a parent revision.
//                   format: ('old_revision', revision id)
//  'new_manifest' : represents the new manifest associated with the revision.
//                   format: ('new_manifest', manifest id)
//  'old_manifest' : represents a manifest associated with a parent revision.
//                   format: ('old_manifest', manifest id)
//  'patch' : represents a file that was modified.
//            format: ('patch', filename), ('from', file id), ('to', file id)
//  'add_file' : represents a file that was added.
//               format: ('add_file', filename)
//  'delete_file' : represents a file that was deleted.
//                  format: ('delete_file', filename)
//  'delete_dir' : represents a directory that was deleted.
//                 format: ('delete_dir', filename)
//  'rename_file' : represents a file that was renamed.
//                  format: ('rename_file', old filename), ('to', new filename)
//  'rename_dir' : represents a directory that was renamed.
//                 format: ('rename_dir', old filename), ('to', new filename)
//
// Output format: All stanzas are formatted by basic_io. Stanzas are seperated 
// by a blank line. Values will be escaped, '\' -> '\\' and '"' -> '\"'.
//
// Error conditions: If the revision specified is unknown or invalid prints an 
// error message to stderr and exits with status 1.
static void
automate_get_revision(std::vector<utf8> args,
                 std::string const & help_name,
                 app_state & app,
                 std::ostream & output)
{
  if (args.size() > 1)
    throw usage(help_name);

  revision_data dat;
  revision_id ident;

  if (args.size() == 0)
    {
      revision_set rev;
      manifest_map m_old, m_new;

      app.require_working_copy(); 
      calculate_unrestricted_revision(app, rev, m_old, m_new);
      calculate_ident(rev, ident);
      write_revision_set(rev, dat);
    }
  else
    {
      ident = revision_id(idx(args, 0)());
      N(app.db.revision_exists(ident),
        F("no revision %s found in database") % ident);
      app.db.get_revision(ident, dat);
    }

  L(F("dumping revision %s\n") % ident);
  output.write(dat.inner()().data(), dat.inner()().size());
}

// Name: get_manifest
// Arguments:
//   1: a manifest id (optional, determined from working directory if non-existant)
// Added in: 1.0
// Purpose: Prints the contents of the manifest associated with the given manifest ID.
//
// Output format: One line for each file in the manifest. Each line begins with a 
// 40 character file ID, followed by two space characters (' ') and then the filename.
// eg:
// 22382ac1bdffec21170a88ff2580fe39b508243f  vocab.hh
//
// Error conditions:  If the manifest ID specified is unknown or invalid prints an 
// error message to stderr and exits with status 1.
static void
automate_get_manifest(std::vector<utf8> args,
                 std::string const & help_name,
                 app_state & app,
                 std::ostream & output)
{
  if (args.size() > 1)
    throw usage(help_name);

  manifest_data dat;
  manifest_id ident;

  if (args.size() == 0)
    {
      revision_set rev;
      manifest_map m_old, m_new;

      app.require_working_copy();
      calculate_unrestricted_revision(app, rev, m_old, m_new);

      calculate_ident(m_new, ident);
      write_manifest_map(m_new, dat);
    }
  else
    {
      ident = manifest_id(idx(args, 0)());
      N(app.db.manifest_version_exists(ident),
        F("no manifest version %s found in database") % ident);
      app.db.get_manifest_version(ident, dat);
    }

  L(F("dumping manifest %s\n") % ident);
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
static void
automate_get_file(std::vector<utf8> args,
                 std::string const & help_name,
                 app_state & app,
                 std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);

  file_id ident(idx(args, 0)());
  N(app.db.file_version_exists(ident),
    F("no file version %s found in database") % ident);

  file_data dat;
  L(F("dumping file %s\n") % ident);
  app.db.get_file_version(ident, dat);
  output.write(dat.inner()().data(), dat.inner()().size());
}

void
automate_command(utf8 cmd, std::vector<utf8> args,
                 std::string const & root_cmd_name,
                 app_state & app,
                 std::ostream & output);

// Name: stdio
// Arguments: none
// Added in: 1.0
// Purpose: Allow multiple automate commands to be run from one instance
//   of monotone.
//
// Input format: The input is a series of lines of the form
//   'l'<size>':'<string>[<size>':'<string>...]'e', with characters
//   after the 'e' of one command, but before the 'l' of the next ignored.
//   This space is reserved, and should not contain characters other
//   than '\n'.
//   Example:
//     l6:leavese
//     l7:parents40:0e3171212f34839c2e3263e7282cdeea22fc5378e
//
// Output format: <command number>:<err code>:<last?>:<size>:<output>
//   <command number> is a decimal number specifying which command
//   this output is from. It is 0 for the first command, and increases
//   by one each time.
//   <err code> is 0 for success, 1 for a syntax error, and 2 for any
//   other error.
//   <last?> is 'l' if this is the last piece of output for this command,
//   and 'm' if there is more output to come.
//   <size> is the number of bytes in the output.
//   <output> is the output of the command.
//   Example:
//     0:0:l:205:0e3171212f34839c2e3263e7282cdeea22fc5378
//     1f4ef73c3e056883c6a5ff66728dd764557db5e6
//     2133c52680aa2492b18ed902bdef7e083464c0b8
//     23501f8afd1f9ee037019765309b0f8428567f8a
//     2c295fcf5fe20301557b9b3a5b4d437b5ab8ec8c
//     1:0:l:41:7706a422ccad41621c958affa999b1a1dd644e79
//
// Error conditions: Errors encountered by the commands run only set the error
//   code in the output for that command. Malformed input results in exit with
//   a non-zero return value and an error message.

//We use our own stringbuf class so we can put in a callback on write.
//This lets us dump output at a set length, rather than waiting until
//we have all of the output.
typedef std::basic_stringbuf<char,
                             std::char_traits<char>,
                             std::allocator<char> > char_stringbuf;
struct my_stringbuf : public char_stringbuf
{
private:
  std::streamsize written;
  boost::function1<void, int> on_write;
  std::streamsize last_call;
  std::streamsize call_every;
  bool clear;
public:
  my_stringbuf() : char_stringbuf(),
                   written(0),
                   last_call(0),
                   call_every(constants::automate_stdio_size)
  {}
  virtual std::streamsize
  xsputn(const char_stringbuf::char_type* __s, std::streamsize __n)
  {
    std::streamsize ret=char_stringbuf::xsputn(__s, __n);
    written+=__n;
    while(written>=last_call+call_every)
      {
        if(on_write)
          on_write(call_every);
        last_call+=call_every;
      }
    return ret;
  }
  virtual int sync()
  {
    int ret=char_stringbuf::sync();
    if(on_write)
      on_write(-1);
    last_call=written;
    return ret;
  }
  void set_on_write(boost::function1<void, int> x)
  {
    on_write = x;
  }
};

void print_some_output(int cmdnum,
                       int err,
                       bool last,
                       std::string const & text,
                       std::ostream & s,
                       int & pos,
                       int size)
{
  if(size==-1)
    {
      while(text.size()-pos > constants::automate_stdio_size)
        {
          s<<cmdnum<<':'<<err<<':'<<'m'<<':';
          s<<constants::automate_stdio_size<<':'
           <<text.substr(pos, constants::automate_stdio_size);
          pos+=constants::automate_stdio_size;
          s.flush();
        }
      s<<cmdnum<<':'<<err<<':'<<(last?'l':'m')<<':';
      s<<(text.size()-pos)<<':'<<text.substr(pos);
      pos=text.size();
    }
  else
    {
      I((unsigned int)(size) <= constants::automate_stdio_size);
      s<<cmdnum<<':'<<err<<':'<<(last?'l':'m')<<':';
      s<<size<<':'<<text.substr(pos, size);
      pos+=size;
    }
  s.flush();
}

static void
automate_stdio(std::vector<utf8> args,
                   std::string const & help_name,
                   app_state & app,
                   std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);
  int cmdnum = 0;
  char c;
  ssize_t n=1;
  while(n)//while(!EOF)
    {
      std::string x;
      utf8 cmd;
      args.clear();
      bool first=true;
      int toklen=0;
      bool firstchar=true;
      for(n=read(0, &c, 1); c != 'l' && n; n=read(0, &c, 1))
        ;
      for(n=read(0, &c, 1); c!='e' && n; n=read(0, &c, 1))
        {
          if(c<='9' && c>='0')
            {
              toklen=(toklen*10)+(c-'0');
            }
          else if(c == ':')
            {
              char *tok=new char[toklen];
              int count=0;
              while(count<toklen)
                count+=read(0, tok, toklen-count);
              if(first)
                cmd=utf8(std::string(tok, toklen));
              else
                args.push_back(utf8(std::string(tok, toklen)));
              toklen=0;
              delete[] tok;
              first=false;
            }
          else
            {
              N(false, F("Bad input to automate stdio"));
            }
          firstchar=false;
        }
      if(cmd() != "")
        {
          int outpos=0;
          int err;
          std::ostringstream s;
          my_stringbuf sb;
          sb.set_on_write(boost::bind(print_some_output,
                                      cmdnum,
                                      boost::ref(err),
                                      false,
                                      boost::bind(&my_stringbuf::str, &sb),
                                      boost::ref(output),
                                      boost::ref(outpos),
                                      _1));
          s.std::basic_ios<char, std::char_traits<char> >::rdbuf(&sb);
          try
            {
              err=0;
              automate_command(cmd, args, help_name, app, s);
            }
          catch(usage & u)
            {
              if(sb.str().size())
                s.flush();
              err=1;
              commands::explain_usage(help_name, s);
            }
          catch(informative_failure & f)
            {
              if(sb.str().size())
                s.flush();
              err=2;
              //Do this instead of printing f.what directly so the output
              //will be split into properly-sized blocks automatically.
              s<<f.what;
            }
            print_some_output(cmdnum, err, true, sb.str(),
                              output, outpos, -1);
        }
      cmdnum++;
    }
}

// Name: keys
// Arguments: none
// Added in: 1.1
// Purpose: Prints all keys in the keystore, and if a database is given
//   also all keys in the database, in basic_io format.
// Output format: For each key, a basic_io stanza is printed. The items in
//   the stanza are:
//     name - the key identifier
//     public_hash - the hash of the public half of the key
//     private_hash - the hash of the private half of the key
//     public_location - where the public half of the key is stored
//     private_location - where the private half of the key is stored
//   The *_location items may have multiple values, as shown below
//   for public_location.
//   If the private key does not exist, then the private_hash and
//   private_location items will be absent.
//
// Sample output:
//               name "tbrownaw@gmail.com"
//        public_hash [475055ec71ad48f5dfaf875b0fea597b5cbbee64]
//       private_hash [7f76dae3f91bb48f80f1871856d9d519770b7f8a]
//    public_location "database" "keystore"
//   private_location "keystore"
//
//              name "njs@pobox.com"
//       public_hash [de84b575d5e47254393eba49dce9dc4db98ed42d]
//   public_location "database"
//
//               name "foo@bar.com"
//        public_hash [7b6ce0bd83240438e7a8c7c207d8654881b763f6]
//       private_hash [bfc3263e3257087f531168850801ccefc668312d]
//    public_location "keystore"
//   private_location "keystore"
//
// Error conditions: None.
static void
automate_keys(std::vector<utf8> args, std::string const & help_name,
              app_state & app, std::ostream & output)
{
  if (args.size() != 0)
    throw usage(help_name);
  std::vector<rsa_keypair_id> dbkeys;
  std::vector<rsa_keypair_id> kskeys;
  // public_hash, private_hash, public_location, private_location
  std::map<std::string, boost::tuple<hexenc<id>, hexenc<id>,
                                     std::vector<std::string>,
                                     std::vector<std::string> > > items;
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      app.db.get_key_ids("", dbkeys);
      guard.commit();
    }
  app.keys.get_key_ids("", kskeys);

  for (std::vector<rsa_keypair_id>::iterator i = dbkeys.begin();
       i != dbkeys.end(); i++)
    {
      base64<rsa_pub_key> pub_encoded;
      hexenc<id> hash_code;

      app.db.get_key(*i, pub_encoded);
      key_hash_code(*i, pub_encoded, hash_code);
      items[(*i)()].get<0>() = hash_code;
      items[(*i)()].get<2>().push_back("database");
    }

  for (std::vector<rsa_keypair_id>::iterator i = kskeys.begin();
       i != kskeys.end(); i++)
    {
      keypair kp;
      hexenc<id> privhash, pubhash;
      app.keys.get_key_pair(*i, kp); 
      key_hash_code(*i, kp.pub, pubhash);
      key_hash_code(*i, kp.priv, privhash);
      items[(*i)()].get<0>() = pubhash;
      items[(*i)()].get<1>() = privhash;
      items[(*i)()].get<2>().push_back("keystore");
      items[(*i)()].get<3>().push_back("keystore");
    }
  basic_io::printer prt(output);
  for (std::map<std::string, boost::tuple<hexenc<id>, hexenc<id>,
                                     std::vector<std::string>,
                                     std::vector<std::string> > >::iterator
         i = items.begin(); i != items.end(); ++i)
    {
      basic_io::stanza stz;
      stz.push_str_pair("name", i->first);
      stz.push_hex_pair("public_hash", i->second.get<0>()());
      if (!i->second.get<1>()().empty())
        stz.push_hex_pair("private_hash", i->second.get<1>()());
      stz.push_str_multi("public_location", i->second.get<2>());
      if (!i->second.get<3>().empty())
        stz.push_str_multi("private_location", i->second.get<3>());
      prt.print_stanza(stz);
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
  else if (cmd() == "attributes")
    automate_attributes(args, root_cmd_name, app, output);
  else if (cmd() == "stdio")
    automate_stdio(args, root_cmd_name, app, output);
  else if (cmd() == "certs")
    automate_certs(args, root_cmd_name, app, output);
  else if (cmd() == "get_revision")
    automate_get_revision(args, root_cmd_name, app, output);
  else if (cmd() == "get_manifest")
    automate_get_manifest(args, root_cmd_name, app, output);
  else if (cmd() == "get_file")
    automate_get_file(args, root_cmd_name, app, output);
  else if (cmd() == "keys")
    automate_keys(args, root_cmd_name, app, output);
  else
    throw usage(root_cmd_name);
}
