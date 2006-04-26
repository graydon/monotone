#include "cmd.hh"

#include "revision.hh"
#include "diff_patch.hh"
#include "packet.hh"
#include "transforms.hh"
#include "restrictions.hh"

#include <map>
using std::map;
using std::make_pair;
#include <iostream>
using std::cout;

static void 
get_log_message_interactively(revision_set const & cs, 
                              app_state & app,
                              string & log_message)
{
  string commentary;
  data summary, user_log_message;
  write_revision_set(cs, summary);
  read_user_log(user_log_message);
  commentary += "----------------------------------------------------------------------\n";
  commentary += _("Enter a description of this change.\n"
                  "Lines beginning with `MTN:' are removed automatically.\n");
  commentary += "\n";
  commentary += summary();
  commentary += "----------------------------------------------------------------------\n";

  N(app.lua.hook_edit_comment(commentary, user_log_message(), log_message),
    F("edit of log message failed"));
}

CMD(revert, N_("workspace"), N_("[PATH]..."), 
    N_("revert file(s), dir(s) or entire workspace (\".\")"), 
    OPT_DEPTH % OPT_EXCLUDE % OPT_MISSING)
{
  roster_t old_roster;
  revision_id old_revision_id;
  cset work, included_work, excluded_work;
  path_set old_paths;

  if (args.size() < 1 && !app.missing)
      throw usage(name);
 
  app.require_workspace();

  get_base_revision(app, old_revision_id, old_roster);

  get_work_cset(work);
  old_roster.extract_path_set(old_paths);

  path_set valid_paths(old_paths);

  extract_rearranged_paths(work, valid_paths);
  add_intermediate_paths(valid_paths);

  if (app.missing)
    {
      path_set missing;
      find_missing(app, args, missing);
      if (missing.empty())
        {
          L(FL("no missing files in restriction."));
          return;
        }

      app.set_restriction(valid_paths, missing);
    }
  else
    {
      app.set_restriction(valid_paths, args);
    }

  restrict_cset(work, included_work, excluded_work, app);

  node_map const & nodes = old_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      if (null_node(node->parent))
        continue;

      split_path sp;
      old_roster.get_name(nid, sp);
      file_path fp(sp);
      
      // Only revert restriction-included files.
      if (!app.restriction_includes(sp))
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
          L(FL("reverting %s to [%s]\n") % fp % f->content);
          
          N(app.db.file_version_exists(f->content),
            F("no file version %s found in database for %s")
            % f->content % fp);
          
          file_data dat;
          L(FL("writing file %s to %s\n")
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

  // race
  put_work_cset(excluded_work);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}

CMD(disapprove, N_("review"), N_("REVISION"), 
    N_("disapprove of a particular revision"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id r;
  revision_set rev, rev_inverse;
  boost::shared_ptr<cset> cs_inverse(new cset());
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

    write_revision_set(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    dbw.consume_revision_data(inv_id, rdat);
    
    cert_revision_in_branch(inv_id, branchname, app, dbw); 
    cert_revision_date_now(inv_id, app, dbw);
    cert_revision_author_default(inv_id, app, dbw);
    cert_revision_changelog(inv_id, (boost::format("disapproval of revision '%s'") % r).str(), app, dbw);
    guard.commit();
  }
}


CMD(add, N_("workspace"), N_("[PATH]..."),
    N_("add files to workspace"), OPT_UNKNOWN)
{
  if (!app.unknown && (args.size() < 1))
    throw usage(name);

  app.require_workspace();

  path_set paths;
  if (app.unknown)
    {
      path_set ignored;
      find_unknown_and_ignored(app, false, args, paths, ignored);
    }
  else
    for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
      {
        split_path sp;
        file_path_external(*i).split(sp);
        paths.insert(sp);
      }

  bool add_recursive = !app.unknown; 
  perform_additions(paths, app, add_recursive);
}

CMD(drop, N_("workspace"), N_("[PATH]..."),
    N_("drop files from workspace"), OPT_EXECUTE % OPT_MISSING % OPT_RECURSIVE)
{
  if (!app.missing && (args.size() < 1))
    throw usage(name);

  app.require_workspace();

  path_set paths;
  if (app.missing)
    find_missing(app, args, paths);
  else
    for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
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
    OPT_EXECUTE)
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
       "after this command, the directory that currently has the name NEW_ROOT\n"
       "will be the root directory, and the directory that is currently the root\n"
       "directory will have name PUT_OLD.\n"
       "Using --execute is strongly recommended."),
    OPT_EXECUTE)
{
  if (args.size() != 2)
    throw usage(name);

  app.require_workspace();
  file_path new_root = file_path_external(idx(args, 0));
  file_path put_old = file_path_external(idx(args, 1));
  perform_pivot_root(new_root, put_old, app);
}

