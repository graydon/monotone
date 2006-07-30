// Copyright (C) 2006 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "app_state.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"

#include <boost/lexical_cast.hpp>

using std::string;
using boost::lexical_cast;

// This file's primary entry point is workspace::migrate_ws_format.  It is
// responsible for migrating workspace directories from metadata formats
// used by older versions of monotone.  This file also defines the other
// workspace:: functions related to metadata format.  Whenever a new
// workspace format is added, this file must be updated and tests must be
// added to tests/migrate_workspace/.

// Workspace metadata formats have a revision number, which is a simple
// nonnegative integer.  Any given version of monotone supports normal use
// of exactly one format, the "current" format; it also supports 'migrating'
// from all previous formats.  The current metadata format is recorded in
// this constant:
static const unsigned int current_workspace_format = 1;

// This is the oldest released version of monotone that supports the current
// format.
static const char first_version_supporting_current_format[] = "0.26";

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
      else if (directory_exists(old_bookkeeping_root))
        format = 0;
      else
        N(false, F("workspace required but not found"));
    }
  else
    {
      data f_dat;
      read_data(f_path, f_dat);
      format = lexical_cast<unsigned int>(remove_ws(f_dat()));
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

void
workspace::check_ws_format(app_state & app)
{
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
    % format % current_workspace_format % app.prog_name
    % first_version_supporting_current_format);

  // keep this message in sync with the copy in migrate_ws_format
  E(format <= current_workspace_format,
    F("this version of monotone only understands workspace metadata\n"
      "in formats 0 through %d.  your workspace is in format %d.\n"
      "you need a newer version of monotone to use this workspace.") 
    % current_workspace_format % format);
}


// Workspace migration is done incrementally.  The functions defined below
// each perform one step.

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

// This function is the public face of the migrate_X_to_Y functions.

void
workspace::migrate_ws_format()
{
  unsigned int format = get_ws_format();

  switch (format)
    {
    case 0: migrate_0_to_1();
      break;

    case current_workspace_format: 
      P(F("this workspace is in the current format, no migration is necessary."));
      break;

    default: 
      // keep this message in sync with the copy in check_ws_format
      E(false,
        F("this version of monotone only understands workspace metadata\n"
          "in formats 0 through %d.  your workspace is in format %d.\n"
          "you need a newer version of monotone to use this workspace.") 
        % current_workspace_format % format);
    }

  // We are now in the current format.
  write_ws_format();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
