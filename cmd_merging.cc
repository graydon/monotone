// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>

#include "cmd.hh"
#include "diff_patch.hh"
#include "merge.hh"
#include "packet.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "roster_merge.hh"
#include "transforms.hh"
#include "update.hh"
#include "work.hh"

using std::cout;
using std::map;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

struct update_source
  : public file_content_source
{
  map<file_id, file_data> & temporary_store;
  app_state & app;
  update_source (map<file_id, file_data> & tmp,
                 app_state & app)
    : temporary_store(tmp), app(app)
  {}
  void get_file_content(file_id const & fid,
                        file_data & dat) const
  {
    map<file_id, file_data>::const_iterator 
      i = temporary_store.find(fid);

    if (i != temporary_store.end())
      dat = i->second;
    else
      app.db.get_file_version(fid, dat);
  }
};

CMD(update, N_("workspace"), "",
    N_("update workspace.\n"
       "This command modifies your workspace to be based off of a\n"
       "different revision, preserving uncommitted changes as it does so.\n"
       "If a revision is given, update the workspace to that revision.\n"
       "If not, update the workspace to the head of the branch."),
    OPT_BRANCH_NAME % OPT_REVISION)
{
  revision_set r_working;
  roster_t working_roster, chosen_roster, target_roster;
  shared_ptr<roster_t> old_roster = shared_ptr<roster_t>(new roster_t());
  marking_map working_mm, chosen_mm, merged_mm, target_mm;
  revision_id r_old_id, r_working_id, r_chosen_id, r_target_id;
  temp_node_id_source nis;

  if (args.size() > 0)
    throw usage(name);

  if (app.revision_selectors.size() > 1)
    throw usage(name);

  app.require_workspace();

  // FIXME: the next few lines are a little bit expensive insofar as they
  // load the base roster twice. The API could use some factoring or
  // such. But it should work for now; revisit if performance is
  // intolerable.

  get_base_and_current_roster_shape(*old_roster, 
                                    working_roster, nis, app);
  update_current_roster_from_filesystem(working_roster, app);

  get_revision_id(r_old_id);
  make_revision_set(r_old_id, *old_roster, working_roster, r_working);

  calculate_ident(r_working, r_working_id);
  I(r_working.edges.size() == 1);
  r_old_id = edge_old_revision(r_working.edges.begin());
  make_roster_for_base_plus_cset(r_old_id,
                                 edge_changes(r_working.edges.begin()),
                                 r_working_id,
                                 working_roster, working_mm, nis, app);

  N(!null_id(r_old_id),
    F("this workspace is a new project; cannot update"));

  if (app.revision_selectors.size() == 0)
    {
      P(F("updating along branch '%s'") % app.branch_name);
      set<revision_id> candidates;
      pick_update_candidates(r_old_id, app, candidates);
      N(!candidates.empty(),
        F("your request matches no descendents of the current revision\n"
          "in fact, it doesn't even match the current revision\n"
          "maybe you want --revision=<rev on other branch>"));
      if (candidates.size() != 1)
        {
          P(F("multiple update candidates:"));
          for (set<revision_id>::const_iterator i = candidates.begin();
               i != candidates.end(); ++i)
            P(i18n_format("  %s") % describe_revision(app, *i));
          P(F("choose one with '%s update -r<id>'") % app.prog_name);
          E(false, F("multiple update candidates remain after selection"));
        }
      r_chosen_id = *(candidates.begin());
    }
  else
    {
      complete(app, app.revision_selectors[0](), r_chosen_id);
      N(app.db.revision_exists(r_chosen_id),
        F("no such revision '%s'") % r_chosen_id);
    }

  notify_if_multiple_heads(app);

  if (r_old_id == r_chosen_id)
    {
      P(F("already up to date at %s") % r_old_id);
      // do still switch the workspace branch, in case they have used
      // update to switch branches.
      if (!app.branch_name().empty())
        app.make_branch_sticky();
      return;
    }

  P(F("selected update target %s") % r_chosen_id);

  bool switched_branch = false;
  {
    // figure out which branches the target is in
    vector< revision<cert> > certs;
    app.db.get_revision_certs(r_chosen_id, branch_cert_name, certs);
    erase_bogus_certs(certs, app);

    set< utf8 > branches;
    for (vector< revision<cert> >::const_iterator i = certs.begin();
         i != certs.end(); i++)
      {
        cert_value b;
        decode_base64(i->inner().value, b);
        branches.insert(utf8(b()));
      }

    if (branches.find(app.branch_name) != branches.end())
      {
        L(FL("using existing branch %s") % app.branch_name());
      }
    else
      {
        P(F("target revision is not in current branch"));
        if (branches.size() > 1)
          {
            // multiple non-matching branchnames
            string branch_list;
            for (set<utf8>::const_iterator i = branches.begin();
                 i != branches.end(); i++)
              branch_list += "\n  " + (*i)();
            N(false, F("target revision is in multiple branches:%s\n\n"
                       "try again with explicit --branch") % branch_list);
          }
        else if (branches.size() == 1)
          {
            // one non-matching, inform and update
            app.branch_name = (*(branches.begin()))();
            switched_branch = true;
            P(F("switching to branch %s") % app.branch_name());
          }
        else
          {
            I(branches.size() == 0);
            W(F("target revision not in any branch\n"
                "next commit will use branch %s")
              % app.branch_name());
          }
      }
  }

  app.db.get_roster(r_chosen_id, chosen_roster, chosen_mm);

  set<revision_id>
    working_uncommon_ancestors,
    chosen_uncommon_ancestors;

  if (is_ancestor(r_old_id, r_chosen_id, app))
    {
      target_roster = chosen_roster;
      target_mm = chosen_mm;
      r_target_id = r_chosen_id;
      app.db.get_uncommon_ancestors(r_old_id, r_chosen_id,
                                    working_uncommon_ancestors,
                                    chosen_uncommon_ancestors);
    }
  else
    {
      cset transplant;
      make_cset (*old_roster, chosen_roster, transplant);

      // Just pick some unused revid, all that's important is that it not
      // match the work revision or any ancestors of the base revision.
      r_target_id = revision_id(hexenc<id>("5432100000000000000000000500000000000000"));
      make_roster_for_base_plus_cset(r_old_id,
                                     transplant,
                                     r_target_id,
                                     target_roster, target_mm, nis, app);
      chosen_uncommon_ancestors.insert(r_target_id);
    }

  // Note that under the definition of mark-merge, the workspace is an
  // "uncommon ancestor" of itself too, even though it was not present in
  // the database (hence not returned by the query above).

  working_uncommon_ancestors.insert(r_working_id);

  // Now merge the working roster with the chosen target.

  roster_merge_result result;
  roster_merge(working_roster, working_mm, working_uncommon_ancestors,
               target_roster, target_mm, chosen_uncommon_ancestors,
               result);

  roster_t & merged_roster = result.roster;

  content_merge_workspace_adaptor wca(app, old_roster);
  resolve_merge_conflicts (r_old_id, r_target_id,
                           working_roster, target_roster,
                           working_mm, target_mm,
                           result, wca, app);

  I(result.is_clean());

  // Temporary node ids may appear if updating to a non-ancestor.
  merged_roster.check_sane(true);

  // We have the following
  //
  // old --> working
  //   |         |
  //   V         V
  //  chosen --> merged
  //
  // - old is the revision specified in _MTN/revision
  // - working is based on old and includes the workspace's changes
  // - chosen is the revision we're updating to and will end up in _MTN/revision
  // - merged is the merge of working and chosen
  //
  // we apply the working to merged cset to the workspace
  // and write the cset from chosen to merged changeset in _MTN/work

  cset update, remaining;
  make_cset(working_roster, merged_roster, update);
  make_cset(target_roster, merged_roster, remaining);

  //   {
  //     data t1, t2, t3;
  //     write_cset(update, t1);
  //     write_cset(remaining, t2);
  //     write_manifest_of_roster(merged_roster, t3);
  //     P(F("updating workspace with [[[\n%s\n]]]") % t1);
  //     P(F("leaving residual work [[[\n%s\n]]]") % t2);
  //     P(F("merged roster [[[\n%s\n]]]") % t3);
  //   }

  update_source fsource(wca.temporary_store, app);
  editable_working_tree ewt(app, fsource);
  update.apply_to(ewt);

  // small race condition here...
  // nb: we write out r_chosen, not r_new, because the revision-on-disk
  // is the basis of the workspace, not the workspace itself.
  put_revision_id(r_chosen_id);
  if (!app.branch_name().empty())
    {
      app.make_branch_sticky();
    }
  if (switched_branch)
    P(F("switched branch; next commit will use branch %s") % app.branch_name());
  P(F("updated to base revision %s") % r_chosen_id);

  put_work_cset(remaining);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}