CMD(status, N_("informative"), N_("[PATH]..."), N_("show status of workspace"),
    OPT_DEPTH % OPT_EXCLUDE % OPT_BRIEF)
{
  revision_set rs;
  roster_t old_roster, new_roster;
  data tmp;
  temp_node_id_source nis;

  app.require_workspace();
  get_working_revision_and_rosters(app, args, rs, old_roster, new_roster, nis);

  if (global_sanity.brief)
    {
      I(rs.edges.size() == 1);
      cset const & cs = edge_changes(rs.edges.begin());
      
      for (path_set::const_iterator i = cs.nodes_deleted.begin();
           i != cs.nodes_deleted.end(); ++i) 
        cout << "dropped " << *i << "\n";

      for (std::map<split_path, split_path>::const_iterator 
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); ++i) 
        cout << "renamed " << i->first << "\n" 
             << "     to " << i->second << "\n";

      for (path_set::const_iterator i = cs.dirs_added.begin();
           i != cs.dirs_added.end(); ++i) 
        cout << "added   " << *i << "\n";

      for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
           i != cs.files_added.end(); ++i) 
        cout << "added   " << i->first << "\n";

      for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
             i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i) 
        {
          cout << "patched " << i->first << "\n";
        }
    }
  else
    {
      write_revision_set(rs, tmp);
      cout << "\n" << tmp << "\n";
    }
}

CMD(checkout, N_("tree"), N_("[DIRECTORY]\n"),
    N_("check out a revision from database into directory.\n"
    "If a revision is given, that's the one that will be checked out.\n"
    "Otherwise, it will be the head of the branch (given or implicit).\n"
    "If no directory is given, the branch name will be used as directory"),
    OPT_BRANCH_NAME % OPT_REVISION)
{
  revision_id ident;
  system_path dir;
  // we have a special case for "checkout .", i.e., to current dir
  bool checkout_dot = false;

  transaction_guard guard(app.db, false);

  if (args.size() > 1 || app.revision_selectors.size() > 1)
    throw usage(name);

  if (args.size() == 0)
    {
      // no checkout dir specified, use branch name for dir
      N(!app.branch_name().empty(), F("need --branch argument for branch-based checkout"));
      dir = system_path(app.branch_name());
    }
  else
    {
      // checkout to specified dir
      dir = system_path(idx(args, 0));
      if (idx(args, 0) == utf8("."))
        checkout_dot = true;
    }

  if (!checkout_dot)
    require_path_is_nonexistent(dir,
                                F("checkout directory '%s' already exists")
                                % dir);

  if (app.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.branch_name().empty(), F("need --branch argument for branch-based checkout"));
      set<revision_id> heads;
      get_branch_heads(app.branch_name(), app, heads);
      N(heads.size() > 0, F("branch '%s' is empty") % app.branch_name);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.branch_name);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s\n") % describe_revision(app, *i));
          P(F("choose one with '%s checkout -r<id>'") % app.prog_name);
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
          
      L(FL("found %d %s branch certs on revision %s\n") 
        % certs.size()
        % app.branch_name
        % ident);
        
      N(certs.size() != 0, F("revision %s is not a member of branch %s\n") 
        % ident % app.branch_name);
    }

  app.create_workspace(dir);
    
  file_data data;
  roster_t ros;
  marking_map mm;
  
  put_revision_id(ident);
  
  L(FL("checking out revision %s to directory %s\n") % ident % dir);
  app.db.get_roster(ident, ros, mm);
  
  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_t node = i->second;
      split_path sp;
      ros.get_name(i->first, sp);
      file_path path(sp);

      if (is_dir_t(node))
        {
          if (sp.size() == 1)
            I(null_name(idx(sp,0)));
          else
            mkdir_p(path);
        }
      else
        {
          file_t file = downcast_to_file_t(node);
          N(app.db.file_version_exists(file->content),
            F("no file %s found in database for %s")
            % file->content % path);
      
          file_data dat;
          L(FL("writing file %s to %s\n")
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
    OPT_NONE)
{
  if (args.size() < 2 || args.size() > 4)
    throw usage(name);

  roster_t old_roster, new_roster;

  app.require_workspace();
  temp_node_id_source nis;
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
          // Clear all attrs (or a specific attr)
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
                cout << path << " : " << i->first << "=" << i->second.second << "\n";
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
            cout << path << " : " << i->first << "=" << i->second.second << "\n";
          else
            cout << F("No attribute '%s' on path '%s'") % a_key % path << "\n";
        }
      else
        throw usage(name);
    }
  else 
    throw usage(name);
}



