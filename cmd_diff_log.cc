#include "cmd.hh"
#include "diff_patch.hh"
#include "revision.hh"
#include "transforms.hh"
#include "restrictions.hh"

using std::set;
#include <map>
using std::map;
#include <iostream>
using std::cout;
#include <sstream>
using std::ostringstream;
#include <deque>
using std::deque;

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
  void print(std::ostream & os, size_t max_cols) const;
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
print_indented_set(std::ostream & os, 
                   path_set const & s,
                   size_t max_cols)
{
  size_t cols = 8;
  os << "       ";
  for (path_set::const_iterator i = s.begin();
       i != s.end(); i++)
    {
      const std::string str = boost::lexical_cast<std::string>(file_path(*i));
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
changes_summary::print(std::ostream & os, size_t max_cols) const
{

  if (! cs.nodes_deleted.empty())
    {
      os << "Deleted entries:" << "\n";
      print_indented_set(os, cs.nodes_deleted, max_cols);
    }
  
  if (! cs.nodes_renamed.empty())
    {
      os << "Renamed entries:" << "\n";
      for (std::map<split_path, split_path>::const_iterator
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); i++)
        os << "        " << file_path(i->first) << " to " << file_path(i->second) << "\n";
    }

  if (! cs.files_added.empty())
    {
      path_set tmp;
      for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
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
      for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
           i != cs.deltas_applied.end(); ++i)
        tmp.insert(i->first);
      os << "Modified files:" << "\n";
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.attrs_set.empty() || ! cs.attrs_cleared.empty())
    {
      path_set tmp;
      for (std::set<std::pair<split_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
           i != cs.attrs_cleared.end(); ++i)
        tmp.insert(i->first);

      for (std::map<std::pair<split_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
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
  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
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
           diff_type type,
           set<split_path> restrict_paths = set<split_path>())
{
  // 60 is somewhat arbitrary, but less than 80
  std::string patch_sep = std::string(60, '=');

  for (std::map<split_path, file_id>::const_iterator 
         i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      if (!restrict_paths.empty() 
          && restrict_paths.find(i->first) == restrict_paths.end())
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
      
      if (guess_binary(unpacked()))
        cout << "# " << file_path(i->first) << " is binary\n";
      else
        {     
          split_into_lines(unpacked(), lines);
          if (! lines.empty())
            {
              cout << (boost::format("--- %s\t%s\n") % file_path(i->first) % i->second)
                   << (boost::format("+++ %s\t%s\n") % file_path(i->first) % i->second)
                   << (boost::format("@@ -0,0 +1,%d @@\n") % lines.size());
              for (vector<string>::const_iterator j = lines.begin();
                   j != lines.end(); ++j)
                {
                  cout << "+" << *j << "\n";
                }
            }
        }
    }

  std::map<split_path, split_path> reverse_rename_map;

  for (std::map<split_path, split_path>::const_iterator 
         i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      reverse_rename_map.insert(std::make_pair(i->second, i->first));
    }

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      if (!restrict_paths.empty() 
          && restrict_paths.find(i->first) == restrict_paths.end())
        continue;

      file_data f_old;
      data data_old, data_new;
      vector<string> old_lines, new_lines;

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
      
      if (guess_binary(data_new()) || 
          guess_binary(data_old()))
        cout << "# " << file_path(delta_entry_path(i)) << " is binary\n";
      else
        {
          split_into_lines(data_old(), old_lines);
          split_into_lines(data_new(), new_lines);

          split_path dst_path = delta_entry_path(i);
          split_path src_path = dst_path;

          std::map<split_path, split_path>::const_iterator re;
          re = reverse_rename_map.find(dst_path);

          if (re != reverse_rename_map.end())
            src_path = re->second;

          make_diff(file_path(src_path).as_internal(), 
                    file_path(dst_path).as_internal(), 
                    delta_entry_src(i),
                    delta_entry_dst(i),
                    old_lines, new_lines,
                    cout, type);
        }
    }
}

CMD(diff, N_("informative"), N_("[PATH]..."), 
    N_("show current diffs on stdout.\n"
    "If one revision is given, the diff between the workspace and\n"
    "that revision is shown.  If two revisions are given, the diff between\n"
    "them is given.  If no format is specified, unified is used by default."),
    OPT_REVISION % OPT_DEPTH % OPT_EXCLUDE %
    OPT_UNIFIED_DIFF % OPT_CONTEXT_DIFF % OPT_EXTERNAL_DIFF %
    OPT_EXTERNAL_DIFF_ARGS)
{
  revision_set r_old, r_new;
  roster_t new_roster, old_roster;
  bool new_is_archived;
  diff_type type = app.diff_format;
  ostringstream header;
  temp_node_id_source nis;

  if (app.diff_args_provided)
    N(app.diff_format == external_diff,
      F("--diff-args requires --external\n"
        "try adding --external or removing --diff-args?"));

  cset composite;
  cset excluded;

  // initialize before transaction so we have a database to work with

  if (app.revision_selectors.size() == 0)
    app.require_workspace();
  else if (app.revision_selectors.size() == 1)
    app.require_workspace();

  if (app.revision_selectors.size() == 0)
    {
      get_working_revision_and_rosters(app, args, r_new,
                                       old_roster, 
                                       new_roster,
                                       excluded,
                                       nis);

      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      if (r_new.edges.size() == 1)
        composite = edge_changes(r_new.edges.begin());
      new_is_archived = false;
      revision_id old_rid;
      get_revision_id(old_rid);
      header << "# old_revision [" << old_rid << "]" << "\n";
    }
  else if (app.revision_selectors.size() == 1)
    {
      revision_id r_old_id;
      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);
      get_working_revision_and_rosters(app, args, r_new,
                                       old_roster, 
                                       new_roster,
                                       excluded,
                                       nis);
      // Clobber old_roster with the one specified
      app.db.get_revision(r_old_id, r_old);
      app.db.get_roster(r_old_id, old_roster);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      N(r_new.edges.size() == 1, F("current revision has no ancestor"));
      new_is_archived = false;
      header << "# old_revision [" << r_old_id << "]" << "\n";
      {
        // Calculate a cset from old->new, then re-restrict it (using the
        // one from get_working_revision_and_rosters doesn't work here,
        // since it only restricts the edge base->new, and there might be
        // changes outside the restriction in old->base)
        cset tmp1, tmp2;
        make_cset (old_roster, new_roster, tmp1);
        calculate_restricted_cset (app, args, tmp1, composite, tmp2);
      }
    }
  else if (app.revision_selectors.size() == 2)
    {
      revision_id r_old_id, r_new_id;
      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      complete(app, idx(app.revision_selectors, 1)(), r_new_id);
      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);
      app.db.get_revision(r_old_id, r_old);
      N(app.db.revision_exists(r_new_id),
        F("no such revision '%s'") % r_new_id);
      app.db.get_revision(r_new_id, r_new);
      app.db.get_roster(r_old_id, old_roster);
      app.db.get_roster(r_new_id, new_roster);
      new_is_archived = true;
      {
        // Calculate a cset from old->new, then re-restrict it. 
        // FIXME: this is *possibly* a UI bug, insofar as we
        // look at the restriction name(s) you provided on the command
        // line in the context of new and old, *not* the workspace.
        // One way of "fixing" this is to map the filenames on the command
        // line to node_ids, and then restrict based on those. This 
        // might be more intuitive; on the other hand it would make it
        // impossible to restrict to paths which are dead in the working
        // copy but live between old and new. So ... no rush to "fix" it;
        // discuss implications first.
        cset tmp1, tmp2;
        make_cset (old_roster, new_roster, tmp1);
        calculate_restricted_cset (app, args, tmp1, composite, tmp2);
      }
    }
  else
    {
      throw usage(name);
    }

  
  data summary;
  write_cset(composite, summary);

  vector<string> lines;
  split_into_lines(summary(), lines);
  cout << "# " << "\n";
  if (summary().size() > 0) 
    {
      cout << header.str() << "# " << "\n";
      for (vector<string>::iterator i = lines.begin(); i != lines.end(); ++i)
        cout << "# " << *i << "\n";
    }
  else
    {
      cout << "# " << _("no changes") << "\n";
    }
  cout << "# " << "\n";

  if (type == external_diff) {
    do_external_diff(composite, app, new_is_archived);
  } else
    dump_diffs(composite, app, new_is_archived, type);
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
log_certs(app_state & app, revision_id id, cert_name name, string label, bool multiline)
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
    OPT_LAST % OPT_NEXT % OPT_REVISION % OPT_BRIEF % OPT_DIFFS % OPT_NO_MERGES %
    OPT_NO_FILES)
{
  if (app.revision_selectors.size() == 0)
    app.require_workspace("try passing a --revision to start at");

  temp_node_id_source nis;

  set<node_id> nodes;
  
  set<revision_id> frontier;

  revision_id first_rid;
  if (app.revision_selectors.size() == 0)
    {
      get_revision_id(first_rid);
      frontier.insert(first_rid);
    }
  else
    {
      for (std::vector<utf8>::const_iterator i = app.revision_selectors.begin();
           i != app.revision_selectors.end(); i++) 
        {
          set<revision_id> rids;
          complete(app, (*i)(), rids);
          frontier.insert(rids.begin(), rids.end());
          if (i == app.revision_selectors.begin())
            first_rid = *rids.begin();
        }
    }

  if (args.size() > 0)
    {
      // User wants to trace only specific files
      roster_t old_roster, new_roster;
      revision_set rev;

      if (app.revision_selectors.size() == 0)
        get_unrestricted_working_revision_and_rosters(app, rev, old_roster, new_roster, nis);
      else
        app.db.get_roster(first_rid, new_roster);          

      deque<node_t> todo;
      for (size_t i = 0; i < args.size(); ++i)
        {
          file_path fp = file_path_external(idx(args, i));
          split_path sp;
          fp.split(sp);
          N(new_roster.has_node(sp),
            F("Unknown file '%s' for log command") % fp);
          todo.push_back(new_roster.get_node(sp));
        }
      while (!todo.empty())
        {
          node_t n = todo.front();
          todo.pop_front();
          nodes.insert(n->self);
          if (is_dir_t(n))
            {
              dir_t d = downcast_to_dir_t(n);
              for (dir_map::const_iterator i = d->children.begin();
                   i != d->children.end(); ++i)
                {
                  todo.push_front(i->second);
                }
            }
        }
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

  revision_set rev;
  while(! frontier.empty() && (last == -1 || last > 0) && (next == -1 || next > 0))
    {
      set<revision_id> next_frontier;
      
      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        { 
          revision_id rid = *i;

          bool print_this = nodes.empty();
          set<  revision<id> > parents;
          vector< revision<cert> > tmp;

          if (!app.db.revision_exists(rid))
            {
              L(FL("revision %s does not exist in db, skipping\n") % rid);
              continue;
            }

          if (seen.find(rid) != seen.end())
            continue;

          seen.insert(rid);
          app.db.get_revision(rid, rev);

          set<node_id> next_nodes;

          if (!nodes.empty())
            {
              set<node_id> nodes_changed;
              set<node_id> nodes_born;
              bool any_node_hit = false;
              select_nodes_modified_by_rev(rid, rev, 
                                           nodes_changed, 
                                           nodes_born,
                                           app);
              for (set<node_id>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
                {
                  if (nodes_changed.find(*n) != nodes_changed.end()
                      || nodes_born.find(*n) != nodes_born.end())
                    {
                      any_node_hit = true;
                      break;
                    }
                }

              next_nodes = nodes;
              for (set<node_id>::const_iterator n = nodes_born.begin(); n != nodes_born.end();
                   ++n)
                next_nodes.erase(*n);

              if (any_node_hit)
                print_this = true;
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
            if (global_sanity.brief)
              {
                cout << rid;
                log_certs(app, rid, author_name);
                log_certs(app, rid, date_name);
                log_certs(app, rid, branch_name);
                cout << "\n";
              }
            else
              {
                cout << "-----------------------------------------------------------------"
                     << "\n";
                cout << "Revision: " << rid << "\n";

                changes_summary csum;

                set<revision_id> ancestors;

                for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
                  {
                    ancestors.insert(edge_old_revision(e));
                    csum.add_change_set(edge_changes(e));
                  }

                for (set<revision_id>::const_iterator anc = ancestors.begin();
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
                    // limit to selected nodes
                    set<split_path> node_names;
                    if (!nodes.empty())
                      {
                        roster_t ros;
                        app.db.get_roster(rid, ros);

                        for (set<node_id>::const_iterator n = nodes.begin();
                             n != nodes.end(); n++)
                          {
                            split_path sp;
                            ros.get_name(*n, sp);
                            node_names.insert(sp);
                          }
                      }
                    dump_diffs(edge_changes(e), app, true, unified_diff,
                               node_names);
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

          }
        // when we had a restriction and run out of nodes, stop.
        if (!nodes.empty() && next_nodes.empty())
          return;

        nodes = next_nodes;
        }
      frontier = next_frontier;
    }
}

