// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <map>

#include "cmd.hh"
#include "diff_patch.hh"
#include "localized_file_io.hh"
#include "packet.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "transforms.hh"
#include "work.hh"

using std::cout;
using std::make_pair;
using std::pair;
using std::map;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

static void
get_log_message_interactively(revision_t const & cs,
                              app_state & app,
                              string & log_message)
{
  string commentary;
  data summary, user_log_message;
  write_revision(cs, summary);
  read_user_log(user_log_message);
  commentary += string(70, '-') + "\n";
  commentary += _("Enter a description of this change.\n"
                  "Lines beginning with `MTN:' "
                  "are removed automatically.\n");
  commentary += "\n";
  commentary += summary();
  commentary += string(70, '-') + "\n";

  N(app.lua.hook_edit_comment(commentary, user_log_message(), log_message),
    F("edit of log message failed"));
}

CMD(revert, N_("workspace"), N_("[PATH]..."),
    N_("revert file(s), dir(s) or entire workspace (\".\")"),
    option::depth % option::exclude % option::missing)
{
  temp_node_id_source nis;
  roster_t old_roster, new_roster;
  cset included, excluded;

  N(app.missing || !args.empty() || !app.exclude_patterns.empty(),
    F("you must pass at least one path to 'revert' (perhaps '.')"));

  app.require_workspace();

  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);

  node_restriction mask(args_to_paths(args), args_to_paths(app.exclude_patterns),
                        old_roster, new_roster, app);

  if (app.missing)
    {
      // --missing is a further filter on the files included by a
      // restriction we first find all missing files included by the
      // specified args and then make a restriction that includes only
      // these missing files.
      path_set missing;
      find_missing(new_roster, mask, missing);
      if (missing.empty())
        {
          P(F("no missing files to revert"));
          return;
        }

      std::vector<file_path> missing_files;
      for (path_set::const_iterator i = missing.begin(); i != missing.end(); i++)
        {
          file_path fp(*i);
          L(FL("missing files are '%s'") % fp);
          missing_files.push_back(fp);
        }
      // replace the original mask with a more restricted one
      mask = node_restriction(missing_files, std::vector<file_path>(),
                              old_roster, new_roster, app);
    }

  make_restricted_csets(old_roster, new_roster,
                        included, excluded, mask);

  // The included cset will be thrown away (reverted) leaving the
  // excluded cset pending in MTN/work which must be valid against the
  // old roster.

  check_restricted_cset(old_roster, excluded);

  node_map const & nodes = old_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); 
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      if (old_roster.is_root(nid))
        continue;

      split_path sp;
      old_roster.get_name(nid, sp);
      file_path fp(sp);

      if (!mask.includes(old_roster, nid))
        continue;

      if (is_file_t(node))
        {
          file_t f = downcast_to_file_t(node);
          if (file_exists(fp))
            {
              hexenc<id> ident;
              calculate_ident(fp, ident, app.lua);
              // don't touch unchanged files
              if (ident == f->content.inner())
                continue;
            }

          P(F("reverting %s") % fp);
          L(FL("reverting %s to [%s]") % fp % f->content);

          N(app.db.file_version_exists(f->content),
            F("no file version %s found in database for %s")
            % f->content % fp);

          file_data dat;
          L(FL("writing file %s to %s")
            % f->content % fp);
          app.db.get_file_version(f->content, dat);
          write_localized_data(fp, dat.inner(), app.lua);
        }
      else
        {
          if (!directory_exists(fp))
            {
              P(F("recreating %s/") % fp);
              mkdir_p(fp);
            }
        }
    }

  // Included_work is thrown away which effectively reverts any adds,
  // drops and renames it contains. Drops and rename sources will have
  // been rewritten above but this may leave rename targets laying
  // around.

  // Race.
  put_work_cset(excluded);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}