// should merge support --message, --message-file?  It seems somewhat weird,
// since a single 'merge' command may perform arbitrarily many actual merges.
CMD(merge, N_("tree"), "", N_("merge unmerged heads of branch"),
    OPT_BRANCH_NAME % OPT_DATE % OPT_AUTHOR)
{
  set<revision_id> heads;

  if (args.size() != 0)
    throw usage(name);

  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  N(heads.size() != 0, F("branch '%s' is empty") % app.branch_name);
  if (heads.size() == 1)
    {
      P(F("branch '%s' is already merged") % app.branch_name);
      return;
    }

  set<revision_id>::const_iterator i = heads.begin();
  revision_id left = *i;
  revision_id ancestor;
  size_t count = 1;
  P(F("starting with revision 1 / %d") % heads.size());
  for (++i; i != heads.end(); ++i, ++count)
    {
      revision_id right = *i;
      P(F("merging with revision %d / %d") % (count + 1) % heads.size());
      P(F("[source] %s") % left);
      P(F("[source] %s") % right);

      revision_id merged;
      transaction_guard guard(app.db);
      interactive_merge_and_store(left, right, merged, app);

      // merged 1 edge; now we commit this, update merge source and
      // try next one

      packet_db_writer dbw(app);
      cert_revision_in_branch(merged, app.branch_name(), app, dbw);

      string log = (FL("merge of %s\n"
                       "     and %s\n") % left % right).str();
      cert_revision_changelog(merged, log, app, dbw);

      guard.commit();
      P(F("[merged] %s") % merged);
      left = merged;
    }
  P(F("note: your workspaces have not been updated"));
}

