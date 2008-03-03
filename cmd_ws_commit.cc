// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <map>

#include "cmd.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "transforms.hh"
#include "work.hh"
#include "charset.hh"
#include "ui.hh"
#include "app_state.hh"
#include "project.hh"
#include "basic_io.hh"
#include "xdelta.hh"
#include "keys.hh"
#include "key_store.hh"
#include "simplestring_xform.hh"
#include "database.hh"
#include "roster.hh"

using std::cout;
using std::make_pair;
using std::pair;
using std::make_pair;
using std::map;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

static void
revision_summary(revision_t const & rev, branch_name const & branch, utf8 & summary)
{
  string out;
  // We intentionally do not collapse the final \n into the format
  // strings here, for consistency with newline conventions used by most
  // other format strings.
  out += (F("Current branch: %s") % branch).str() += '\n';
  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      // A colon at the end of this string looked nicer, but it made
      // double-click copying from terminals annoying.
      out += (F("Changes against parent %s")
                % encode_hexenc(parent.inner()())).str() += '\n';

      cset const & cs = edge_changes(*i);

      if (cs.empty())
        out += F("  no changes").str() += '\n';

      for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
            i != cs.nodes_deleted.end(); ++i)
        out += (F("  dropped  %s") % *i).str() += '\n';

      for (map<file_path, file_path>::const_iterator
            i = cs.nodes_renamed.begin();
            i != cs.nodes_renamed.end(); ++i)
        out += (F("  renamed  %s\n"
                   "       to  %s") % i->first % i->second).str() += '\n';

      for (set<file_path>::const_iterator i = cs.dirs_added.begin();
            i != cs.dirs_added.end(); ++i)
        out += (F("  added    %s") % *i).str() += '\n';

      for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
            i != cs.files_added.end(); ++i)
        out += (F("  added    %s") % i->first).str() += '\n';

      for (map<file_path, pair<file_id, file_id> >::const_iterator
              i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
        out += (F("  patched  %s") % (i->first)).str() += '\n';

      for (map<pair<file_path, attr_key>, attr_value >::const_iterator
             i = cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
        out += (F("  attr on  %s\n"
                   "    attr   %s\n"
                   "    value  %s")
                 % (i->first.first) % (i->first.second) % (i->second)
                 ).str() += "\n";

      for (set<pair<file_path, attr_key> >::const_iterator
             i = cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
        out += (F("  unset on %s\n"
                   "      attr %s")
                 % (i->first) % (i->second)).str() += "\n";
    }
    summary = utf8(out);
}

static void
get_log_message_interactively(lua_hooks & lua, workspace & work,
                              revision_t const & cs,
                              branch_name const & branchname,
                              utf8 & log_message)
{
  utf8 summary;
  revision_summary(cs, branchname, summary);
  external summary_external;
  utf8_to_system_best_effort(summary, summary_external);

  utf8 branch_comment = utf8((F("branch \"%s\"\n\n") % branchname).str());
  external branch_external;
  utf8_to_system_best_effort(branch_comment, branch_external);

  string magic_line = _("*****DELETE THIS LINE TO CONFIRM YOUR COMMIT*****");
  string commentary_str;
  commentary_str += string(70, '-') + "\n";
  commentary_str += _("Enter a description of this change.\n"
                      "Lines beginning with `MTN:' "
                      "are removed automatically.");
  commentary_str += "\n\n";
  commentary_str += summary_external();
  commentary_str += string(70, '-') + "\n";

  external commentary(commentary_str);

  utf8 user_log_message;
  work.read_user_log(user_log_message);

  //if the _MTN/log file was non-empty, we'll append the 'magic' line
  utf8 user_log;
  if (user_log_message().length() > 0)
    user_log = utf8( magic_line + "\n" + user_log_message());
  else
    user_log = user_log_message;

  external user_log_message_external;
  utf8_to_system_best_effort(user_log, user_log_message_external);

  external log_message_external;
  N(lua.hook_edit_comment(commentary, user_log_message_external,
                          log_message_external),
    F("edit of log message failed"));

  N(log_message_external().find(magic_line) == string::npos,
    F("failed to remove magic line; commit cancelled"));

  system_to_utf8(log_message_external, log_message);
}

CMD(revert, "revert", "", CMD_REF(workspace), N_("[PATH]..."),
    N_("Reverts files and/or directories"),
    N_("In order to revert the entire workspace, specify \".\" as the "
       "file name."),
    options::opts::depth | options::opts::exclude | options::opts::missing)
{
  roster_t old_roster, new_roster;
  cset preserved;

  N(app.opts.missing || !args.empty() || !app.opts.exclude_patterns.empty(),
    F("you must pass at least one path to 'revert' (perhaps '.')"));

  database db(app);
  workspace work(app);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  N(parents.size() == 1,
    F("this command can only be used in a single-parent workspace"));
  old_roster = parent_roster(parents.begin());

  {
    temp_node_id_source nis;
    work.get_current_roster_shape(db, nis, new_roster);
  }

  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_roster, new_roster);

  if (app.opts.missing)
    {
      // --missing is a further filter on the files included by a
      // restriction we first find all missing files included by the
      // specified args and then make a restriction that includes only
      // these missing files.
      set<file_path> missing;
      work.find_missing(new_roster, mask, missing);
      if (missing.empty())
        {
          P(F("no missing files to revert"));
          return;
        }

      std::vector<file_path> missing_files;
      for (set<file_path>::const_iterator i = missing.begin();
           i != missing.end(); i++)
        {
          L(FL("reverting missing file: %s") % *i);
          missing_files.push_back(*i);
        }
      // replace the original mask with a more restricted one
      mask = node_restriction(work, missing_files,
                              std::vector<file_path>(), app.opts.depth,
                              old_roster, new_roster);
    }

  // We want the restricted roster to include all the changes
  // that are to be *kept*. Then, the changes to revert are those
  // from the new roster *back* to the restricted roster

  roster_t restricted_roster;
  make_restricted_roster(new_roster, old_roster, restricted_roster,
                         mask);

  make_cset(old_roster, restricted_roster, preserved);

  // The preserved cset will be left pending in MTN/revision

  // if/when reverting through the editable_tree interface use
  // make_cset(new_roster, restricted_roster, reverted);
  // to get a cset that gets us back to the restricted roster
  // from the current workspace roster

  // the intermediate paths record the paths of all directory nodes
  // paths we reverted on the fly for descendant nodes below them.
  // if a children of such a directory node should be recreated, we use
  // this recorded path here instead of just
  //  a) the node's old name, which could eventually be wrong if the parent
  //     path is a rename_target (i.e. a new path), see the
  //     "revert_drop_not_rename" test
  //  b) the parent node's new name + the basename of the old name,
  //     which may be wrong as well in case of a more complex pivot_rename

  std::map<node_id, file_path> intermediate_paths;
  node_map const & nodes = old_roster.all_nodes();

  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      if (old_roster.is_root(nid))
        continue;

      if (!mask.includes(old_roster, nid))
        continue;

      file_path old_path, new_path, old_parent, new_parent;;
      path_component base;

      old_roster.get_name(nid, old_path);
      old_path.dirname_basename(old_parent, base);

      // if we recorded the parent node in this rename already
      // use the intermediate path (i.e. the new new_path after this
      // action) as target path for the reverted item)
      const std::map<node_id, file_path>::iterator it =
        intermediate_paths.find(node->parent);
      if (it != intermediate_paths.end())
      {
          new_path = it->second / base;
      }
      else
      {
          if (old_roster.is_root(node->parent))
          {
            new_path = file_path() / base;
          }
          else
          {
            new_roster.get_name(node->parent, new_parent);
            new_path = new_parent / base;
          }
      }

      if (is_file_t(node))
        {
          file_t f = downcast_to_file_t(node);
          if (file_exists(new_path))
            {
              file_id ident;
              calculate_ident(new_path, ident);
              // don't touch unchanged files
              if (ident == f->content)
                continue;
              else
                L(FL("skipping unchanged %s") % new_path);
            }

          P(F("reverting %s") % new_path);
          L(FL("reverting %s to [%s]") % new_path % f->content);

          N(db.file_version_exists(f->content),
            F("no file version %s found in database for %s")
            % f->content % new_path);

          file_data dat;
          L(FL("writing file %s to %s")
            % f->content % new_path);
          db.get_file_version(f->content, dat);
          write_data(new_path, dat.inner());
        }
      else
        {
          intermediate_paths.insert(std::pair<node_id, file_path>(nid, new_path));

          if (!directory_exists(new_path))
            {
              P(F("recreating %s/") % new_path);
              mkdir_p(new_path);
            }
          else
            {
              L(FL("skipping existing %s/") % new_path);
            }
        }
    }

  // Included_work is thrown away which effectively reverts any adds,
  // drops and renames it contains. Drops and rename sources will have
  // been rewritten above but this may leave rename targets laying
  // around.

  revision_t remaining;
  make_revision_for_workspace(parent_id(parents.begin()), preserved, remaining);

  // Race.
  work.put_work_rev(remaining);
  work.update_any_attrs(db);
  work.maybe_update_inodeprints(db);
}