CMD(disapprove, N_("review"), N_("REVISION"),
    N_("disapprove of a particular revision"),
    option::branch_name)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id r;
  revision_t rev, rev_inverse;
  shared_ptr<cset> cs_inverse(new cset());
  complete(app, idx(args, 0)(), r);
  app.db.get_revision(r, rev);

  N(rev.edges.size() == 1,
    F("revision '%s' has %d changesets, cannot invert\n") % r % rev.edges.size());

  cert_value branchname;
  guess_branch(r, app, branchname);
  N(app.branch_name() != "", F("need --branch argument for disapproval"));

  edge_entry const & old_edge (*rev.edges.begin());
  app.db.get_revision_manifest(edge_old_revision(old_edge),
                               rev_inverse.new_manifest);
  {
    roster_t old_roster, new_roster;
    app.db.get_roster(edge_old_revision(old_edge), old_roster);
    app.db.get_roster(r, new_roster);
    make_cset(new_roster, old_roster, *cs_inverse);
  }
  rev_inverse.edges.insert(make_pair(r, cs_inverse));

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    revision_id inv_id;
    revision_data rdat;

    write_revision(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    dbw.consume_revision_data(inv_id, rdat);

    cert_revision_in_branch(inv_id, branchname, app, dbw);
    cert_revision_date_now(inv_id, app, dbw);
    cert_revision_author_default(inv_id, app, dbw);
    cert_revision_changelog(inv_id, 
                            (FL("disapproval of revision '%s'") 
                             % r).str(), app, dbw);
    guard.commit();
  }
}


CMD(add, N_("workspace"), N_("[PATH]..."),
    N_("add files to workspace"), option::unknown)
{
  if (!app.unknown && (args.size() < 1))
    throw usage(name);

  app.require_workspace();

  path_set paths;
  if (app.unknown)
    {
      vector<file_path> roots = args_to_paths(args);
      path_restriction mask(roots, args_to_paths(app.exclude_patterns), app);
      path_set ignored;

      // if no starting paths have been specified use the workspace root
      if (roots.empty())
        roots.push_back(file_path());

      find_unknown_and_ignored(app, mask, roots, paths, ignored);
    }
  else
    for (vector<utf8>::const_iterator i = args.begin(); 
         i != args.end(); ++i)
      {
        split_path sp;
        file_path_external(*i).split(sp);
        paths.insert(sp);
      }

  bool add_recursive = !app.unknown;
  perform_additions(paths, app, add_recursive);
}

CMD(drop, N_("workspace"), N_("[PATH]..."),
    N_("drop files from workspace"), option::execute % option::missing % option::recursive)
{
  if (!app.missing && (args.size() < 1))
    throw usage(name);

  app.require_workspace();

  path_set paths;
  if (app.missing)
    {
      temp_node_id_source nis;
      roster_t current_roster_shape;
      get_current_roster_shape(current_roster_shape, nis, app);
      node_restriction mask(args_to_paths(args), args_to_paths(app.exclude_patterns),
                            current_roster_shape, app);
      find_missing(current_roster_shape, mask, paths);
    }
  else
    for (vector<utf8>::const_iterator i = args.begin(); 
         i != args.end(); ++i)
      {
        split_path sp;
        file_path_external(*i).split(sp);
        paths.insert(sp);
      }

  perform_deletions(paths, app);
}

ALIAS(rm, drop);


CMD(rename, N_("workspace"),
    N_("SRC DEST\n"
       "SRC1 [SRC2 [...]] DEST_DIR"),
    N_("rename entries in the workspace"),
    option::execute)
{
  if (args.size() < 2)
    throw usage(name);

  app.require_workspace();

  file_path dst_path = file_path_external(args.back());

  set<file_path> src_paths;
  for (size_t i = 0; i < args.size()-1; i++)
    {
      file_path s = file_path_external(idx(args, i));
      src_paths.insert(s);
    }
  perform_rename(src_paths, dst_path, app);
}

ALIAS(mv, rename)


  CMD(pivot_root, N_("workspace"), N_("NEW_ROOT PUT_OLD"),
      N_("rename the root directory\n"
         "after this command, the directory that currently "
         "has the name NEW_ROOT\n"
         "will be the root directory, and the directory "
         "that is currently the root\n"
         "directory will have name PUT_OLD.\n"
         "Using --execute is strongly recommended."),
    option::execute)
{
  if (args.size() != 2)
    throw usage(name);

  app.require_workspace();
  file_path new_root = file_path_external(idx(args, 0));
  file_path put_old = file_path_external(idx(args, 1));
  perform_pivot_root(new_root, put_old, app);
}