CMD(commit, N_("workspace"), N_("[PATH]..."), 
    N_("commit workspace to database"),
    OPT_BRANCH_NAME % OPT_MESSAGE % OPT_MSGFILE % OPT_DATE % 
    OPT_AUTHOR % OPT_DEPTH % OPT_EXCLUDE)
{
  string log_message("");
  bool log_message_given;
  revision_set rs;
  revision_id rid;
  roster_t old_roster, new_roster;
  temp_node_id_source nis;
  
  app.make_branch_sticky();
  app.require_workspace();

  // preserve excluded work for future commmits
  cset excluded_work;
  get_working_revision_and_rosters(app, args, rs, old_roster, new_roster, excluded_work, nis);
  calculate_ident(rs, rid);

  N(rs.is_nontrivial(), F("no changes to commit\n"));
    
  cert_value branchname;
  I(rs.edges.size() == 1);

  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  unsigned int old_head_size = heads.size();

  if (app.branch_name() != "") 
    branchname = app.branch_name();
  else 
    guess_branch(edge_old_revision(rs.edges.begin()), app, branchname);

  P(F("beginning commit on branch '%s'\n") % branchname);
  L(FL("new manifest '%s'\n"
      "new revision '%s'\n")
    % rs.new_manifest
    % rid);

  process_commit_message_args(log_message_given, log_message, app);
  
  N(!(log_message_given && has_contents_user_log()),
    F("_MTN/log is non-empty and log message was specified on command line\n"
      "perhaps move or delete _MTN/log,\n"
      "or remove --message/--message-file from the command line?"));
  
  if (!log_message_given)
    {
      // this call handles _MTN/log
      get_log_message_interactively(rs, app, log_message);
      // we only check for empty log messages when the user entered them
      // interactively.  Consensus was that if someone wanted to explicitly
      // type --message="", then there wasn't any reason to stop them.
      N(log_message.find_first_not_of(" \r\t\n") != string::npos,
        F("empty log message; commit canceled"));
      // we save interactively entered log messages to _MTN/log, so if
      // something goes wrong, the next commit will pop up their old log
      // message by default.  we only do this for interactively entered
      // messages, because otherwise 'monotone commit -mfoo' giving an error,
      // means that after you correct that error and hit up-arrow to try
      // again, you get an "_MTN/log non-empty and message given on command
      // line" error... which is annoying.
      write_user_log(data(log_message));
    }

  // If the hook doesn't exist, allow the message to be used.
  bool message_validated;
  string reason, new_manifest_text;

  dump(rs, new_manifest_text);

  app.lua.hook_validate_commit_message(log_message, new_manifest_text,
                                       message_validated, reason);
  N(message_validated, F("log message rejected: %s\n") % reason);

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    if (app.db.revision_exists(rid))
      {
        W(F("revision %s already in database\n") % rid);
      }
    else
      {
        // new revision
        L(FL("inserting new revision %s\n") % rid);

        I(rs.edges.size() == 1);
        edge_map::const_iterator edge = rs.edges.begin();
        I(edge != rs.edges.end());

        // process file deltas or new files
        cset const & cs = edge_changes(edge);

        for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
             i != cs.deltas_applied.end(); ++i)
          {
            file_path path(i->first);
            file_id old_content = i->second.first;
            file_id new_content = i->second.second;

            if (app.db.file_version_exists(new_content))
              {
                L(FL("skipping file delta %s, already in database\n")
                  % delta_entry_dst(i));
              }
            else if (app.db.file_version_exists(old_content))
              {
                L(FL("inserting delta %s -> %s\n")
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
              // If we don't err out here, our packet writer will later.
              E(false, F("Your database is missing version %s of file '%s'")
                         % old_content % path);
          }

        for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
             i != cs.files_added.end(); ++i)
          {
            file_path path(i->first);
            file_id new_content = i->second;

            L(FL("inserting full version %s\n") % new_content);
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
    write_revision_set(rs, rdat);
    dbw.consume_revision_data(rid, rdat);
  
    cert_revision_in_branch(rid, branchname, app, dbw); 
    if (app.date_set)
      cert_revision_date_time(rid, app.date, app, dbw);
    else
      cert_revision_date_now(rid, app, dbw);
    if (app.author().length() > 0)
      cert_revision_author(rid, app.author(), app, dbw);
    else
      cert_revision_author_default(rid, app, dbw);
    cert_revision_changelog(rid, log_message, app, dbw);
    guard.commit();
  }
  
  // small race condition here...
  put_work_cset(excluded_work);
  put_revision_id(rid);
  P(F("committed revision %s\n") % rid);
  
  blank_user_log();

  get_branch_heads(app.branch_name(), app, heads);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run '%s merge'") 
      % app.prog_name);
  }
    
  update_any_attrs(app);
  maybe_update_inodeprints(app);

  {
    // tell lua what happened. yes, we might lose some information here,
    // but it's just an indicator for lua, eg. to post stuff to a mailing
    // list. if the user *really* cares about cert validity, multiple certs
    // with same name, etc.  they can inquire further, later.
    map<cert_name, cert_value> certs;
    vector< revision<cert> > ctmp;
    app.db.get_revision_certs(rid, ctmp);
    for (vector< revision<cert> >::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      {
        cert_value vtmp;
        decode_base64(i->inner().value, vtmp);
        certs.insert(make_pair(i->inner().name, vtmp));
      }
    revision_data rdat;
    app.db.get_revision(rid, rdat);
    app.lua.hook_note_commit(rid, rdat, certs);
  }
}

ALIAS(ci, commit);


CMD_NO_WORKSPACE(setup, N_("tree"), N_("[DIRECTORY]"), N_("setup a new workspace directory, default to current"),
    OPT_BRANCH_NAME)
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
    OPT_NONE)
{
  app.require_workspace();
  enable_inodeprints();
  maybe_update_inodeprints(app);
}
