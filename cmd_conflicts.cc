// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>

#include "app_state.hh"
#include "cmd.hh"
#include "database.hh"
#include "roster_merge.hh"

CMD_GROUP(conflicts, "conflicts", "", CMD_REF(tree),
          N_("Commands for conflict resolutions"),
          "");

struct conflicts_t
{
  roster_merge_result result;
  revision_id ancestor_rid, left_rid, right_rid;
  boost::shared_ptr<roster_t> ancestor_roster;
  boost::shared_ptr<roster_t> left_roster;
  boost::shared_ptr<roster_t> right_roster;
  marking_map left_marking, right_marking;

  conflicts_t(database & db, bookkeeping_path const & file):
    left_roster(boost::shared_ptr<roster_t>(new roster_t())),
    right_roster(boost::shared_ptr<roster_t>(new roster_t()))
  {
    result.clear(); // default constructor doesn't do this.

    result.read_conflict_file(db, file, ancestor_rid, left_rid, right_rid,
                              *left_roster, left_marking,
                              *right_roster, right_marking);
  };

  void write (database & db, lua_hooks & lua, bookkeeping_path const & file)
    {
      result.write_conflict_file
        (db, lua, file, ancestor_rid, left_rid, right_rid,
         left_roster, left_marking, right_roster, right_marking);
    };
};

typedef enum {first, remaining} show_conflicts_case_t;

static void
show_conflicts(database & db, conflicts_t conflicts, show_conflicts_case_t show_case)
{
  // Go thru the conflicts we know how to resolve in the same order
  // merge.cc resolve_merge_conflicts outputs them.
  for (std::vector<duplicate_name_conflict>::iterator i = conflicts.result.duplicate_name_conflicts.begin();
       i != conflicts.result.duplicate_name_conflicts.end();
       ++i)
    {
      duplicate_name_conflict & conflict = *i;

      if (conflict.left_resolution.first == resolve_conflicts::none ||
          conflict.right_resolution.first == resolve_conflicts::none)
        {
          file_path left_name;
          conflicts.left_roster->get_name(conflict.left_nid, left_name);
          P(F("duplicate_name %s") % left_name);

          switch (show_case)
            {
            case first:
              P(F("possible resolutions:"));

              if (conflict.left_resolution.first == resolve_conflicts::none)
                {
                  P(F("resolve_first_left drop"));
                  P(F("resolve_first_left rename \"name\""));
                  P(F("resolve_first_left user \"name\""));
                }

              if (conflict.right_resolution.first == resolve_conflicts::none)
                {
                  P(F("resolve_first_right drop"));
                  P(F("resolve_first_right rename \"name\""));
                  P(F("resolve_first_right user \"name\""));
                }
              return;

            case remaining:
              break;
            }
        }
    }

  for (std::vector<file_content_conflict>::iterator i = conflicts.result.file_content_conflicts.begin();
       i != conflicts.result.file_content_conflicts.end();
       ++i)
    {
      file_content_conflict & conflict = *i;

      if (conflict.resolution.first == resolve_conflicts::none)
        {
          file_path name;
          conflicts.left_roster->get_name(conflict.nid, name);
          P(F("content %s") % name);

          switch (show_case)
            {
            case first:
              P(F("possible resolutions:"));
              P(F("resolve_first interactive \"file_name\""));
              P(F("resolve_first user \"file_name\""));
              return;

            case remaining:
              break;
            }
        }
    }

  switch (show_case)
    {
    case first:
      {
        int const count = conflicts.result.count_unsupported_resolution();
        if (count > 0)
            P(FP("warning: %d conflict with no supported resolutions.",
                 "warning: %d conflicts with no supported resolutions.",
                 count) % count);
        else
          P(F("all conflicts resolved"));
      }
      break;

    case remaining:
      {
        int const count = conflicts.result.count_unsupported_resolution();
        if (count > 0)
          {
            P(FP("warning: %d conflict with no supported resolutions.",
                 "warning: %d conflicts with no supported resolutions.",
                 count) % count);

            content_merge_database_adaptor adaptor
              (db, conflicts.left_rid, conflicts.right_rid, conflicts.left_marking, conflicts.right_marking);

            conflicts.result.report_missing_root_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_invalid_name_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_directory_loop_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_orphaned_node_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_multiple_name_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
            conflicts.result.report_attribute_conflicts
              (*conflicts.left_roster, *conflicts.right_roster, adaptor, false, std::cout);
          }
      }
      break;
    }

} // show_conflicts

enum side_t {left, right, neither};
static char const * const conflict_resolution_not_supported_msg = "%s is not a supported conflict resolution for %s";

// Call Lua merge3 hook to merge left_fid, right_fid, store result in result_path
static bool
do_interactive_merge(database & db,
                     lua_hooks & lua,
                     conflicts_t & conflicts,
                     node_id const nid,
                     file_id const & ancestor_fid,
                     file_id const & left_fid,
                     file_id const & right_fid,
                     bookkeeping_path const & result_path)
{
  file_path ancestor_path, left_path, right_path;

  if (!conflicts.ancestor_roster)
    {
      conflicts.ancestor_roster = boost::shared_ptr<roster_t>(new roster_t());
      db.get_roster(conflicts.ancestor_rid, *conflicts.ancestor_roster);
    }

  conflicts.ancestor_roster->get_name(nid, ancestor_path);
  conflicts.left_roster->get_name(nid, left_path);
  conflicts.right_roster->get_name(nid, right_path);

  file_data left_data, right_data, ancestor_data;
  data merged_unpacked;

  db.get_file_version(left_fid, left_data);
  db.get_file_version(ancestor_fid, ancestor_data);
  db.get_file_version(right_fid, right_data);

  if (lua.hook_merge3(ancestor_path, left_path, right_path, file_path(),
                      ancestor_data.inner(), left_data.inner(),
                      right_data.inner(), merged_unpacked))
    {
      write_data(result_path, merged_unpacked);
      return true;
    }

  return false;
} // do_interactive_merge

