// Copyright (C) 2006 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "ui.hh"
#include "cset.hh"
#include "simplestring_xform.hh"
#include "revision.hh"
#include "file_io.hh"
#include "work.hh"

#include "lexical_cast.hh"
#include <exception>

using std::string;
using std::exception;
using boost::lexical_cast;

// This file's primary entry point is workspace::migrate_ws_format.  It is
// responsible for migrating workspace directories from metadata formats
// used by older versions of monotone.  This file also defines the other
// workspace:: functions related to metadata format.  Whenever a new
// workspace format is added, this file must be updated and a test must be
// added to tests/workspace_migration/, following the instructions in that
// file.

// Workspace metadata formats have a revision number, which is a simple
// nonnegative integer.  Any given version of monotone supports normal use
// of exactly one format, the "current" format; it also supports 'migrating'
// from all previous formats.  The current metadata format is recorded in
// this constant:
static const unsigned int current_workspace_format = 2;

// This is the oldest released version of monotone that supports the current
// format.
static const char first_version_supporting_current_format[] = "0.30";

// In a workspace, the metadata format's revision number is, notionally,
// stored in the file _MTN/format.  However, this file only appears in
// metadata formats 2 and later.  Format 1 is indicated by the _absence_
// of _MTN/format.  Format 0 is even older, and is indicated by the
// metadata directory being named "MT", not "_MTN".  All these little
// details are handled by the following two functions.  Note that
// write_ws_format is a public interface, but get_ws_format is not
// (the corresponding public interface is check_ws_format, below).

static unsigned int
get_ws_format()
{
  unsigned int format;
  bookkeeping_path f_path = bookkeeping_root / "format";
  if (!file_exists(f_path))
    {
      if (directory_exists(bookkeeping_root))
        format = 1;
      else if (directory_exists(file_path() / old_bookkeeping_root_component))
        format = 0;
      else
        N(false, F("workspace required but not found"));
    }
  else
    {
      data f_dat;
      try
        {
          read_data(f_path, f_dat);
          format = lexical_cast<unsigned int>(remove_ws(f_dat()));
        }
      catch (exception & e)
        {
          E(false, F("workspace is corrupt: %s is invalid")
                   % f_path);
        }
      if (format == 1)
        {
          W(F("_MTN/format should not exist in a format 1 workspace; corrected"));
          delete_file(f_path);
        }
    }
  return format;
}

void
workspace::write_ws_format()
{
  bookkeeping_path f_path = bookkeeping_root / "format";
  // one or other side of this conditional will always be dead code, but
  // both sides should be preserved, to document all historical formats.
  // N.B. this will _not_ do the right thing for format 0.  Which is fine.
  if (current_workspace_format <= 1)
    {
      if (file_exists(f_path))
        delete_file(f_path);
    }
  else
    {
      data f_dat(lexical_cast<string>(current_workspace_format) + "\n");
      write_data(f_path, f_dat);
    }
}

// This function is the public face of get_ws_format.  It produces
// suitable error messages if the workspace's format number is not
// equal to current_workspace_format.

void
workspace::check_ws_format()
{
  if (!workspace::found)
    return;

  unsigned int format = get_ws_format();

  // Don't give user false expectations about format 0.
  E(format > 0,
    F("this workspace's metadata is in format 0. to use this workspace\n"
      "with this version of monotone, you must delete it and check it\n"
      "out again (migration from format 0 is not possible).\n"
      "once you have done this, you will not be able to use the workspace\n"
      "with versions of monotone older than %s.\n"
      "we apologize for the inconvenience.")
    % first_version_supporting_current_format);

  E(format >= current_workspace_format,
    F("to use this workspace with this version of monotone, its metadata\n"
      "must be migrated from format %d to format %d, using the command\n"
      "'%s migrate_workspace'.\n"
      "once you have done this, you will not be able to use the workspace\n"
      "with versions of monotone older than %s.")
    % format % current_workspace_format % ui.prog_name
    % first_version_supporting_current_format);

  // keep this message in sync with the copy in migrate_ws_format
  E(format <= current_workspace_format,
    F("this version of monotone only understands workspace metadata\n"
      "in formats 0 through %d.  your workspace is in format %d.\n"
      "you need a newer version of monotone to use this workspace.") 
    % current_workspace_format % format);
}