CMD(status, N_("informative"), N_("[PATH]..."), N_("show status of workspace"),
    option::depth % option::exclude)
{
  roster_t old_roster, new_roster, restricted_roster;
  cset included, excluded;
  revision_id old_rev_id;
  revision_t rev;
  data tmp;
  temp_node_id_source nis;

  app.require_workspace();
  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.exclude_patterns),
                        old_roster, new_roster, app);

  update_current_roster_from_filesystem(new_roster, mask, app);
  make_restricted_csets(old_roster, new_roster, 
                        included, excluded, mask);
  check_restricted_cset(old_roster, included);

  restricted_roster = old_roster;
  editable_roster_base er(restricted_roster, nis);
  included.apply_to(er);

  get_revision_id(old_rev_id);
  make_revision(old_rev_id, old_roster, restricted_roster, rev);

  // We intentionally do not collapse the final \n into the format
  // strings here, for consistency with newline conventions used by most
  // other format strings.
  cout << (F("Current branch: %s") % app.branch_name).str() << "\n";
  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      cout << (F("Changes against parent %s:") % parent).str() << "\n";

      cset const & cs = edge_changes(*i);

      if (cs.empty())
        cout << F("  no changes").str() << "\n";

      for (path_set::const_iterator i = cs.nodes_deleted.begin();
            i != cs.nodes_deleted.end(); ++i)
        cout << (F("  dropped %s") % *i).str() << "\n";

      for (map<split_path, split_path>::const_iterator
            i = cs.nodes_renamed.begin();
            i != cs.nodes_renamed.end(); ++i)
        cout << (F("  renamed %s\n"
                   "       to %s") % i->first % i->second).str() << "\n";

      for (path_set::const_iterator i = cs.dirs_added.begin();
            i != cs.dirs_added.end(); ++i)
        cout << (F("  added   %s") % *i).str() << "\n";

      for (map<split_path, file_id>::const_iterator i = cs.files_added.begin();
            i != cs.files_added.end(); ++i)
        cout << (F("  added   %s") % i->first).str() << "\n";

      for (map<split_path, pair<file_id, file_id> >::const_iterator
              i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
        cout << (F("  patched %s") % (i->first)).str() << "\n";
    }
}

CMD(checkout, N_("tree"), N_("[DIRECTORY]\n"),
    N_("check out a revision from database into directory.\n"
       "If a revision is given, that's the one that will be checked out.\n"
       "Otherwise, it will be the head of the branch (given or implicit).\n"
       "If no directory is given, the branch name will be used as directory"),
    option::branch_name % option::revision)
{
  revision_id ident;
  system_path dir;

  transaction_guard guard(app.db, false);

  if (args.size() > 1 || app.revision_selectors.size() > 1)
    throw usage(name);

  if (app.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.branch_name().empty(), 
        F("use --revision or --branch to specify what to checkout"));

      set<revision_id> heads;
      get_branch_heads(app.branch_name(), app, heads);
      N(heads.size() > 0, 
        F("branch '%s' is empty") % app.branch_name);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.branch_name);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s") % describe_revision(app, *i));
          P(F("choose one with '%s checkout -r<id>'") % ui.prog_name);
          E(false, F("branch %s has multiple heads") % app.branch_name);
        }
      ident = *(heads.begin());
    }
  else if (app.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app, idx(app.revision_selectors, 0)(), ident);
      N(app.db.revision_exists(ident),
        F("no such revision '%s'") % ident);

      cert_value b;
      guess_branch(ident, app, b);

      I(!app.branch_name().empty());
      cert_value branch_name(app.branch_name());
      base64<cert_value> branch_encoded;
      encode_base64(branch_name, branch_encoded);

      vector< revision<cert> > certs;
      app.db.get_revision_certs(ident, branch_cert_name, branch_encoded, certs);

      L(FL("found %d %s branch certs on revision %s")
        % certs.size()
        % app.branch_name
        % ident);

      N(certs.size() != 0, F("revision %s is not a member of branch %s")
        % ident % app.branch_name);
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
        N(!app.branch_name().empty(), 
          F("you must specify a destination directory"));
        dir = system_path(app.branch_name());
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

  app.create_workspace(dir);

  file_data data;
  roster_t ros;
  marking_map mm;

  put_revision_id(ident);

  L(FL("checking out revision %s to directory %s") % ident % dir);
  app.db.get_roster(ident, ros, mm);

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); 
       i != nodes.end(); ++i)
    {
      node_t node = i->second;
      split_path sp;
      ros.get_name(i->first, sp);
      file_path path(sp);

      if (is_dir_t(node))
        {
          if (!workspace_root(sp))
            mkdir_p(path);
        }
      else
        {
          file_t file = downcast_to_file_t(node);
          N(app.db.file_version_exists(file->content),
            F("no file %s found in database for %s")
            % file->content % path);

          file_data dat;
          L(FL("writing file %s to %s")
            % file->content % path);
          app.db.get_file_version(file->content, dat);
          write_localized_data(path, dat.inner(), app.lua);
        }
    }
  remove_work_cset();
  update_any_attrs(app);
  maybe_update_inodeprints(app);
  guard.commit();
}

