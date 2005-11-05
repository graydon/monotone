// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include <boost/shared_ptr.hpp>

#include "diff_patch.hh"
#include "merge.hh"
#include "packet.hh"
#include "revision.hh"
#include "roster_merge.hh"
#include "safe_map.hh"
#include "transforms.hh"

using std::map;
using std::make_pair;
using boost::shared_ptr;

static void
load_and_cache_roster(revision_id const & rid,
		      map<revision_id, shared_ptr<roster_t> > & rmap,
		      shared_ptr<roster_t> & rout,
		      app_state & app)
{
  map<revision_id, shared_ptr<roster_t> >::const_iterator i = rmap.find(rid);
  if (i != rmap.end())
    rout = i->second;
  else
    { 
      rout = shared_ptr<roster_t>(new roster_t());
      marking_map mm;
      app.db.get_roster(rid, *rout, mm);
      safe_insert(rmap, make_pair(rid, rout));
    }
}

static void
get_file_details(roster_t const & ros, node_id nid,
		 file_id & fid,
		 file_path & pth)
{
  I(ros.has_node(nid));
  file_t f = downcast_to_file_t(ros.get_node(nid));
  fid = f->content;
  split_path sp;
  ros.get_name(nid, sp);
  pth = file_path(sp);
}

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

  // FIXME_ROSTERS: we only have code (below) to invoke the
  // line-merger on content conflicts. Other classes of conflict will
  // cause an invariant to trip below.  Probably just a bunch of lua
  // hooks for remaining conflict types will be ok.

  if (!result.is_clean())
    {
      L(F("unclean mark-merge: %d name conflicts, %d content conflicts, %d attr conflicts\n")	
	% result.node_name_conflicts.size()
	% result.file_content_conflicts.size()
	% result.node_attr_conflicts.size());

      for (size_t i = 0; i < result.node_name_conflicts.size(); ++i)
	L(F("name conflict on node %d: [parent %d, self %s] vs. [parent %d, self %s]\n") 
	  % result.node_name_conflicts[i].nid 
	  % result.node_name_conflicts[i].left.first 
	  % result.node_name_conflicts[i].left.second
	  % result.node_name_conflicts[i].right.first 
	  % result.node_name_conflicts[i].right.second);

      for (size_t i = 0; i < result.file_content_conflicts.size(); ++i)
	L(F("content conflict on node %d: [%s] vs. [%s]\n") 
	  % result.file_content_conflicts[i].nid
	  % result.file_content_conflicts[i].left
	  % result.file_content_conflicts[i].right);

      for (size_t i = 0; i < result.node_attr_conflicts.size(); ++i)
	L(F("attribute conflict on node %d, key %s: [%d, %s] vs. [%d, %s]\n") 
	  % result.node_attr_conflicts[i].nid
	  % result.node_attr_conflicts[i].key
	  % result.node_attr_conflicts[i].left.first
	  % result.node_attr_conflicts[i].left.second
	  % result.node_attr_conflicts[i].right.first
	  % result.node_attr_conflicts[i].right.second);

      // Attempt to auto-resolve any content conflicts using the line-merger.
      // To do this requires finding a merge ancestor.
      if (!result.file_content_conflicts.empty())
	{

	  L(F("examining content conflicts\n"));
	  std::vector<file_content_conflict> residual_conflicts;

	  revision_id lca;
	  map<revision_id, shared_ptr<roster_t> > lca_rosters;
	  find_common_ancestor_for_merge(left_rid, right_rid, lca, app);

	  for (size_t i = 0; i < result.file_content_conflicts.size(); ++i)
	    {
	      file_content_conflict const & conflict = result.file_content_conflicts[i];

	      // For each file, if the lca is nonzero and its roster
	      // contains the file, then we use its roster.  Otherwise
	      // we use the roster at the file's birth revision, which
	      // is the "per-file worst case" lca.
	      shared_ptr<roster_t> roster_for_file_lca;

	      // Begin by loading any non-empty file lca roster
	      if (!lca.inner()().empty())
		load_and_cache_roster(lca, lca_rosters, roster_for_file_lca, app);

	      // If this roster doesn't contain the file, replace it with 
	      // the file's birth roster.
	      if (!roster_for_file_lca->has_node(conflict.nid))
		{
		  marking_map::const_iterator j = left_marking_map.find(conflict.nid);
		  I(j != left_marking_map.end());
		  load_and_cache_roster(j->second.birth_revision, lca_rosters, 
					roster_for_file_lca, app);
		}

	      // Now we should certainly have the node.
	      I(roster_for_file_lca->has_node(conflict.nid));

	      file_id anc_id, left_id, right_id;
	      file_path anc_path, left_path, right_path;
	      get_file_details (*roster_for_file_lca, conflict.nid, anc_id, anc_path);
	      get_file_details (left_roster, conflict.nid, left_id, left_path);
	      get_file_details (right_roster, conflict.nid, right_id, right_path);
	      
	      file_id merged_id;
	      
	      merge_provider mp(app, *roster_for_file_lca, left_roster, right_roster);
	      if (mp.try_to_merge_files(anc_path, left_path, right_path, right_path,
					anc_id, left_id, right_id, merged_id))
		{
		  L(F("resolved content conflict %d / %d\n") 
		    % (i+1) % result.file_content_conflicts.size());
		}
	      else
		residual_conflicts.push_back(conflict);
	    }
	  result.file_content_conflicts = residual_conflicts;	  
	}	 
    }


  // write new files into the db

  I(result.is_clean());
  merged_roster.check_sane();

  revision_set merged_rev;
  
  calculate_ident(merged_roster, merged_rev.new_manifest);
  
  manifest_id left_mid;
  calculate_ident(left_roster, left_mid);
  boost::shared_ptr<cset> left_to_merged(new cset);
  make_cset(left_roster, merged_roster, *left_to_merged);
  safe_insert(merged_rev.edges, std::make_pair(left_rid,
                                               std::make_pair(left_mid,
                                                              left_to_merged)));
  
  manifest_id right_mid;
  calculate_ident(right_roster, right_mid);
  boost::shared_ptr<cset> right_to_merged(new cset);
  make_cset(right_roster, merged_roster, *right_to_merged);
  safe_insert(merged_rev.edges, std::make_pair(right_rid,
                                               std::make_pair(right_mid,
                                                              right_to_merged)));
  
  revision_data merged_data;
  write_revision_set(merged_rev, merged_data);
  calculate_ident(merged_data, merged_rid);
  {
    transaction_guard guard(app.db);
  
    app.db.put_revision(merged_rid, merged_rev);
    packet_db_writer dbw(app);
    if (app.date_set)
      cert_revision_date_time(merged_rid, app.date, app, dbw);
    else
      cert_revision_date_now(merged_rid, app, dbw);
    if (app.author().length() > 0)
      cert_revision_author(merged_rid, app.author(), app, dbw);
    else
      cert_revision_author_default(merged_rid, app, dbw);

    guard.commit();
  }
}
