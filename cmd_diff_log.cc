// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <deque>
#include <iostream>
#include <map>
#include <sstream>

#include "cmd.hh"
#include "diff_patch.hh"
#include "localized_file_io.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"

using std::cout;
using std::deque;
using std::make_pair;
using std::map;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::lexical_cast;

// The changes_summary structure holds a list all of files and directories
// affected in a revision, and is useful in the 'log' command to print this
// information easily.  It has to be constructed from all cset objects
// that belong to a revision.

struct
changes_summary
{
  cset cs;
  changes_summary(void);
  void add_change_set(cset const & cs);
  void print(ostream & os, size_t max_cols) const;
};

changes_summary::changes_summary(void)
{
}

void
changes_summary::add_change_set(cset const & c)
{
  if (c.empty())
    return;

  // FIXME: not sure whether it matters for an informal summary
  // object like this, but the pre-state names in deletes and renames
  // are not really sensible to union; they refer to different trees
  // so mixing them up in a single set is potentially ambiguous.

  copy(c.nodes_deleted.begin(), c.nodes_deleted.end(),
       inserter(cs.nodes_deleted, cs.nodes_deleted.begin()));

  copy(c.files_added.begin(), c.files_added.end(),
       inserter(cs.files_added, cs.files_added.begin()));

  copy(c.dirs_added.begin(), c.dirs_added.end(),
       inserter(cs.dirs_added, cs.dirs_added.begin()));

  copy(c.nodes_renamed.begin(), c.nodes_renamed.end(),
       inserter(cs.nodes_renamed, cs.nodes_renamed.begin()));

  copy(c.deltas_applied.begin(), c.deltas_applied.end(),
       inserter(cs.deltas_applied, cs.deltas_applied.begin()));

  copy(c.attrs_cleared.begin(), c.attrs_cleared.end(),
       inserter(cs.attrs_cleared, cs.attrs_cleared.begin()));

  copy(c.attrs_set.begin(), c.attrs_set.end(),
       inserter(cs.attrs_set, cs.attrs_set.begin()));
}

static void
print_indented_set(ostream & os,
                   path_set const & s,
                   size_t max_cols)
{
  size_t cols = 8;
  os << "       ";
  for (path_set::const_iterator i = s.begin();
       i != s.end(); i++)
    {
      const string str = lexical_cast<string>(file_path(*i));
      if (cols > 8 && cols + str.size() + 1 >= max_cols)
        {
          cols = 8;
          os << "\n" << "       ";
        }
      os << " " << str;
      cols += str.size() + 1;
    }
  os << "\n";
}

void
changes_summary::print(ostream & os, size_t max_cols) const
{

  if (! cs.nodes_deleted.empty())
    {
      os << "Deleted entries:" << "\n";
      print_indented_set(os, cs.nodes_deleted, max_cols);
    }

  if (! cs.nodes_renamed.empty())
    {
      os << "Renamed entries:" << "\n";
      for (map<split_path, split_path>::const_iterator
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); i++)
        os << "        " << file_path(i->first) 
           << " to " << file_path(i->second) << "\n";
    }

  if (! cs.files_added.empty())
    {
      path_set tmp;
      for (map<split_path, file_id>::const_iterator 
             i = cs.files_added.begin();
           i != cs.files_added.end(); ++i)
        tmp.insert(i->first);
      os << "Added files:" << "\n";
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.dirs_added.empty())
    {
      os << "Added directories:" << "\n";
      print_indented_set(os, cs.dirs_added, max_cols);
    }

  if (! cs.deltas_applied.empty())
    {
      path_set tmp;
      for (map<split_path, pair<file_id, file_id> >::const_iterator 
             i = cs.deltas_applied.begin();
           i != cs.deltas_applied.end(); ++i)
        tmp.insert(i->first);
      os << "Modified files:" << "\n";
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.attrs_set.empty() || ! cs.attrs_cleared.empty())
    {
      path_set tmp;
      for (set<pair<split_path, attr_key> >::const_iterator 
             i = cs.attrs_cleared.begin();
           i != cs.attrs_cleared.end(); ++i)
        tmp.insert(i->first);

      for (map<pair<split_path, attr_key>, attr_value>::const_iterator 
             i = cs.attrs_set.begin();
           i != cs.attrs_set.end(); ++i)
        tmp.insert(i->first.first);

      os << "Modified attrs:" << "\n";
      print_indented_set(os, tmp, max_cols);
    }
}