CMD(disapprove, "disapprove", "", CMD_REF(review), N_("REVISION"),
    N_("Disapproves a particular revision"),
    "",
    options::opts::branch | options::opts::messages | options::opts::date |
    options::opts::author)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  utf8 log_message("");
  bool log_message_given;
  revision_id r;
  revision_t rev, rev_inverse;
  shared_ptr<cset> cs_inverse(new cset());
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  db.get_revision(r, rev);

  N(rev.edges.size() == 1,
    F("revision %s has %d changesets, cannot invert") % r % rev.edges.size());

  guess_branch(app.opts, project, r);
  N(app.opts.branchname() != "", F("need --branch argument for disapproval"));

  process_commit_message_args(app.opts, log_message_given, log_message,
                              utf8((FL("disapproval of revision '%s'") % r).str()));

  cache_user_key(app.opts, app.lua, db, keys);

  edge_entry const & old_edge (*rev.edges.begin());
  db.get_revision_manifest(edge_old_revision(old_edge),
                               rev_inverse.new_manifest);
  {
    roster_t old_roster, new_roster;
    db.get_roster(edge_old_revision(old_edge), old_roster);
    db.get_roster(r, new_roster);
    make_cset(new_roster, old_roster, *cs_inverse);
  }
  rev_inverse.edges.insert(make_pair(r, cs_inverse));

  {
    transaction_guard guard(db);

    revision_id inv_id;
    revision_data rdat;

    write_revision(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    db.put_revision(inv_id, rdat);

    project.put_standard_certs_from_options(app.opts, app.lua, keys,
                                            inv_id, app.opts.branchname,
                                            log_message);
    guard.commit();
  }
}

