// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <set>

#include <boost/shared_ptr.hpp>

#include "diff_patch.hh"
#include "merge.hh"
#include "revision.hh"
#include "roster_merge.hh"
#include "safe_map.hh"
#include "transforms.hh"
#include "app_state.hh"

using std::make_pair;
using std::map;
using std::set;
using std::vector;

using boost::shared_ptr;

static void
get_file_details(roster_t const & ros, node_id nid,
                 file_id & fid,
                 file_path & pth)
{
  I(ros.has_node(nid));
  file_t f = downcast_to_file_t(ros.get_node(nid));
  fid = f->content;
  ros.get_name(nid, pth);
}

void
resolve_merge_conflicts(roster_t const & left_roster,
                        roster_t const & right_roster,
                        roster_merge_result & result,
                        content_merge_adaptor & adaptor,
                        app_state & app)
{
  // FIXME_ROSTERS: we only have code (below) to invoke the
  // line-merger on content conflicts. Other classes of conflict will
  // cause an invariant to trip below.  Probably just a bunch of lua
  // hooks for remaining conflict types will be ok.

  if (!result.is_clean())
    result.log_conflicts();


  if (result.has_non_content_conflicts())
    {
      result.report_missing_root_conflicts(left_roster, right_roster, adaptor);
      result.report_invalid_name_conflicts(left_roster, right_roster, adaptor);
      result.report_directory_loop_conflicts(left_roster, right_roster, adaptor);

      result.report_orphaned_node_conflicts(left_roster, right_roster, adaptor);
      result.report_multiple_name_conflicts(left_roster, right_roster, adaptor);
      result.report_duplicate_name_conflicts(left_roster, right_roster, adaptor);

      result.report_attribute_conflicts(left_roster, right_roster, adaptor);
      result.report_file_content_conflicts(left_roster, right_roster, adaptor);
    }
  else if (result.has_content_conflicts())
    {
      // Attempt to auto-resolve any content conflicts using the line-merger.
      // To do this requires finding a merge ancestor.

      L(FL("examining content conflicts"));

      // FIXME: need this for the tests to pass but it doesn't really make sense
      // because the auto merger may resolve all of the content conflicts

      result.report_file_content_conflicts(left_roster, right_roster, adaptor);

      // TODO: split this into two merge passes
      // first make an automatic merge pass over all content conflicts
      // then list all remaining conflicts before making a second
      // manual merge pass over the remaining conflicts

      size_t cnt;
      size_t total_conflicts = result.file_content_conflicts.size();
      std::vector<file_content_conflict>::iterator it;

      for (cnt = 1, it = result.file_content_conflicts.begin();
           it != result.file_content_conflicts.end(); ++cnt)
        {
          file_content_conflict const & conflict = *it;

          MM(conflict);

          revision_id rid;
          shared_ptr<roster_t const> roster_for_file_lca;
          adaptor.get_ancestral_roster(conflict.nid, rid, roster_for_file_lca);

          // Now we should certainly have a roster, which has the node.
          I(roster_for_file_lca);
          I(roster_for_file_lca->has_node(conflict.nid));

          file_id anc_id, left_id, right_id;
          file_path anc_path, left_path, right_path;
          get_file_details(*roster_for_file_lca, conflict.nid, anc_id, anc_path);
          get_file_details(left_roster, conflict.nid, left_id, left_path);
          get_file_details(right_roster, conflict.nid, right_id, right_path);

          file_id merged_id;

          content_merger cm(app, *roster_for_file_lca,
                            left_roster, right_roster,
                            adaptor);

          if (cm.try_to_merge_files(anc_path, left_path, right_path, right_path,
                                    anc_id, left_id, right_id, merged_id))
            {
              L(FL("resolved content conflict %d / %d")
                % cnt % total_conflicts);
              file_t f = downcast_to_file_t(result.roster.get_node(conflict.nid));
              f->content = merged_id;

              it = result.file_content_conflicts.erase(it);
            }
          else
            {
              ++it;

              // If the content_merger has failed, there's no point
              // trying to continue--we'll only frustrate users by
              // encouraging them to continue working with their merge
              // tool on a merge that is now destined to fail.
              break;
            }
        }
    }

  E(result.is_clean(),
    F("merge failed due to unresolved conflicts"));
}

void
interactive_merge_and_store(revision_id const & left_rid,
                            revision_id const & right_rid,
                            revision_id & merged_rid,
                            app_state & app)
{
  roster_t left_roster, right_roster;
  marking_map left_marking_map, right_marking_map;
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;

  app.db.get_roster(left_rid, left_roster, left_marking_map);
  app.db.get_roster(right_rid, right_roster, right_marking_map);
  app.db.get_uncommon_ancestors(left_rid, right_rid,
                                left_uncommon_ancestors, right_uncommon_ancestors);

  roster_merge_result result;

//   {
//     data tmp;
//     write_roster_and_marking(left_roster, left_marking_map, tmp);
//     P(F("merge left roster: [[[\n%s\n]]]") % tmp);
//     write_roster_and_marking(right_roster, right_marking_map, tmp);
//     P(F("merge right roster: [[[\n%s\n]]]") % tmp);
//   }

  roster_merge(left_roster, left_marking_map, left_uncommon_ancestors,
               right_roster, right_marking_map, right_uncommon_ancestors,
               result);

  content_merge_database_adaptor dba(app, left_rid, right_rid,
                                     left_marking_map, right_marking_map);
  resolve_merge_conflicts(left_roster, right_roster,
                          result, dba, app);

  // write new files into the db
  store_roster_merge_result(left_roster, right_roster, result,
                            left_rid, right_rid, merged_rid,
                            app);
}

void
store_roster_merge_result(roster_t const & left_roster,
                          roster_t const & right_roster,
                          roster_merge_result & result,
                          revision_id const & left_rid,
                          revision_id const & right_rid,
                          revision_id & merged_rid,
                          app_state & app)
{
  I(result.is_clean());
  roster_t & merged_roster = result.roster;
  merged_roster.check_sane();

  revision_t merged_rev;
  merged_rev.made_for = made_for_database;

  calculate_ident(merged_roster, merged_rev.new_manifest);

  shared_ptr<cset> left_to_merged(new cset);
  make_cset(left_roster, merged_roster, *left_to_merged);
  safe_insert(merged_rev.edges, make_pair(left_rid, left_to_merged));

  shared_ptr<cset> right_to_merged(new cset);
  make_cset(right_roster, merged_roster, *right_to_merged);
  safe_insert(merged_rev.edges, make_pair(right_rid, right_to_merged));

  revision_data merged_data;
  write_revision(merged_rev, merged_data);
  calculate_ident(merged_data, merged_rid);
  {
    transaction_guard guard(app.db);

    app.db.put_revision(merged_rid, merged_rev);

    guard.commit();
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
