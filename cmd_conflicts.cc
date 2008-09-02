// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include "app_state.hh"
#include "cmd.hh"
#include "database.hh"
#include "roster_merge.hh"

CMD_GROUP(conflicts, "conflicts", "", CMD_REF(tree),
          N_("Commands for conflict resolutions."),
          "");

struct conflicts_t
{
  roster_merge_result result;
  revision_id ancestor_rid, left_rid, right_rid;
  boost::shared_ptr<roster_t> left_roster;
  boost::shared_ptr<roster_t> right_roster;
  marking_map left_marking, right_marking;

  conflicts_t(database & db, system_path file):
    left_roster(boost::shared_ptr<roster_t>(new roster_t())),
    right_roster(boost::shared_ptr<roster_t>(new roster_t()))
  {
    result.clear(); // default constructor doesn't do this.

    result.read_conflict_file(db, file, ancestor_rid, left_rid, right_rid,
                              *left_roster, left_marking,
                              *right_roster, right_marking);
  };

  void write (database & db, lua_hooks & lua, system_path file)
    {
      result.write_conflict_file
        (db, lua, file, ancestor_rid, left_rid, right_rid,
         left_roster, left_marking, right_roster, right_marking);
    };
};

static void
show_first_conflict(conflicts_t conflicts)
{
  // To find the first conflict, go thru the conflicts we know how to
  // resolve in the same order we output them.
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
          P(F("possible resolutions:"));
          P(F("resolve user \"file_name\""));
          return;
        }
    }

  N(false, F("no resolvable yet unresolved conflicts"));
  
} // show_first_conflict

enum side_t {left, right, neither};
static char const * const conflict_resolution_not_supported_msg = "%s is not a supported conflict resolution for %s";

static void
set_duplicate_name_conflict(resolve_conflicts::file_resolution_t & resolution,
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
      resolution.first  = resolve_conflicts::content_user;
      resolution.second = resolve_conflicts::new_optimal_path(idx(args,1)());
    }
  else
    N(false, F(conflict_resolution_not_supported_msg) % idx(args,0) % "duplicate_name");
  
} //set_duplicate_name_conflict

static void
set_first_conflict(side_t side, conflicts_t & conflicts, args_vector const & args)
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
                  set_duplicate_name_conflict(conflict.left_resolution, args);
                  return;
                }
              break;

            case right:
              if (conflict.right_resolution.first == resolve_conflicts::none)
                {
                  set_duplicate_name_conflict(conflict.right_resolution, args);
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
              if ("user" == idx(args,0)())
                {
                  N(args.size() == 2, F("wrong number of arguments"));
                  
                  conflict.resolution.first  = resolve_conflicts::content_user;
                  conflict.resolution.second = resolve_conflicts::new_optimal_path(idx(args,1)());
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

// CMD(store) is in cmd_merging, since it needs access to
// show_conflicts_core, and doesn't need conflicts_t.

CMD(show_first, "show_first", "", CMD_REF(conflicts),
    "",
    N_("Show the first conflict in the conflicts file, and possible resolutions."),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  show_first_conflict(conflicts);
}

CMD(resolve_first, "resolve_first", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the resolution for the first single-file conflict."),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(neither, conflicts, args);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_left, "resolve_first_left", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the left resolution for the first two-file conflict."),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(left, conflicts, args);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(resolve_first_right, "resolve_first_right", "", CMD_REF(conflicts),
    N_("RESOLUTION"),
    N_("Set the right resolution for the first two-file conflict."),
    "",
    options::opts::conflicts_opts)
{
  database db(app);
  conflicts_t conflicts (db, app.opts.conflicts_file);

  set_first_conflict(right, conflicts, args);

  conflicts.write (db, app.lua, app.opts.conflicts_file);
}

CMD(clean, "clean", "", CMD_REF(conflicts),
    N_(""),
    N_("Delete any bookkeeping files related to conflict resolution."),
    "",
    options::opts::none)
{
  bookkeeping_path conflicts_file("conflicts");
  bookkeeping_path resolutions_dir("resolutions");
  
  if (path_exists(conflicts_file))
    delete_file(conflicts_file);

  if (path_exists(resolutions_dir))
    delete_dir_recursive(resolutions_dir);
}

// end of file