CMD(mkdir, "mkdir", "", CMD_REF(workspace), N_("[DIRECTORY...]"),
    N_("Creates directories and adds them to the workspace"),
    "",
    options::opts::no_ignore)
{
  if (args.size() < 1)
    throw usage(execid);

  database db(app);
  workspace work(app);

  set<file_path> paths;
  // spin through args and try to ensure that we won't have any collisions
  // before doing any real filesystem modification.  we'll also verify paths
  // against .mtn-ignore here.
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      file_path fp = file_path_external(*i);
      require_path_is_nonexistent
        (fp, F("directory '%s' already exists") % fp);

      // we'll treat this as a user (fatal) error.  it really wouldn't make
      // sense to add a dir to .mtn-ignore and then try to add it to the
      // project with a mkdir statement, but one never can tell...
      N(app.opts.no_ignore || !work.ignore_file(fp),
        F("ignoring directory '%s' [see .mtn-ignore]") % fp);

      paths.insert(fp);
    }

  // this time, since we've verified that there should be no collisions,
  // we'll just go ahead and do the filesystem additions.
  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    mkdir_p(*i);

  work.perform_additions(db, paths, false, !app.opts.no_ignore);
}

CMD(add, "add", "", CMD_REF(workspace), N_("[PATH]..."),
    N_("Adds files to the workspace"),
    "",
    options::opts::unknown | options::opts::no_ignore |
    options::opts::recursive)
{
  if (!app.opts.unknown && (args.size() < 1))
    throw usage(execid);

  database db(app);
  workspace work(app);

  vector<file_path> roots = args_to_paths(args);

  set<file_path> paths;
  bool add_recursive = app.opts.recursive;
  if (app.opts.unknown)
    {
      path_restriction mask(work, roots,
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth);
      set<file_path> ignored;

      // if no starting paths have been specified use the workspace root
      if (roots.empty())
        roots.push_back(file_path());

      work.find_unknown_and_ignored(db, mask, roots, paths, ignored);

      work.perform_additions(db, ignored,
                                 add_recursive, !app.opts.no_ignore);
    }
  else
    paths = set<file_path>(roots.begin(), roots.end());

  work.perform_additions(db, paths, add_recursive, !app.opts.no_ignore);
}

CMD(drop, "drop", "rm", CMD_REF(workspace), N_("[PATH]..."),
    N_("Drops files from the workspace"),
    "",
    options::opts::bookkeep_only | options::opts::missing | options::opts::recursive)
{
  if (!app.opts.missing && (args.size() < 1))
    throw usage(execid);

  database db(app);
  workspace work(app);

  set<file_path> paths;
  if (app.opts.missing)
    {
      temp_node_id_source nis;
      roster_t current_roster_shape;
      work.get_current_roster_shape(db, nis, current_roster_shape);
      node_restriction mask(work, args_to_paths(args),
                            args_to_paths(app.opts.exclude_patterns),
                            app.opts.depth,
                            current_roster_shape);
      work.find_missing(current_roster_shape, mask, paths);
    }
  else
    {
      vector<file_path> roots = args_to_paths(args);
      paths = set<file_path>(roots.begin(), roots.end());
    }

  work.perform_deletions(db, paths,
                             app.opts.recursive, app.opts.bookkeep_only);
}


CMD(rename, "rename", "mv", CMD_REF(workspace),
    N_("SRC DEST\n"
       "SRC1 [SRC2 [...]] DEST_DIR"),
    N_("Renames entries in the workspace"),
    "",
    options::opts::bookkeep_only)
{
  if (args.size() < 2)
    throw usage(execid);

  database db(app);
  workspace work(app);

  utf8 dstr = args.back();
  file_path dst_path = file_path_external(dstr);

  set<file_path> src_paths;
  for (size_t i = 0; i < args.size()-1; i++)
    {
      file_path s = file_path_external(idx(args, i));
      src_paths.insert(s);
    }

  //this catches the case where the user specifies a directory 'by convention'
  //that doesn't exist.  the code in perform_rename already handles the proper
  //cases for more than one source item.
  if (src_paths.size() == 1 && dstr()[dstr().size() -1] == '/')
    if (get_path_status(*src_paths.begin()) != path::directory)
        N(get_path_status(dst_path) == path::directory,
          F(_("The specified target directory %s/ doesn't exist.")) % dst_path);

  work.perform_rename(db, src_paths, dst_path, app.opts.bookkeep_only);
}