static void
do_external_diff(cset const & cs,
                 app_state & app,
                 bool new_is_archived)
{
  for (map<split_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      data data_old;
      data data_new;

      file_data f_old;
      app.db.get_file_version(delta_entry_src(i), f_old);
      data_old = f_old.inner();

      if (new_is_archived)
        {
          file_data f_new;
          app.db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          read_localized_data(file_path(delta_entry_path(i)),
                              data_new, app.lua);
        }

      bool is_binary = false;
      if (guess_binary(data_old()) ||
          guess_binary(data_new()))
        is_binary = true;

      app.lua.hook_external_diff(file_path(delta_entry_path(i)),
                                 data_old,
                                 data_new,
                                 is_binary,
                                 app.diff_args_provided,
                                 app.diff_args(),
                                 delta_entry_src(i).inner()(),
                                 delta_entry_dst(i).inner()());
    }
}

static void
dump_diffs(cset const & cs,
           app_state & app,
           bool new_is_archived,
           set<split_path> const & paths,
           bool limit_paths = false)
{
  // 60 is somewhat arbitrary, but less than 80
  string patch_sep = string(60, '=');

  for (map<split_path, file_id>::const_iterator
         i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      if (limit_paths && paths.find(i->first) == paths.end())
        continue;

      cout << patch_sep << "\n";
      data unpacked;
      vector<string> lines;

      if (new_is_archived)
        {
          file_data dat;
          app.db.get_file_version(i->second, dat);
          unpacked = dat.inner();
        }
      else
        {
          read_localized_data(file_path(i->first),
                              unpacked, app.lua);
        }

      std::string pattern("");
      if (app.diff_show_encloser)
        app.lua.hook_get_encloser_pattern(file_path(i->first),
                                          pattern);

      make_diff(file_path(i->first).as_internal(),
                file_path(i->first).as_internal(),
                i->second,
                i->second,
                data(), unpacked,
                cout, app.diff_format, pattern);
    }

  map<split_path, split_path> reverse_rename_map;

  for (map<split_path, split_path>::const_iterator
         i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      reverse_rename_map.insert(make_pair(i->second, i->first));
    }

  for (map<split_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      if (limit_paths && paths.find(i->first) == paths.end())
        continue;

      file_data f_old;
      data data_old, data_new;

      cout << patch_sep << "\n";

      app.db.get_file_version(delta_entry_src(i), f_old);
      data_old = f_old.inner();

      if (new_is_archived)
        {
          file_data f_new;
          app.db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          read_localized_data(file_path(delta_entry_path(i)),
                              data_new, app.lua);
        }

      split_path dst_path = delta_entry_path(i);
      split_path src_path = dst_path;
      map<split_path, split_path>::const_iterator re;
      re = reverse_rename_map.find(dst_path);
      if (re != reverse_rename_map.end())
        src_path = re->second;

      std::string pattern("");
      if (app.diff_show_encloser)
        app.lua.hook_get_encloser_pattern(file_path(src_path),
                                          pattern);

      make_diff(file_path(src_path).as_internal(),
                file_path(dst_path).as_internal(),
                delta_entry_src(i),
                delta_entry_dst(i),
                data_old, data_new,
                cout, app.diff_format, pattern);
    }
}

static void
dump_diffs(cset const & cs,
           app_state & app,
           bool new_is_archived)
{
  set<split_path> dummy;
  dump_diffs(cs, app, new_is_archived, dummy);
}