ALIAS(co, checkout)

CMD(attr, N_("workspace"), N_("set PATH ATTR VALUE\nget PATH [ATTR]\ndrop PATH [ATTR]"),
    N_("set, get or drop file attributes"),
    option::none)
{
  if (args.size() < 2 || args.size() > 4)
    throw usage(name);

  roster_t old_roster, new_roster;
  temp_node_id_source nis;

  app.require_workspace();
  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);


  file_path path = file_path_external(idx(args,1));
  split_path sp;
  path.split(sp);

  N(new_roster.has_node(sp), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(sp);

  string subcmd = idx(args, 0)();
  if (subcmd == "set" || subcmd == "drop")
    {
      if (subcmd == "set")
        {
          if (args.size() != 4)
            throw usage(name);

          attr_key a_key = idx(args, 2)();
          attr_value a_value = idx(args, 3)();

          node->attrs[a_key] = make_pair(true, a_value);
        }
      else
        {
          // Clear all attrs (or a specific attr).
          if (args.size() == 2)
            {
              for (full_attr_map_t::iterator i = node->attrs.begin();
                   i != node->attrs.end(); ++i)
                i->second = make_pair(false, "");
            }
          else if (args.size() == 3)
            {
              attr_key a_key = idx(args, 2)();
              N(node->attrs.find(a_key) != node->attrs.end(),
                F("Path '%s' does not have attribute '%s'\n")
                % path % a_key);
              node->attrs[a_key] = make_pair(false, "");
            }
          else
            throw usage(name);
        }

      cset new_work;
      make_cset(old_roster, new_roster, new_work);
      put_work_cset(new_work);
      update_any_attrs(app);
    }
  else if (subcmd == "get")
    {
      if (args.size() == 2)
        {
          bool has_any_live_attrs = false;
          for (full_attr_map_t::const_iterator i = node->attrs.begin();
               i != node->attrs.end(); ++i)
            if (i->second.first)
              {
                cout << path << " : " 
                     << i->first << "=" 
                     << i->second.second << "\n";
                has_any_live_attrs = true;
              }
          if (!has_any_live_attrs)
            cout << F("No attributes for '%s'") % path << "\n";
        }
      else if (args.size() == 3)
        {
          attr_key a_key = idx(args, 2)();
          full_attr_map_t::const_iterator i = node->attrs.find(a_key);
          if (i != node->attrs.end() && i->second.first)
            cout << path << " : " 
                 << i->first << "=" 
                 << i->second.second << "\n";
          else
            cout << (F("No attribute '%s' on path '%s'") 
                     % a_key % path) << "\n";
        }
      else
        throw usage(name);
    }
  else
    throw usage(name);
}