CMD(pivot_root, "pivot_root", "", CMD_REF(workspace), N_("NEW_ROOT PUT_OLD"),
    N_("Renames the root directory"),
    N_("After this command, the directory that currently "
       "has the name NEW_ROOT "
       "will be the root directory, and the directory "
       "that is currently the root "
       "directory will have name PUT_OLD.\n"
       "Use of --bookkeep-only is NOT recommended."),
    options::opts::bookkeep_only)
{
  if (args.size() != 2)
    throw usage(execid);

  database db(app);
  workspace work(app);
  file_path new_root = file_path_external(idx(args, 0));
  file_path put_old = file_path_external(idx(args, 1));
  work.perform_pivot_root(db, new_root, put_old,
                              app.opts.bookkeep_only);
}

CMD(status, "status", "", CMD_REF(informative), N_("[PATH]..."),
    N_("Shows workspace's status information"),
    "",
    options::opts::depth | options::opts::exclude)
{
  roster_t new_roster;
  parent_map old_rosters;
  revision_t rev;
  temp_node_id_source nis;

  database db(app);
  workspace work(app);
  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_rosters, new_roster);

  work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_revision(old_rosters, new_roster, mask, rev);

  utf8 summary;
  revision_summary(rev, app.opts.branchname, summary);
  external summary_external;
  utf8_to_system_best_effort(summary, summary_external);
  cout << summary_external;
}

CMD(checkout, "checkout", "co", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Checks out a revision from the database into a directory"),
    N_("If a revision is given, that's the one that will be checked out.  "
       "Otherwise, it will be the head of the branch (given or implicit).  "
       "If no directory is given, the branch name will be used as directory."),
    options::opts::branch | options::opts::revision)
{
  revision_id revid;
  system_path dir;

  database db(app);
  project_t project(db);
  transaction_guard guard(db, false);

  if (args.size() > 1 || app.opts.revision_selectors.size() > 1)
    throw usage(execid);

  if (app.opts.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.opts.branchname().empty(),
        F("use --revision or --branch to specify what to checkout"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branchname, heads,
                               app.opts.ignore_suspend_certs);
      N(heads.size() > 0,
        F("branch '%s' is empty") % app.opts.branchname);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branchname);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(project, *i));
          P(F("choose one with '%s checkout -r<id>'") % ui.prog_name);
          E(false, F("branch %s has multiple heads") % app.opts.branchname);
        }
      revid = *(heads.begin());
    }
  else if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), revid);

      guess_branch(app.opts, project, revid);

      I(!app.opts.branchname().empty());

      N(project.revision_is_in_branch(revid, app.opts.branchname),
        F("revision %s is not a member of branch %s")
        % revid % app.opts.branchname);
    }

  // we do this part of the checking down here, because it is legitimate to
  // do
  //  $ mtn co -r h:net.venge.monotone
  // and have mtn guess the branch, and then use that branch name as the
  // default directory.  But in this case the branch name will not be set
  // until after the guess_branch() call above:
  {
    bool checkout_dot = false;

    if (args.size() == 0)
      {
        // No checkout dir specified, use branch name for dir.
        N(!app.opts.branchname().empty(),
          F("you must specify a destination directory"));
        dir = system_path(app.opts.branchname());
      }
    else
      {
        // Checkout to specified dir.
        dir = system_path(idx(args, 0));
        if (idx(args, 0) == utf8("."))
          checkout_dot = true;
      }

    if (!checkout_dot)
      require_path_is_nonexistent
        (dir, F("checkout directory '%s' already exists") % dir);
  }

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  roster_t empty_roster, current_roster;

  L(FL("checking out revision %s to directory %s") % revid % dir);
  db.get_roster(revid, current_roster);

  revision_t workrev;
  make_revision_for_workspace(revid, cset(), workrev);
  work.put_work_rev(workrev);

  cset checkout;
  make_cset(empty_roster, current_roster, checkout);

  content_merge_checkout_adaptor wca(db);

  work.perform_content_update(db, checkout, wca, false);

  work.update_any_attrs(db);
  work.maybe_update_inodeprints(db);
  guard.commit();
}

CMD_GROUP(attr, "attr", "", CMD_REF(workspace),
          N_("Manages file attributes"),
          N_("This command is used to set, get or drop file attributes."));