CMD(propagate, N_("tree"), N_("SOURCE-BRANCH DEST-BRANCH"),
    N_("merge from one branch to another asymmetrically"),
    OPT_DATE % OPT_AUTHOR % OPT_MESSAGE % OPT_MSGFILE)
{
  if (args.size() != 2)
    throw usage(name);
  vector<utf8> a = args;
  a.push_back(utf8());
  process(app, "merge_into_dir", a);
}

CMD(merge_into_dir, N_("tree"), N_("SOURCE-BRANCH DEST-BRANCH DIR"),
    N_("merge one branch into a subdirectory in another branch"),
    OPT_DATE % OPT_AUTHOR % OPT_MESSAGE % OPT_MSGFILE)
{
  //   This is a special merge operator, but very useful for people
  //   maintaining "slightly disparate but related" trees. It does a one-way
  //   merge; less powerful than putting things in the same branch and also
  //   more flexible.
  //
  //   1. Check to see if src and dst branches are merged, if not abort, if so
  //   call heads N1 and N2 respectively.
  //
  //   2. (FIXME: not yet present) Run the hook propagate ("src-branch",
  //   "dst-branch", N1, N2) which gives the user a chance to massage N1 into
  //   a state which is likely to "merge nicely" with N2, eg. edit pathnames,
  //   omit optional files of no interest.
  //
  //   3. Do a normal 2 or 3-way merge on N1 and N2, depending on the
  //   existence of common ancestors.
  //
  //   4. Save the results as the delta (N2,M), the ancestry edges (N1,M)
  //   and (N2,M), and the cert (N2,dst).
  //
  //   There are also special cases we have to check for where no merge is
  //   actually necessary, because there hasn't been any divergence since the
  //   last time propagate was run.
  //
  //   If dir is not the empty string, rename the root of N1 to have the name
  //   'dir' in the merged tree. (ie, it has name "basename(dir)", and its
  //   parent node is "N2.get_node(dirname(dir))")

  set<revision_id> src_heads, dst_heads;

  if (args.size() != 3)
    throw usage(name);

  get_branch_heads(idx(args, 0)(), app, src_heads);
  get_branch_heads(idx(args, 1)(), app, dst_heads);

  N(src_heads.size() != 0, F("branch '%s' is empty") % idx(args, 0)());
  N(src_heads.size() == 1, F("branch '%s' is not merged") % idx(args, 0)());

  N(dst_heads.size() != 0, F("branch '%s' is empty") % idx(args, 1)());
  N(dst_heads.size() == 1, F("branch '%s' is not merged") % idx(args, 1)());

  set<revision_id>::const_iterator src_i = src_heads.begin();
  set<revision_id>::const_iterator dst_i = dst_heads.begin();

  P(F("propagating %s -> %s") % idx(args,0) % idx(args,1));
  P(F("[source] %s") % *src_i);
  P(F("[target] %s") % *dst_i);

  // check for special cases
  if (*src_i == *dst_i || is_ancestor(*src_i, *dst_i, app))
    {
      P(F("branch '%s' is up-to-date with respect to branch '%s'")
          % idx(args, 1)() % idx(args, 0)());
      P(F("no action taken"));
    }
  else if (is_ancestor(*dst_i, *src_i, app))
    {
      P(F("no merge necessary; putting %s in branch '%s'")
        % (*src_i) % idx(args, 1)());
      transaction_guard guard(app.db);
      packet_db_writer dbw(app);
      cert_revision_in_branch(*src_i, idx(args, 1)(), app, dbw);
      guard.commit();
    }
  else
    {
      revision_id merged;
      transaction_guard guard(app.db);

      {
        revision_id const & left_rid(*src_i), & right_rid(*dst_i);
        roster_t left_roster, right_roster;
        MM(left_roster);
        MM(right_roster);
        marking_map left_marking_map, right_marking_map;
        set<revision_id> 
          left_uncommon_ancestors, 
          right_uncommon_ancestors;

        app.db.get_roster(left_rid, left_roster, left_marking_map);
        app.db.get_roster(right_rid, right_roster, right_marking_map);
        app.db.get_uncommon_ancestors(left_rid, right_rid,
                                      left_uncommon_ancestors,
                                      right_uncommon_ancestors);

        {
          dir_t moved_root = left_roster.root();
          split_path sp, dirname;
          path_component basename;
          MM(dirname);
          if (!idx(args,2)().empty())
            {
              file_path_external(idx(args,2)).split(sp);
              dirname_basename(sp, dirname, basename);
              N(right_roster.has_node(dirname),
                F("Path %s not found in destination tree.") % sp);
              node_t parent = right_roster.get_node(dirname);
              moved_root->parent = parent->self;
              moved_root->name = basename;
              marking_map::iterator 
                i = left_marking_map.find(moved_root->self);
              I(i != left_marking_map.end());
              i->second.parent_name.clear();
              i->second.parent_name.insert(left_rid);
            }
        }

        roster_merge_result result;
        roster_merge(left_roster, 
                     left_marking_map, 
                     left_uncommon_ancestors,
                     right_roster, 
                     right_marking_map, 
                     right_uncommon_ancestors,
                     result);

        content_merge_database_adaptor 
          dba(app, left_rid, right_rid, left_marking_map);

        resolve_merge_conflicts (left_rid, right_rid,
                                 left_roster, right_roster,
                                 left_marking_map, right_marking_map,
                                 result, dba, app);

        {
          dir_t moved_root = left_roster.root();
          moved_root->parent = the_null_node;
          moved_root->name = the_null_component;
        }

        // Write new files into the db.
        store_roster_merge_result(left_roster, right_roster, result,
                                  left_rid, right_rid, merged,
                                  app);
      }

      packet_db_writer dbw(app);

      cert_revision_in_branch(merged, idx(args, 1)(), app, dbw);

      bool log_message_given;
      string log_message;
      process_commit_message_args(log_message_given, log_message, app);
      if (!log_message_given)
        log_message = (FL("propagate from branch '%s' (head %s)\n"
                          "            to branch '%s' (head %s)\n")
                       % idx(args, 0) % (*src_i)
                       % idx(args, 1) % (*dst_i)).str();

      cert_revision_changelog(merged, log_message, app, dbw);

      guard.commit();
      P(F("[merged] %s") % merged);
    }
}

