// copyright (C) 2004 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iostream>
#include <iterator>

#include "vocab.hh"
#include "app_state.hh"
#include "commands.hh"
#include "revision.hh"

static std::string const interface_version = "0.1";

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
//   1: a branch name
// Added in: 0.0
// Purpose: Prints the heads of the given branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are sorted.
// Error conditions: If the branch does not exist, prints nothing.  (There are
//   no heads.)
static void
automate_heads(std::vector<utf8> args,
               std::string const & help_name,
               app_state & app,
               std::ostream & output)
{
  if (args.size() != 1)
    throw usage(help_name);

  std::set<revision_id> heads;
  get_branch_heads(idx(args, 0)(), app, heads);
  for (std::set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
    output << (*i).inner()() << std::endl;
}

// Name: descendents
// Arguments:
//   1 or more: revision ids
// Added in: 0.1
// Purpose: Prints the descendents (exclusive) of the given revisions
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are sorted.
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
//   1 or more: revision ids
// Added in: 0.1
// Purpose: Prints all arguments, except those that are an ancestor of some
//   other argument.  One way to think about this is that it prints the
//   minimal elements of the given set, under the ordering imposed by the
//   "child of" relation.  Another way to think of it is if the arguments were
//   a branch, then we print the heads of that branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are sorted.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
static void
automate_erase_ancestors(std::vector<utf8> args,
                         std::string const & help_name,
                         app_state & app,
                         std::ostream & output)
{
  if (args.size() == 0)
    throw usage(help_name);

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
  else if (cmd() == "descendents")
    automate_descendents(args, root_cmd_name, app, output);
  else if (cmd() == "erase_ancestors")
    automate_erase_ancestors(args, root_cmd_name, app, output);
  else
    throw usage(root_cmd_name);
}