CMD(attr_drop, "drop", "", CMD_REF(attr), N_("PATH [ATTR]"),
    N_("Removes attributes from a file"),
    N_("If no attribute is specified, this command removes all attributes "
       "attached to the file given in PATH.  Otherwise only removes the "
       "attribute specified in ATTR."),
    options::opts::none)
{
  N(args.size() > 0 && args.size() < 3,
    F("wrong argument count"));

  roster_t new_roster;
  temp_node_id_source nis;

  database db(app);
  workspace work(app);
  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args, 0));

  N(new_roster.has_node(path), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(path);

  // Clear all attrs (or a specific attr).
  if (args.size() == 1)
    {
      for (full_attr_map_t::iterator i = node->attrs.begin();
           i != node->attrs.end(); ++i)
        i->second = make_pair(false, "");
    }
  else
    {
      I(args.size() == 2);
      attr_key a_key = attr_key(idx(args, 1)());
      N(node->attrs.find(a_key) != node->attrs.end(),
        F("Path '%s' does not have attribute '%s'")
        % path % a_key);
      node->attrs[a_key] = make_pair(false, "");
    }

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
  work.update_any_attrs(db);
}

CMD(attr_get, "get", "", CMD_REF(attr), N_("PATH [ATTR]"),
    N_("Gets the values of a file's attributes"),
    N_("If no attribute is specified, this command prints all attributes "
       "attached to the file given in PATH.  Otherwise it only prints the "
       "attribute specified in ATTR."),
    options::opts::none)
{
  N(args.size() > 0 && args.size() < 3,
    F("wrong argument count"));

  roster_t new_roster;
  temp_node_id_source nis;

  database db(app);
  workspace work(app);
  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args, 0));

  N(new_roster.has_node(path), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(path);

  if (args.size() == 1)
    {
      bool has_any_live_attrs = false;
      for (full_attr_map_t::const_iterator i = node->attrs.begin();
           i != node->attrs.end(); ++i)
        if (i->second.first)
          {
            cout << path << " : "
                 << i->first << '='
                 << i->second.second << '\n';
            has_any_live_attrs = true;
          }
      if (!has_any_live_attrs)
        cout << F("No attributes for '%s'") % path << '\n';
    }
  else
    {
      I(args.size() == 2);
      attr_key a_key = attr_key(idx(args, 1)());
      full_attr_map_t::const_iterator i = node->attrs.find(a_key);
      if (i != node->attrs.end() && i->second.first)
        cout << path << " : "
             << i->first << '='
             << i->second.second << '\n';
      else
        cout << (F("No attribute '%s' on path '%s'")
                 % a_key % path) << '\n';
    }
}

CMD(attr_set, "set", "", CMD_REF(attr), N_("PATH ATTR VALUE"),
    N_("Sets an attribute on a file"),
    N_("Sets the attribute given on ATTR to the value specified in VALUE "
       "for the file mentioned in PATH."),
    options::opts::none)
{
  N(args.size() == 3,
    F("wrong argument count"));

  roster_t new_roster;
  temp_node_id_source nis;

  database db(app);
  workspace work(app);
  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args, 0));

  N(new_roster.has_node(path), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(path);

  attr_key a_key = attr_key(idx(args, 1)());
  attr_value a_value = attr_value(idx(args, 2)());

  node->attrs[a_key] = make_pair(true, a_value);

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
  work.update_any_attrs(db);
}