CMD(explicit_merge, N_("tree"),
    N_("LEFT-REVISION RIGHT-REVISION DEST-BRANCH"),
    N_("merge two explicitly given revisions, "
       "placing result in given branch"),
    OPT_DATE % OPT_AUTHOR)
{
  revision_id left, right;
  string branch;

  if (args.size() != 3)
    throw usage(name);

  complete(app, idx(args, 0)(), left);
  complete(app, idx(args, 1)(), right);
  branch = idx(args, 2)();

  N(!(left == right),
    F("%s and %s are the same revision, aborting") % left % right);
  N(!is_ancestor(left, right, app),
    F("%s is already an ancestor of %s") % left % right);
  N(!is_ancestor(right, left, app),
    F("%s is already an ancestor of %s") % right % left);

  // Somewhat redundant, but consistent with output of plain "merge" command.
  P(F("[source] %s") % left);
  P(F("[source] %s") % right);

  revision_id merged;
  transaction_guard guard(app.db);
  interactive_merge_and_store(left, right, merged, app);

  packet_db_writer dbw(app);

  cert_revision_in_branch(merged, branch, app, dbw);

  string log = (FL("explicit_merge of '%s'\n"
                   "              and '%s'\n"
                   "        to branch '%s'\n")
                % left % right % branch).str();

  cert_revision_changelog(merged, log, app, dbw);

  guard.commit();
  P(F("[merged] %s") % merged);
}

