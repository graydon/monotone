// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "merge.hh"
#include "roster_merge.hh"

void
interactive_merge_and_store(revision_id const & left_rid,
                            revision_id const & right_rid,
                            revision_id & merged_rid,
                            app_state & app)
{
  roster_t left_roster, right_roster;
  marking_map left_marking_map, right_marking_map;
  std::set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;

  app.db.get_roster(left_rid, left_roster, left_marking_map);
  app.db.get_roster(right_rid, right_roster, right_marking_map);
  app.db.get_uncommon_ancestors(left_rid, right_rid,
                                left_uncommon_ancestors, right_uncommon_ancestors);

  roster_merge_result result;

  roster_merge(left_roster, left_marking_map, left_uncommon_ancestors,
               right_roster, right_marking_map, right_uncommon_ancestors,
               result);

  roster_t & merged_roster = result.roster;

  // FIXME_ROSTERS: add code here to resolve conflicts
  // write new files into the db

  I(result.is_clean());
  I(check_sane(merged_roster));

  revision_set merged_rev;
  
  calculate_ident(merged_roster, merged_rev.new_manifest);
  
  manifest_id left_mid;
  calculate_ident(left_roster, left_mid);
  cset left_to_merged;
  make_cset(left_roster, merged_roster, left_to_merged);
  safe_insert(merged_rev.edges, std::make_pair(left_rid,
                                               std::make_pair(left_mid,
                                                              left_to_merged)));
  
  manifest_id right_mid;
  calculate_ident(right_roster, right_mid);
  cset right_to_merged;
  make_cset(right_roster, merged_roster, right_to_merged);
  safe_insert(merged_rev.edges, std::make_pair(right_rid,
                                               std::make_pair(right_mid,
                                                              right_to_merged)));
  
  revision_data merged_data;
  write_revision_set(merged_rev, merged_data);
  calculate_ident(merged_data, merged_rid);
  {
    transaction_guard guard(app.db);
  
    app.db.put_revision(merged_rid, merged_rev);
    if (app.date_set)
      cert_revision_date_time(merged_id, app.date, app, dbw);
    else
      cert_revision_date_now(merged_id, app, dbw);
    if (app.author().length() > 0)
      cert_revision_author(merged_id, app.author(), app, dbw);
    else
      cert_revision_author_default(merged_id, app, dbw);

    guard.commit();
  }
}