// Name: get_attributes
// Arguments:
//   1: file / directory name
// Added in: 1.0
// Renamed from attributes to get_attributes in: 5.0
// Purpose: Prints all attributes for the specified path
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
// Error conditions: If the path has no attributes, prints only the
//                   format version, if the file is unknown, escalates
CMD_AUTOMATE(get_attributes, N_("PATH"),
             N_("Prints all attributes for the specified path"),
             "",
             options::opts::none)
{
  N(args.size() > 0,
    F("wrong argument count"));

  database db(app);
  workspace work(app);

  // retrieve the path
  file_path path = file_path_external(idx(args,0));

  roster_t base, current;
  parent_map parents;
  temp_node_id_source nis;

  // get the base and the current roster of this workspace
  work.get_current_roster_shape(db, nis, current);
  work.get_parent_rosters(db, parents);
  N(parents.size() == 1,
    F("this command can only be used in a single-parent workspace"));
  base = parent_roster(parents.begin());

  N(current.has_node(path), F("Unknown path '%s'") % path);

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
    std::string state;

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

            // the attribute is new if it either hasn't been found
            // in the previous roster or has been deleted there
            if (j == prev_node->attrs.end() || !j->second.first)
              {
                state = "added";
              }
            // check if the attribute's value has been changed
            else if (i->second.second() != j->second.second())
              {
                state = "changed";
              }
            else
              {
                state = "unchanged";
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

// Name: set_attribute
// Arguments:
//   1: file / directory name
//   2: attribute key
//   3: attribute value
// Added in: 5.0
// Purpose: Edits the workspace revision and sets an attribute on a certain path
//
// Error conditions: If PATH is unknown in the new roster, prints an error and
//                   exits with status 1.
CMD_AUTOMATE(set_attribute, N_("PATH KEY VALUE"),
             N_("Sets an attribute on a certain path"),
             "",
             options::opts::none)
{
  N(args.size() == 3,
    F("wrong argument count"));

  database db(app);
  workspace work(app);

  roster_t new_roster;
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args,0));

  N(new_roster.has_node(path), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(path);

  attr_key a_key = attr_key(idx(args,1)());
  attr_value a_value = attr_value(idx(args,2)());

  node->attrs[a_key] = make_pair(true, a_value);

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
  work.update_any_attrs(db);
}

// Name: drop_attribute
// Arguments:
//   1: file / directory name
//   2: attribute key (optional)
// Added in: 5.0
// Purpose: Edits the workspace revision and drops an attribute or all
//          attributes of the specified path
//
// Error conditions: If PATH is unknown in the new roster or the specified
//                   attribute key is unknown, prints an error and exits with
//                   status 1.
CMD_AUTOMATE(drop_attribute, N_("PATH [KEY]"),
             N_("Drops an attribute or all of them from a certain path"),
             "",
             options::opts::none)
{
  N(args.size() ==1 || args.size() == 2,
    F("wrong argument count"));

  database db(app);
  workspace work(app);

  roster_t new_roster;
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, new_roster);

  file_path path = file_path_external(idx(args,0));

  N(new_roster.has_node(path), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(path);

  // Clear all attrs (or a specific attr).
  if (args.size() == 1)
    {
      for (full_attr_map_t::iterator i = node->attrs.begin();
           i != node->attrs.end(); ++i)
        i->second = make_pair(false, "");
    }
  else
    {
      attr_key a_key = attr_key(idx(args,1)());
      N(node->attrs.find(a_key) != node->attrs.end(),
        F("Path '%s' does not have attribute '%s'")
        % path % a_key);
      node->attrs[a_key] = make_pair(false, "");
    }

  parent_map parents;
  work.get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  work.put_work_rev(new_work);
  work.update_any_attrs(db);
}

CMD(commit, "commit", "ci", CMD_REF(workspace), N_("[PATH]..."),
    N_("Commits workspace changes to the database"),
    "",
    options::opts::branch | options::opts::message | options::opts::msgfile
    | options::opts::date | options::opts::author | options::opts::depth
    | options::opts::exclude)
{
  database db(app);
  key_store keys(app);
  workspace work(app);
  project_t project(db);

  utf8 log_message("");
  bool log_message_given;
  revision_t restricted_rev;
  parent_map old_rosters;
  roster_t new_roster;
  temp_node_id_source nis;
  cset excluded;

  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_rosters, new_roster);

  work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_revision(old_rosters, new_roster, mask, restricted_rev,
                           excluded, join_words(execid));
  restricted_rev.check_sane();
  N(restricted_rev.is_nontrivial(), F("no changes to commit"));

  revision_id restricted_rev_id;
  calculate_ident(restricted_rev, restricted_rev_id);

  // We need the 'if' because guess_branch will try to override any branch
  // picked up from _MTN/options.
  if (app.opts.branchname().empty())
    {
      branch_name branchname, bn_candidate;
      for (edge_map::iterator i = restricted_rev.edges.begin();
           i != restricted_rev.edges.end();
           i++)
        {
          // this will prefer --branch if it was set
          guess_branch(app.opts, project, edge_old_revision(i),
                       bn_candidate);
          N(branchname() == "" || branchname == bn_candidate,
            F("parent revisions of this commit are in different branches:\n"
              "'%s' and '%s'.\n"
              "please specify a branch name for the commit, with --branch.")
            % branchname % bn_candidate);
          branchname = bn_candidate;
        }

      app.opts.branchname = branchname;
    }

  P(F("beginning commit on branch '%s'") % app.opts.branchname);

  if (global_sanity.debug_p())
    {
      hexenc<id> mid(encode_hexenc(restricted_rev.new_manifest.inner()()));
      hexenc<id> rid(encode_hexenc(restricted_rev_id.inner()()));

      L(FL("new manifest '%s'\n"
           "new revision '%s'\n") % mid % rid);
    }

  process_commit_message_args(app.opts, log_message_given, log_message);

  N(!(log_message_given && work.has_contents_user_log()),
    F("_MTN/log is non-empty and log message "
      "was specified on command line\n"
      "perhaps move or delete _MTN/log,\n"
      "or remove --message/--message-file from the command line?"));

  if (!log_message_given)
    {
      // This call handles _MTN/log.
      get_log_message_interactively(app.lua, work, restricted_rev,
                                    app.opts.branchname, log_message);

      // We only check for empty log messages when the user entered them
      // interactively.  Consensus was that if someone wanted to explicitly
      // type --message="", then there wasn't any reason to stop them.
      N(log_message().find_first_not_of("\n\r\t ") != string::npos,
        F("empty log message; commit canceled"));

      // We save interactively entered log messages to _MTN/log, so if
      // something goes wrong, the next commit will pop up their old
      // log message by default. We only do this for interactively
      // entered messages, because otherwise 'monotone commit -mfoo'
      // giving an error, means that after you correct that error and
      // hit up-arrow to try again, you get an "_MTN/log non-empty and
      // message given on command line" error... which is annoying.

      work.write_user_log(log_message);
    }

  // If the hook doesn't exist, allow the message to be used.
  bool message_validated;
  string reason, new_manifest_text;

  revision_data new_rev;
  write_revision(restricted_rev, new_rev);

  app.lua.hook_validate_commit_message(log_message, new_rev, app.opts.branchname,
                                       message_validated, reason);
  N(message_validated, F("log message rejected by hook: %s") % reason);

  cache_user_key(app.opts, app.lua, db, keys);

  // for the divergence check, below
  set<revision_id> heads;
  project.get_branch_heads(app.opts.branchname, heads,
                           app.opts.ignore_suspend_certs);
  unsigned int old_head_size = heads.size();

  {
    transaction_guard guard(db);

    if (db.revision_exists(restricted_rev_id))
      W(F("revision %s already in database") % restricted_rev_id);
    else
      {
        if (global_sanity.debug_p())
          L(FL("inserting new revision %s")
            % encode_hexenc(restricted_rev_id.inner()()));

        for (edge_map::const_iterator edge = restricted_rev.edges.begin();
             edge != restricted_rev.edges.end();
             edge++)
          {
            // process file deltas or new files
            cset const & cs = edge_changes(edge);

            for (map<file_path, pair<file_id, file_id> >::const_iterator
                   i = cs.deltas_applied.begin();
                 i != cs.deltas_applied.end(); ++i)
              {
                file_path path = i->first;

                file_id old_content = i->second.first;
                file_id new_content = i->second.second;

                if (db.file_version_exists(new_content))
                  {
                    if (global_sanity.debug_p())
                      L(FL("skipping file delta %s, already in database")
                        % encode_hexenc(delta_entry_dst(i).inner()()));
                  }
                else if (db.file_version_exists(old_content))
                  {
                    if (global_sanity.debug_p())
                      L(FL("inserting delta %s -> %s")
                        % encode_hexenc(old_content.inner()())
                        % encode_hexenc(new_content.inner()()));

                    file_data old_data;
                    data new_data;
                    db.get_file_version(old_content, old_data);
                    read_data(path, new_data);
                    // sanity check
                    file_id tid;
                    calculate_ident(file_data(new_data), tid);
                    N(tid == new_content,
                      F("file '%s' modified during commit, aborting")
                      % path);
                    delta del;
                    diff(old_data.inner(), new_data, del);
                    db.put_file_version(old_content,
                                            new_content,
                                            file_delta(del));
                  }
                else
                  // If we don't err out here, the database will later.
                  E(false,
                    F("Your database is missing version %s of file '%s'")
                    % encode_hexenc(old_content.inner()())
                    % path);
              }

            for (map<file_path, file_id>::const_iterator
                   i = cs.files_added.begin();
                 i != cs.files_added.end(); ++i)
              {
                file_path path = i->first;
                file_id new_content = i->second;

                if (global_sanity.debug_p())
                  L(FL("inserting full version %s")
                    % encode_hexenc(new_content.inner()()));
                data new_data;
                read_data(path, new_data);
                // sanity check
                file_id tid;
                calculate_ident(file_data(new_data), tid);
                N(tid == new_content,
                  F("file '%s' modified during commit, aborting")
                  % path);
                db.put_file(new_content, file_data(new_data));
              }
          }

        revision_data rdat;
        write_revision(restricted_rev, rdat);
        db.put_revision(restricted_rev_id, rdat);
      }

    project.put_standard_certs_from_options(app.opts, app.lua, keys,
                                            restricted_rev_id,
                                            app.opts.branchname,
                                            log_message);
    guard.commit();
  }

  // the workspace should remember the branch we just committed to.
  work.set_ws_options(app.opts, true);

  // the work revision is now whatever changes remain on top of the revision
  // we just checked in.
  revision_t remaining;
  make_revision_for_workspace(restricted_rev_id, excluded, remaining);

  // small race condition here...
  work.put_work_rev(remaining);
  P(F("committed revision %s")
    % encode_hexenc(restricted_rev_id.inner()()));

  work.blank_user_log();

  project.get_branch_heads(app.opts.branchname, heads,
                           app.opts.ignore_suspend_certs);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run '%s merge'")
      % ui.prog_name);
  }

  work.update_any_attrs(db);
  work.maybe_update_inodeprints(db);

  {
    // Tell lua what happened. Yes, we might lose some information
    // here, but it's just an indicator for lua, eg. to post stuff to
    // a mailing list. If the user *really* cares about cert validity,
    // multiple certs with same name, etc. they can inquire further,
    // later.
    map<cert_name, cert_value> certs;
    vector< revision<cert> > ctmp;
    project.get_revision_certs(restricted_rev_id, ctmp);
    for (vector< revision<cert> >::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      certs.insert(make_pair(i->inner().name, i->inner().value));

    revision_data rdat;
    db.get_revision(restricted_rev_id, rdat);
    app.lua.hook_note_commit(restricted_rev_id, rdat, certs);
  }
}