CMD(show_conflicts, N_("informative"), N_("REV REV"), 
    N_("Show what conflicts would need to be resolved "
       "to merge the given revisions."),
    OPT_BRANCH_NAME % OPT_DATE % OPT_AUTHOR)
{
  if (args.size() != 2)
    throw usage(name);
  revision_id l_id, r_id;
  complete(app, idx(args,0)(), l_id);
  complete(app, idx(args,1)(), r_id);
  N(!is_ancestor(l_id, r_id, app),
    F("%s is an ancestor of %s; no merge is needed.") % l_id % r_id);
  N(!is_ancestor(r_id, l_id, app),
    F("%s is an ancestor of %s; no merge is needed.") % r_id % l_id);
  roster_t l_roster, r_roster;
  marking_map l_marking, r_marking;
  app.db.get_roster(l_id, l_roster, l_marking);
  app.db.get_roster(r_id, r_roster, r_marking);
  set<revision_id> l_uncommon_ancestors, r_uncommon_ancestors;
  app.db.get_uncommon_ancestors(l_id, r_id,
                                l_uncommon_ancestors,
                                r_uncommon_ancestors);
  roster_merge_result result;
  roster_merge(l_roster, l_marking, l_uncommon_ancestors,
               r_roster, r_marking, r_uncommon_ancestors,
               result);

  P(F("There are %s node_name_conflicts.") 
    % result.node_name_conflicts.size());
  P(F("There are %s file_content_conflicts.") 
    % result.file_content_conflicts.size());
  P(F("There are %s node_attr_conflicts.") 
    % result.node_attr_conflicts.size());
  P(F("There are %s orphaned_node_conflicts.") 
    % result.orphaned_node_conflicts.size());
  P(F("There are %s rename_target_conflicts.") 
    % result.rename_target_conflicts.size());
  P(F("There are %s directory_loop_conflicts.") 
    % result.directory_loop_conflicts.size());
}