CMD(diff, N_("informative"), N_("[PATH]..."),
    N_("show current diffs on stdout.\n"
    "If one revision is given, the diff between the workspace and\n"
    "that revision is shown.  If two revisions are given, the diff between\n"
    "them is given.  If no format is specified, unified is used by default."),
    option::revision % option::depth % option::exclude % option::unified_diff
    % option::context_diff % option::external_diff % option::external_diff_args)
{
  bool new_is_archived;
  ostringstream header;
  temp_node_id_source nis;

  if (app.diff_args_provided)
    N(app.diff_format == external_diff,
      F("--diff-args requires --external\n"
        "try adding --external or removing --diff-args?"));

  cset included, excluded;

  // initialize before transaction so we have a database to work with.

  if (app.revision_selectors.size() == 0)
    app.require_workspace();
  else if (app.revision_selectors.size() == 1)
    app.require_workspace();

  if (app.revision_selectors.size() == 0)
    {
      roster_t new_roster, old_roster;
      revision_id old_rid;

      app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
      app.work.get_revision_id(old_rid);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.exclude_patterns),
                            old_roster, new_roster, app);

      app.work.update_current_roster_from_filesystem(new_roster, mask);
      make_restricted_csets(old_roster, new_roster, 
                            included, excluded, mask);
      check_restricted_cset(old_roster, included);

      new_is_archived = false;
      header << "# old_revision [" << old_rid << "]" << "\n";
    }
  else if (app.revision_selectors.size() == 1)
    {
      roster_t new_roster, old_roster;
      revision_id r_old_id;

      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);

      app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
      // Clobber old_roster with the one specified
      app.db.get_roster(r_old_id, old_roster);

      // FIXME: handle no ancestor case
      // N(r_new.edges.size() == 1, F("current revision has no ancestor"));

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.exclude_patterns), 
                            old_roster, new_roster, app);

      app.work.update_current_roster_from_filesystem(new_roster, mask);
      make_restricted_csets(old_roster, new_roster, 
                            included, excluded, mask);
      check_restricted_cset(old_roster, included);

      new_is_archived = false;
      header << "# old_revision [" << r_old_id << "]" << "\n";
    }
  else if (app.revision_selectors.size() == 2)
    {
      roster_t new_roster, old_roster;
      revision_id r_old_id, r_new_id;

      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      complete(app, idx(app.revision_selectors, 1)(), r_new_id);

      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);
      N(app.db.revision_exists(r_new_id),
        F("no such revision '%s'") % r_new_id);

      app.db.get_roster(r_old_id, old_roster);
      app.db.get_roster(r_new_id, new_roster);

      node_restriction mask(args_to_paths(args),
                            args_to_paths(app.exclude_patterns),
                            old_roster, new_roster, app);

      // FIXME: this is *possibly* a UI bug, insofar as we
      // look at the restriction name(s) you provided on the command
      // line in the context of new and old, *not* the working copy.
      // One way of "fixing" this is to map the filenames on the command
      // line to node_ids, and then restrict based on those. This
      // might be more intuitive; on the other hand it would make it
      // impossible to restrict to paths which are dead in the working
      // copy but live between old and new. So ... no rush to "fix" it;
      // discuss implications first.
      //
      // Let the discussion begin...
      //
      // - "map filenames on the command line to node_ids" needs to be done
      //   in the context of some roster, possibly the working copy base or
      //   the current working copy (or both)
      // - diff with two --revision's may be done with no working copy
      // - some form of "peg" revision syntax for paths that would allow
      //   for each path to specify which revision it is relevant to is
      //   probably the "right" way to go eventually. something like file@rev
      //   (which fails for paths with @'s in them) or possibly //rev/file
      //   since versioned paths are required to be relative.

      make_restricted_csets(old_roster, new_roster, 
                            included, excluded, mask);
      check_restricted_cset(old_roster, included);

      new_is_archived = true;
    }
  else
    {
      throw usage(name);
    }


  data summary;
  write_cset(included, summary);

  vector<string> lines;
  split_into_lines(summary(), lines);
  cout << "# " << "\n";
  if (summary().size() > 0)
    {
      cout << header.str() << "# " << "\n";
      for (vector<string>::iterator i = lines.begin(); 
           i != lines.end(); ++i)
        cout << "# " << *i << "\n";
    }
  else
    {
      cout << "# " << _("no changes") << "\n";
    }
  cout << "# " << "\n";

  if (app.diff_format == external_diff) {
    do_external_diff(included, app, new_is_archived);
  } else
    dump_diffs(included, app, new_is_archived);
}

static void
log_certs(app_state & app, revision_id id, cert_name name,
          string label, string separator,
          bool multiline, bool newline)
{
  vector< revision<cert> > certs;
  bool first = true;

  if (multiline)
    newline = true;

  app.db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, app);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);

      if (first)
        cout << label;
      else
        cout << separator;

      if (multiline)
        {
          cout << "\n" << "\n" << tv;
          if (newline)
            cout << "\n";
        }
      else
        {
          cout << tv;
          if (newline)
            cout << "\n";
        }

      first = false;
    }
}

static void
log_certs(app_state & app, revision_id id, cert_name name, 
          string label, bool multiline)
{
  log_certs(app, id, name, label, label, multiline, true);
}

static void
log_certs(app_state & app, revision_id id, cert_name name)
{
  log_certs(app, id, name, " ", ",", false, false);
}