CMD(commit, N_("workspace"), N_("[PATH]..."),
    N_("commit workspace to database"),
    option::branch_name % option::message % option::msgfile % option::date % 
    option::author % option::depth % option::exclude)
{
  string log_message("");
  bool log_message_given;
  revision_t restricted_rev;
  revision_id old_rev_id, restricted_rev_id;
  roster_t old_roster, new_roster, restricted_roster;
  temp_node_id_source nis;
  cset included, excluded;

  app.make_branch_sticky();
  app.require_workspace();
  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.exclude_patterns),
                        old_roster, new_roster, app);

  update_current_roster_from_filesystem(new_roster, mask, app);
  make_restricted_csets(old_roster, new_roster, 
                        included, excluded, mask);
  check_restricted_cset(old_roster, included);

  restricted_roster = old_roster;
  editable_roster_base er(restricted_roster, nis);
  included.apply_to(er);

  get_revision_id(old_rev_id);
  make_revision(old_rev_id, old_roster, 
                    restricted_roster, restricted_rev);

  calculate_ident(restricted_rev, restricted_rev_id);

  N(restricted_rev.is_nontrivial(), F("no changes to commit"));

  cert_value branchname;
  I(restricted_rev.edges.size() == 1);

  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  unsigned int old_head_size = heads.size();

  if (app.branch_name() != "")
    branchname = app.branch_name();
  else
    guess_branch(edge_old_revision(restricted_rev.edges.begin()), app, branchname);

  P(F("beginning commit on branch '%s'") % branchname);
  L(FL("new manifest '%s'\n"
       "new revision '%s'\n")
    % restricted_rev.new_manifest
    % restricted_rev_id);

  process_commit_message_args(log_message_given, log_message, app);

  N(!(log_message_given && has_contents_user_log()),
    F("_MTN/log is non-empty and log message "
      "was specified on command line\n"
      "perhaps move or delete _MTN/log,\n"
      "or remove --message/--message-file from the command line?"));

  if (!log_message_given)
    {
      // This call handles _MTN/log.

      get_log_message_interactively(restricted_rev, app, log_message);

      // We only check for empty log messages when the user entered them
      // interactively.  Consensus was that if someone wanted to explicitly
      // type --message="", then there wasn't any reason to stop them.
      N(log_message.find_first_not_of("\n\r\t ") != string::npos,
        F("empty log message; commit canceled"));

      // We save interactively entered log messages to _MTN/log, so if
      // something goes wrong, the next commit will pop up their old
      // log message by default. We only do this for interactively
      // entered messages, because otherwise 'monotone commit -mfoo'
      // giving an error, means that after you correct that error and
      // hit up-arrow to try again, you get an "_MTN/log non-empty and
      // message given on command line" error... which is annoying.

      write_user_log(data(log_message));
    }

  // If the hook doesn't exist, allow the message to be used.
  bool message_validated;
  string reason, new_manifest_text;

  dump(restricted_rev, new_manifest_text);

  app.lua.hook_validate_commit_message(log_message, new_manifest_text,
                                       message_validated, reason);
  N(message_validated, F("log message rejected: %s") % reason);

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    if (app.db.revision_exists(restricted_rev_id))
      {
        W(F("revision %s already in database") % restricted_rev_id);
      }
    else
      {
        // new revision
        L(FL("inserting new revision %s") % restricted_rev_id);

        I(restricted_rev.edges.size() == 1);
        edge_map::const_iterator edge = restricted_rev.edges.begin();
        I(edge != restricted_rev.edges.end());

        // process file deltas or new files
        cset const & cs = edge_changes(edge);

        for (map<split_path, pair<file_id, file_id> >::const_iterator 
               i = cs.deltas_applied.begin();
             i != cs.deltas_applied.end(); ++i)
          {
            file_path path(i->first);
            file_id old_content = i->second.first;
            file_id new_content = i->second.second;

            if (app.db.file_version_exists(new_content))
              {
                L(FL("skipping file delta %s, already in database")
                  % delta_entry_dst(i));
              }
            else if (app.db.file_version_exists(old_content))
              {
                L(FL("inserting delta %s -> %s")
                  % old_content % new_content);
                file_data old_data;
                data new_data;
                app.db.get_file_version(old_content, old_data);
                read_localized_data(path, new_data, app.lua);
                // sanity check
                hexenc<id> tid;
                calculate_ident(new_data, tid);
                N(tid == new_content.inner(),
                  F("file '%s' modified during commit, aborting")
                  % path);
                delta del;
                diff(old_data.inner(), new_data, del);
                dbw.consume_file_delta(old_content,
                                       new_content,
                                       file_delta(del));
              }
            else
              // If we don't err out here, our packet writer will
              // later.
              E(false, 
                F("Your database is missing version %s of file '%s'")
                % old_content % path);
          }

        for (map<split_path, file_id>::const_iterator 
               i = cs.files_added.begin();
             i != cs.files_added.end(); ++i)
          {
            file_path path(i->first);
            file_id new_content = i->second;

            L(FL("inserting full version %s") % new_content);
            data new_data;
            read_localized_data(path, new_data, app.lua);
            // sanity check
            hexenc<id> tid;
            calculate_ident(new_data, tid);
            N(tid == new_content.inner(),
              F("file '%s' modified during commit, aborting")
              % path);
            dbw.consume_file_data(new_content, file_data(new_data));
          }
      }

    revision_data rdat;
    write_revision(restricted_rev, rdat);
    dbw.consume_revision_data(restricted_rev_id, rdat);

    cert_revision_in_branch(restricted_rev_id, branchname, app, dbw);
    if (app.date_set)
      cert_revision_date_time(restricted_rev_id, app.date, app, dbw);
    else
      cert_revision_date_now(restricted_rev_id, app, dbw);

    if (app.author().length() > 0)
      cert_revision_author(restricted_rev_id, app.author(), app, dbw);
    else
      cert_revision_author_default(restricted_rev_id, app, dbw);

    cert_revision_changelog(restricted_rev_id, log_message, app, dbw);
    guard.commit();
  }

  // small race condition here...
  put_work_cset(excluded);
  put_revision_id(restricted_rev_id);
  P(F("committed revision %s") % restricted_rev_id);

  blank_user_log();

  get_branch_heads(app.branch_name(), app, heads);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run '%s merge'")
      % ui.prog_name);
  }

  update_any_attrs(app);
  maybe_update_inodeprints(app);

  {
    // Tell lua what happened. Yes, we might lose some information
    // here, but it's just an indicator for lua, eg. to post stuff to
    // a mailing list. If the user *really* cares about cert validity,
    // multiple certs with same name, etc. they can inquire further,
    // later.
    map<cert_name, cert_value> certs;
    vector< revision<cert> > ctmp;
    app.db.get_revision_certs(restricted_rev_id, ctmp);
    for (vector< revision<cert> >::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      {
        cert_value vtmp;
        decode_base64(i->inner().value, vtmp);
        certs.insert(make_pair(i->inner().name, vtmp));
      }
    revision_data rdat;
    app.db.get_revision(restricted_rev_id, rdat);
    app.lua.hook_note_commit(restricted_rev_id, rdat, certs);
  }
}

ALIAS(ci, commit);


CMD_NO_WORKSPACE(setup, N_("tree"), N_("[DIRECTORY]"),
    N_("setup a new workspace directory, default to current"), 
    option::branch_name)
{
  if (args.size() > 1)
    throw usage(name);

  N(!app.branch_name().empty(), F("need --branch argument for setup"));
  app.db.ensure_open();

  string dir;
  if (args.size() == 1)
    dir = idx(args,0)();
  else
    dir = ".";

  app.create_workspace(dir);
  revision_id null;
  put_revision_id(null);
}

CMD(refresh_inodeprints, N_("tree"), "", N_("refresh the inodeprint cache"),
    option::none)
{
  app.require_workspace();
  enable_inodeprints();
  maybe_update_inodeprints(app);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