CMD(cherrypatch, N_("workspace"), "[-r FROM] -r TO",
    N_("cherrypick changes from other parts of history into workspace.\n"
       "This command takes arbitrary changes made at any point in history,\n"
       "and applies them to your current workspace, as if you had made the\n"
       "edits by hand.\n"
       "If one revision is given, applies the changes made in that revision\n"
       "compared to its parent.\n"
       "If two revisions are given, applies the changes made to get from the\n"
       "first revision to the second.")
    % OPT_REVISION)
{
  if (args.size() > 0)
    throw usage(name);
  
  revision_id from_rid, to_rid;
  
  if (app.revision_selectors.size() == 1)
    {
      to_rid = idx(app.revision_selectors, 0);
      std::set<revision_id> parents;
      app.db.get_revision_parents(to_rid, parents);
      N(parents.size() == 1,
        F("revision %s is a merge\n"
          "to apply the changes relative to one of its parents, use:\n"
          "  %s cherrypatch -r PARENT -r %s")
        % to_rid
        % app.prog_name % to_rid);
      from_rid = *parents.begin();
    }
  else if (app.revision_selectors.size() == 2)
    {
      from_rid = idx(app.revision_selectors, 0);
      to_rid = idx(app.revision_selectors, 1);
    }
  else
    throw usage(name);
  
  app.require_workspace();

  N(!(from_rid == to_rid), F("no changes to apply"));

  // notionally, we have the situation
  //
  // from --> working
  //   |         | 
  //   V         V
  //   to --> merged
  //
  // - from is the revision we start cherrypatching from
  // - to is the revision we stop cherrypatching at
  // - working is the current contents of the workspace
  // - merged is the result of the cherrypatching, and achieved by running a
  //   merge in the fictional graph seen above
  //
  // finally, we take the cset from working -> merged, and apply that to the
  //   workspace
  // and take the cset from the workspace's base, and write that to _MTN/work

  //   revision_set r_old, r_working, r_new;
//   roster_t working_roster, chosen_roster, target_roster;
//   boost::shared_ptr<roster_t> old_roster = boost::shared_ptr<roster_t>(new roster_t());
//   marking_map working_mm, chosen_mm, merged_mm, target_mm;
//   revision_id r_old_id, r_working_id, r_chosen_id, r_target_id;

  // FIXME: the next few lines are a little bit expensive insofar as they
  // load the base roster twice. The API could use some factoring or
  // such. But it should work for now; revisit if performance is
  // intolerable.

  // Get the FROM roster and markings
  roster_t from_roster;
  marking_map from_markings;
  app.db.get_roster(from_rid, from_roster, from_markings);

  // Get the TO roster
  roster_t to_true_roster;
  app.db.get_roster(to_rid, to_true_roster);
  
  // Get the working roster and rid (and base, while we're at it)
  revision_id working_rid, base_rid;
  roster_t working_true_roster, base_roster;
  get_unrestricted_working_revision_and_rosters(app, working_rev,
                                                *base_roster, 
                                                working_true_roster);
  // Find the rid
  calculate_ident(working_rev, working_rid);
  I(working_rev.edges.size() == 1);

  // Get the csets
  cset from_to_working, from_to_to;
  make_cset(from_roster, working_true_roster, from_to_working);
  make_cset(from_roster, to_true_roster, from_to_to);

  // Build the rosters with renumbered nids and fake markings
  roster_t to_fake_roster, working_fake_roster;
  make_roster_for_base_plus_cset(from_rid, from_to_working,
                                 r_working_id,
                                 working_roster, working_mm, app);

  app.db.get_roster(from_rid, from_roster, from_markings);
  app.db.get_roster(to_rid, to_roster);

  std::set<revision_id> working_uncommon_ancestors, to_uncommon_ancestors;
  safe_insert(working_uncommon_ancestors, r_working_id);
  safe_insert(to_uncommon_ancestors, to_rid);

  cset from_to_cs;
  make_cset (*from_roster, to_roster, from_to_cs);
  make_roster_for_base_plus_cset(from_rid, from_to_cs, to_rid, to_roster, to_mm, app);

  // Now merge the working roster with the chosen target. 

  roster_merge_result result;  
  roster_merge(working_roster, working_mm, working_uncommon_ancestors,
               to_roster, to_mm, to_uncommon_ancestors,
               result);

  roster_t & merged_roster = result.roster;

  content_merge_workspace_adaptor wca(app, old_roster);
  resolve_merge_conflicts (r_old_id, r_target_id,
                           working_roster, target_roster,
                           working_mm, target_mm,
                           result, wca, app);

  I(result.is_clean());
  // temporary node ids may appear if updating to a non-ancestor
  merged_roster.check_sane(true);

  // we have the following
  //
  // old --> working
  //   |         | 
  //   V         V
  //  chosen --> merged
  //
  // - old is the revision specified in _MTN/revision
  // - working is based on old and includes the workspace's changes
  // - chosen is the revision we're updating to and will end up in _MTN/revision
  // - merged is the merge of working and chosen
  // 
  // we apply the working to merged cset to the workspace 
  // and write the cset from chosen to merged changeset in _MTN/work
  
  cset update, remaining;
  make_cset (working_roster, merged_roster, update);
  make_cset (target_roster, merged_roster, remaining);

  //   {
  //     data t1, t2, t3;
  //     write_cset(update, t1);
  //     write_cset(remaining, t2);
  //     write_manifest_of_roster(merged_roster, t3);
  //     P(F("updating workspace with [[[\n%s\n]]]\n") % t1);
  //     P(F("leaving residual work [[[\n%s\n]]]\n") % t2);
  //     P(F("merged roster [[[\n%s\n]]]\n") % t3);
  //   }

  update_source fsource(wca.temporary_store, app);
  editable_working_tree ewt(app, fsource);
  update.apply_to(ewt);
  
  // small race condition here...
  // nb: we write out r_chosen, not r_new, because the revision-on-disk
  // is the basis of the workspace, not the workspace itself.
  put_revision_id(r_chosen_id);
  if (!app.branch_name().empty())
    {
      app.make_branch_sticky();
    }
  P(F("updated to base revision %s\n") % r_chosen_id);

  put_work_cset(remaining);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}

CMD(heads, N_("tree"), "", N_("show unmerged head revisions of branch"),
    OPT_BRANCH_NAME)
{
  set<revision_id> heads;
  if (args.size() != 0)
    throw usage(name);

  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  if (heads.size() == 0)
    P(F("branch '%s' is empty") % app.branch_name);
  else if (heads.size() == 1)
    P(F("branch '%s' is currently merged:") % app.branch_name);
  else
    P(F("branch '%s' is currently unmerged:") % app.branch_name);

  for (set<revision_id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    cout << describe_revision(app, *i) << "\n";
}

CMD(get_roster, N_("debug"), N_("REVID"),
    N_("dump the roster associated with the given REVID"),
    OPT_NONE)
{
  revision_id rid;
  if (args.size() == 0)
    get_revision_id(rid);
  else if (args.size() == 1)
    complete(app, idx(args, 0)(), rid);
  else
    throw usage(name);

  I(!null_id(rid));

  roster_t roster;
  marking_map mm;
  app.db.get_roster(rid, roster, mm);

  roster_data dat;
  write_roster_and_marking(roster, mm, dat);
  cout << dat;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