CMD_NO_WORKSPACE(setup, "setup", "", CMD_REF(tree), N_("[DIRECTORY]"),
    N_("Sets up a new workspace directory"),
    N_("If no directory is specified, uses the current directory."),
    options::opts::branch)
{
  if (args.size() > 1)
    throw usage(execid);
  N(!app.opts.branchname().empty(),
    F("need --branch argument for setup"));

  database db(app);
  db.ensure_open();

  string dir;
  if (args.size() == 1)
    dir = idx(args,0)();
  else
    dir = ".";

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  revision_t rev;
  make_revision_for_workspace(revision_id(), cset(), rev);
  work.put_work_rev(rev);
}

CMD_NO_WORKSPACE(import, "import", "", CMD_REF(tree), N_("DIRECTORY"),
  N_("Imports the contents of a directory into a branch"),
  "",
  options::opts::branch | options::opts::revision |
  options::opts::message | options::opts::msgfile |
  options::opts::dryrun |
  options::opts::no_ignore | options::opts::exclude |
  options::opts::author | options::opts::date)
{
  revision_id ident;
  system_path dir;
  database db(app);
  project_t project(db);

  N(args.size() == 1,
    F("you must specify a directory to import"));

  if (app.opts.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision_selectors, 0)(), ident);

      guess_branch(app.opts, project, ident);

      I(!app.opts.branchname().empty());

      N(project.revision_is_in_branch(ident, app.opts.branchname),
        F("revision %s is not a member of branch %s")
        % ident % app.opts.branchname);
    }
  else
    {
      // use branch head revision
      N(!app.opts.branchname().empty(),
        F("use --revision or --branch to specify the parent revision for the import"));

      set<revision_id> heads;
      project.get_branch_heads(app.opts.branchname, heads,
                               app.opts.ignore_suspend_certs);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branchname);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(project, *i));
          P(F("choose one with '%s import -r<id>'") % ui.prog_name);
          E(false, F("branch %s has multiple heads") % app.opts.branchname);
        }
      if (heads.size() > 0)
        ident = *(heads.begin());
    }

  dir = system_path(idx(args, 0));
  require_path_is_directory
    (dir,
     F("import directory '%s' doesn't exists") % dir,
     F("import directory '%s' is a file") % dir);

  workspace::create_workspace(app.opts, app.lua, dir);
  workspace work(app);

  try
    {
      revision_t rev;
      make_revision_for_workspace(ident, cset(), rev);
      work.put_work_rev(rev);

      // prepare stuff for 'add' and so on.
      args_vector empty_args;
      options save_opts;
      // add --unknown
      save_opts.exclude_patterns = app.opts.exclude_patterns;
      app.opts.exclude_patterns = args_vector();
      app.opts.unknown = true;
      app.opts.recursive = true;
      process(app, make_command_id("workspace add"), empty_args);
      app.opts.recursive = false;
      app.opts.unknown = false;
      app.opts.exclude_patterns = save_opts.exclude_patterns;

      // drop --missing
      save_opts.no_ignore = app.opts.no_ignore;
      app.opts.missing = true;
      process(app, make_command_id("workspace drop"), empty_args);
      app.opts.missing = false;
      app.opts.no_ignore = save_opts.no_ignore;

      // commit
      if (!app.opts.dryrun)
        process(app, make_command_id("workspace commit"), empty_args);
    }
  catch (...)
    {
      // clean up before rethrowing
      delete_dir_recursive(bookkeeping_root);
      throw;
    }

  // clean up
  delete_dir_recursive(bookkeeping_root);
}

CMD_NO_WORKSPACE(migrate_workspace, "migrate_workspace", "", CMD_REF(tree),
  N_("[DIRECTORY]"),
  N_("Migrates a workspace directory's metadata to the latest format"),
  N_("If no directory is given, defaults to the current workspace."),
  options::opts::none)
{
  if (args.size() > 1)
    throw usage(execid);

  if (args.size() == 1)
    {
      go_to_workspace(system_path(idx(args, 0)));
      workspace::found = true;
    }

  workspace work(app, false);
  work.migrate_ws_format();
}

CMD(refresh_inodeprints, "refresh_inodeprints", "", CMD_REF(tree), "",
    N_("Refreshes the inodeprint cache"),
    "",
    options::opts::none)
{
  database db(app);
  workspace work(app);
  work.enable_inodeprints();
  work.maybe_update_inodeprints(db);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