CMD(log, N_("informative"), N_("[FILE] ..."),
    N_("print history in reverse order (filtering by 'FILE'). If one or more\n"
    "revisions are given, use them as a starting point."),
    option::last % option::next % option::revision % option::brief
    % option::diffs % option::no_merges % option::no_files)
{
  if (app.revision_selectors.size() == 0)
    app.require_workspace("try passing a --revision to start at");

  temp_node_id_source nis;
  set<revision_id> frontier;
  revision_id first_rid;

  if (app.revision_selectors.size() == 0)
    {
      app.work.get_revision_id(first_rid);
      frontier.insert(first_rid);
    }
  else
    {
      for (vector<utf8>::const_iterator i = app.revision_selectors.begin();
           i != app.revision_selectors.end(); i++)
        {
          set<revision_id> rids;
          complete(app, (*i)(), rids);
          frontier.insert(rids.begin(), rids.end());
          if (i == app.revision_selectors.begin())
            first_rid = *rids.begin();
        }
    }

  node_restriction mask(app);

  if (args.size() > 0)
    {
      // User wants to trace only specific files
      roster_t old_roster, new_roster;

      if (app.revision_selectors.size() == 0)
        app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);
      else
        app.db.get_roster(first_rid, new_roster);

      // FIXME_RESTRICTIONS: should this add paths from the rosters of
      // all selected revs?
      mask = node_restriction(args_to_paths(args),
                              args_to_paths(app.exclude_patterns), 
                              old_roster, new_roster, app);
    }

  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);
  cert_name branch_name(branch_cert_name);
  cert_name tag_name(tag_cert_name);
  cert_name changelog_name(changelog_cert_name);
  cert_name comment_name(comment_cert_name);

  set<revision_id> seen;
  long last = app.last;
  long next = app.next;

  N(last == -1 || next == -1,
    F("only one of --last/--next allowed"));

  revision_t rev;

  while(! frontier.empty() && (last == -1 || last > 0) 
        && (next == -1 || next > 0))
    {
      set<revision_id> next_frontier;

      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        {
          revision_id rid = *i;

          bool print_this = mask.empty();
          set<  revision<id> > parents;
          vector< revision<cert> > tmp;
          set<split_path> diff_paths;

          if (null_id(rid) || seen.find(rid) != seen.end())
            continue;

          seen.insert(rid);
          app.db.get_revision(rid, rev);

          if (!mask.empty())
            {
              // TODO: stop if the restriction is pre-dated by the
              // current roster i.e. the restriction's nodes are not
              // born in the current roster
              roster_t roster;
              app.db.get_roster(rid, roster);

              set<node_id> nodes_modified;
              select_nodes_modified_by_rev(rev, roster,
                                           nodes_modified,
                                           app);

              for (set<node_id>::const_iterator n = nodes_modified.begin();
                   n != nodes_modified.end(); ++n)
                {
                  // a deleted node will be "modified" but won't
                  // exist in the result. 
                  // we don't want to print them.
                  if (roster.has_node(*n) && mask.includes(roster, *n))
                    {
                      print_this = true;
                      if (app.diffs)
                        {
                          split_path sp;
                          roster.get_name(*n, sp);
                          diff_paths.insert(sp);
                        }
                    }
                }
            }

          if (next > 0)
            {
              set<revision_id> children;
              app.db.get_revision_children(rid, children);
              copy(children.begin(), children.end(),
                   inserter(next_frontier, next_frontier.end()));
            }
          else // work backwards by default
            {
              set<revision_id> parents;
              app.db.get_revision_parents(rid, parents);
              copy(parents.begin(), parents.end(),
                   inserter(next_frontier, next_frontier.end()));
            }

          if (app.no_merges && rev.is_merge_node())
            print_this = false;

          if (print_this)
          {
            if (app.brief)
              {
                cout << rid;
                log_certs(app, rid, author_name);
                log_certs(app, rid, date_name);
                log_certs(app, rid, branch_name);
                cout << "\n";
              }
            else
              {
                cout << string(65, '-') << "\n";
                cout << "Revision: " << rid << "\n";

                changes_summary csum;

                set<revision_id> ancestors;

                for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
                  {
                    ancestors.insert(edge_old_revision(e));
                    csum.add_change_set(edge_changes(e));
                  }

                for (set<revision_id>::const_iterator 
                       anc = ancestors.begin();
                     anc != ancestors.end(); ++anc)
                  cout << "Ancestor: " << *anc << "\n";

                log_certs(app, rid, author_name, "Author: ", false);
                log_certs(app, rid, date_name,   "Date: ",   false);
                log_certs(app, rid, branch_name, "Branch: ", false);
                log_certs(app, rid, tag_name,    "Tag: ",    false);

                if (!app.no_files && !csum.cs.empty())
                  {
                    cout << "\n";
                    csum.print(cout, 70);
                    cout << "\n";
                  }

                log_certs(app, rid, changelog_name, "ChangeLog: ", true);
                log_certs(app, rid, comment_name,   "Comments: ",  true);
              }

            if (app.diffs)
              {
                for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
                  {
                    dump_diffs(edge_changes(e), app, true, diff_paths,
                               !mask.empty());
                  }
              }

            if (next > 0)
              {
                next--;
              }
            else if (last > 0)
              {
                last--;
              }

            cout.flush();
          }
        }
      frontier = next_frontier;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