static void
set_duplicate_name_conflict(resolve_conflicts::file_resolution_t & resolution,
                            resolve_conflicts::file_resolution_t const & other_resolution,
                            args_vector const & args)
{
  if ("drop" == idx(args, 0)())
    {
      N(args.size() == 1, F("too many arguments"));
      resolution.first = resolve_conflicts::drop;
    }
  else if ("rename" == idx(args, 0)())
    {
      N(args.size() == 2, F("wrong number of arguments"));
      resolution.first  = resolve_conflicts::rename;
      resolution.second = resolve_conflicts::new_file_path(idx(args,1)());
    }
  else if ("user" == idx(args, 0)())
    {
      N(args.size() == 2, F("wrong number of arguments"));
      N(other_resolution.first != resolve_conflicts::content_user,
        F("left and right resolutions cannot both be 'user'"));

      resolution.first  = resolve_conflicts::content_user;
      resolution.second = new_optimal_path(idx(args,1)(), false);
    }
  else
    N(false, F(conflict_resolution_not_supported_msg) % idx(args,0) % "duplicate_name");

} //set_duplicate_name_conflict

static void
set_first_conflict(database & db,
                   lua_hooks & lua,
                   conflicts_t & conflicts,
                   args_vector const & args,
                   side_t side)
{
  if (side != neither)
    {
      for (std::vector<duplicate_name_conflict>::iterator i = conflicts.result.duplicate_name_conflicts.begin();
           i != conflicts.result.duplicate_name_conflicts.end();
           ++i)
        {
          duplicate_name_conflict & conflict = *i;

          switch (side)
            {
            case left:
              if (conflict.left_resolution.first == resolve_conflicts::none)
                {
                  set_duplicate_name_conflict(conflict.left_resolution, conflict.right_resolution, args);
                  return;
                }
              break;

            case right:
              if (conflict.right_resolution.first == resolve_conflicts::none)
                {
                  set_duplicate_name_conflict(conflict.right_resolution, conflict.left_resolution, args);
                  return;
                }
              break;

            case neither:
              I(false);
            }
        }
    }

  if (side == neither)
    {
      for (std::vector<file_content_conflict>::iterator i = conflicts.result.file_content_conflicts.begin();
           i != conflicts.result.file_content_conflicts.end();
           ++i)
        {
          file_content_conflict & conflict = *i;

          if (conflict.resolution.first == resolve_conflicts::none)
            {
              if ("interactive" == idx(args,0)())
                {
                  N(args.size() == 2, F("wrong number of arguments"));
                  N(bookkeeping_path::external_string_is_bookkeeping_path(utf8(idx(args,1)())),
                    F("result path must be under _MTN"));
                  bookkeeping_path const result_path(idx(args,1)());

                  if (do_interactive_merge(db, lua, conflicts, conflict.nid,
                                           conflict.ancestor, conflict.left, conflict.right, result_path))
                    {
                      conflict.resolution.first  = resolve_conflicts::content_user;
                      conflict.resolution.second = boost::shared_ptr<any_path>(new bookkeeping_path(result_path));
                    }
                }
              else if ("user" == idx(args,0)())
                {
                  N(args.size() == 2, F("wrong number of arguments"));

                  conflict.resolution.first  = resolve_conflicts::content_user;
                  conflict.resolution.second = new_optimal_path(idx(args,1)(), false);
                }
              else
                {
                  // We don't allow the user to specify 'resolved_internal'; that
                  // is only done by automate show_conflicts.
                  N(false, F(conflict_resolution_not_supported_msg) % idx(args,0) % "file_content");
                }
              return;
            }
        }
    }

  switch (side)
    {
    case left:
      N(false, F("no resolvable yet unresolved left side conflicts"));
      break;

    case right:
      N(false, F("no resolvable yet unresolved right side conflicts"));
      break;

    case neither:
      N(false, F("no resolvable yet unresolved single-file conflicts"));
      break;
    }

} // set_first_conflict


/// commands

// CMD(store) is in cmd_merging.cc, since it needs access to
// show_conflicts_core, and doesn't need conflicts_t.

CMD(show_first, "show_first", "", CMD_REF(conflicts),
    "",
    N_("Show the first unresolved conflict in the conflicts file, and possible resolutions"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  N(args.size() == 0, F("wrong number of arguments"));
  show_conflicts(db, conflicts, first);
}

CMD(show_remaining, "show_remaining", "", CMD_REF(conflicts),
    "",
    N_("Show the remaining unresolved conflicts in the conflicts file"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  N(args.size() == 0, F("wrong number of arguments"));
  show_conflicts(db, conflicts, remaining);
}

CMD(resolve_first, "resolve_first", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the resolution for the first unresolved single-file conflict"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, neither);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_left, "resolve_first_left", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the left resolution for the first unresolved two-file conflict"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, left);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_right, "resolve_first_right", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the right resolution for the first unresolved two-file conflict"),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(db, app.lua, conflicts, args, right);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(clean, "clean", "", CMD_REF(conflicts),
    N_(""),
    N_("Delete any bookkeeping files related to conflict resolution"),
    "",
    options::opts::none)
{
  bookkeeping_path conflicts_file("_MTN/conflicts");
  bookkeeping_path resolutions_dir("_MTN/resolutions");

  if (path_exists(conflicts_file))
    delete_file(conflicts_file);

  if (path_exists(resolutions_dir))
    delete_dir_recursive(resolutions_dir);
}

// end of file