// Workspace migration is done incrementally.  The functions defined below
// each perform one step.  Note that they must access bookkeeping directory
// files directly, not via work.cc APIs, as those APIs expect a workspace in
// the current format.  Also, note that these functions do not have access
// to the database, lua hooks, or keys; this is because we want the
// migration command to work without options, but work.cc may not know how
// to read options from an old workspace.

static void
migrate_0_to_1()
{
  // Notionally, converting a format 0 workspace to a format 1 workspace is
  // done by renaming the bookkeeping directory from "MT" to "_MTN" and the
  // ignore file from ".mt-ignore" to ".mtn-ignore".  However, there is no
  // point in implementing this, because the first version of monotone that
  // supported workspace format 1 (0.26) also brought a database flag day
  // that invalidates the revision number cached in the bookkeeping
  // directory.  There is no programmatic way to find the new revision
  // number corresponding to what was cached.  Thus, even if we did convert
  // the workspace, it would still be unusable.

  E(false,
    F("it is not possible to migrate from workspace format 0 to any\n"
      "later format.  you must delete this workspace and check it out\n"
      "again.  we apologize for the inconvenience."));
}

static void
migrate_1_to_2()
{
  // In format 1, the parent revision ID of the checkout is stored bare in a
  // file named _MTN/revision, and any directory tree operations are in cset
  // format in _MTN/work, which does not exist if that cset is empty (no
  // changes or only content changes).  In format 2, _MTN/revision contains
  // a serialized revision (qua revision.hh), carrying both pieces of
  // information, and _MTN/work does not exist; also, there may be more than
  // one parent revision, but we do not have to worry about that here.

  bookkeeping_path rev_path = bookkeeping_root / "revision";
  data base_rev_data; MM(base_rev_data);
  try 
    {
      read_data(rev_path, base_rev_data);
    }
  catch (exception & e)
    {
      E(false, F("workspace is corrupt: reading %s: %s")
        % rev_path % e.what());
    }
  revision_id base_rid(remove_ws(base_rev_data())); 
  MM(base_rid);

  cset workcs; 
  MM(workcs);
  bookkeeping_path workcs_path = bookkeeping_root / "work";
  bool delete_workcs = false;
  if (file_exists(workcs_path))
    {
      delete_workcs = true;
      data workcs_data; MM(workcs_data);
      try 
        {
          read_data(workcs_path, workcs_data);
        }
      catch (exception & e)
        {
          E(false, F("workspace is corrupt: reading %s: %s")
            % workcs_path % e.what());
        }

      read_cset(workcs_data, workcs);
    }
  else
    require_path_is_nonexistent(workcs_path,
                                F("workspace is corrupt: "
                                  "%s exists but is not a regular file")
                                % workcs_path);

  revision_t rev;
  MM(rev);
  make_revision_for_workspace(base_rid, workcs, rev);
  data rev_data;
  write_revision(rev, rev_data);
  write_data(rev_path, rev_data);
  if (delete_workcs)
    delete_file(workcs_path);
}

// This function is the public face of the migrate_X_to_X+1 functions.

void
workspace::migrate_ws_format()
{
  unsigned int format = get_ws_format();

  // When adding new migrations, note the organization of the first block of
  // case entries in this switch statement.  There are entries each of the
  // numbers 0 ... C-1 (where C is current_workspace_format); each calls the
  // migrate_<n>_to_<n+1> function, AND DROPS THROUGH.  Thus, when we
  // encounter a workspace in format K < C, the migrate_K_to_K+1,
  // migrate_K+1_to_K+2, ..., migrate_C-1_to_C functions will all be called.
  // The last entry drops through to the write_ws_format() line.

  switch (format)
    {
    case 0: migrate_0_to_1();
    case 1: migrate_1_to_2();

      // We are now in the current format.
      write_ws_format();
      break;

    case current_workspace_format: 
      P(F("this workspace is in the current format, "
          "no migration is necessary."));
      break;

    default:
      I(format > current_workspace_format);
      // keep this message in sync with the copy in check_ws_format
      E(false,
        F("this version of monotone only understands workspace metadata\n"
          "in formats 0 through %d.  your workspace is in format %d.\n"
          "you need a newer version of monotone to use this workspace.") 
        % current_workspace_format % format);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
