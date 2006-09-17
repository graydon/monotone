// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>

#include "annotate.hh"
#include "cmd.hh"
#include "diff_patch.hh"
#include "localized_file_io.hh"
#include "packet.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"

using std::cout;
using std::ostream_iterator;
using std::string;
using std::vector;

// fload, fmerge, and fdiff are simple commands for debugging the line
// merger.

CMD(fload, N_("debug"), "", N_("load file contents into db"), &option::none)
{
  string s = get_stdin();

  file_id f_id;
  file_data f_data(s);

  calculate_ident (f_data, f_id);

  packet_db_writer dbw(app);
  dbw.consume_file_data(f_id, f_data);
}

CMD(fmerge, N_("debug"), N_("<parent> <left> <right>"),
    N_("merge 3 files and output result"),
    &option::none)
{
  if (args.size() != 3)
    throw usage(name);

  file_id 
    anc_id(idx(args, 0)()), 
    left_id(idx(args, 1)()), 
    right_id(idx(args, 2)());

  file_data anc, left, right;

  N(app.db.file_version_exists (anc_id),
    F("ancestor file id does not exist"));

  N(app.db.file_version_exists (left_id),
    F("left file id does not exist"));

  N(app.db.file_version_exists (right_id),
    F("right file id does not exist"));

  app.db.get_file_version(anc_id, anc);
  app.db.get_file_version(left_id, left);
  app.db.get_file_version(right_id, right);

  vector<string> anc_lines, left_lines, right_lines, merged_lines;

  split_into_lines(anc.inner()(), anc_lines);
  split_into_lines(left.inner()(), left_lines);
  split_into_lines(right.inner()(), right_lines);
  N(merge3(anc_lines, left_lines, right_lines, merged_lines), F("merge failed"));
  copy(merged_lines.begin(), merged_lines.end(), ostream_iterator<string>(cout, "\n"));

}

CMD(fdiff, N_("debug"), N_("SRCNAME DESTNAME SRCID DESTID"),
    N_("diff 2 files and output result"),
    &option::context_diff % &option::unified_diff % &option::no_show_encloser)
{
  if (args.size() != 4)
    throw usage(name);

  string const
    & src_name = idx(args, 0)(),
    & dst_name = idx(args, 1)();

  file_id 
    src_id(idx(args, 2)()), 
    dst_id(idx(args, 3)());

  file_data src, dst;

  N(app.db.file_version_exists (src_id),
    F("source file id does not exist"));

  N(app.db.file_version_exists (dst_id),
    F("destination file id does not exist"));

  app.db.get_file_version(src_id, src);
  app.db.get_file_version(dst_id, dst);

  string pattern("");
  if (app.diff_show_encloser)
    app.lua.hook_get_encloser_pattern(file_path_external(src_name), pattern);

  make_diff(src_name, dst_name,
            src_id, dst_id,
            src.inner(), dst.inner(),
            cout, app.diff_format, pattern);
}

CMD(annotate, N_("informative"), N_("PATH"),
    N_("print annotated copy of the file from REVISION"),
    &option::revision % &option::brief)
{
  revision_id rid;

  if (app.revision_selectors.size() == 0)
    app.require_workspace();

  if ((args.size() != 1) || (app.revision_selectors.size() > 1))
    throw usage(name);

  file_path file = file_path_external(idx(args, 0));
  split_path sp;
  file.split(sp);

  if (app.revision_selectors.size() == 0)
    app.work.get_revision_id(rid);
  else
    complete(app, idx(app.revision_selectors, 0)(), rid);

  N(!null_id(rid), 
    F("no revision for file '%s' in database") % file);
  N(app.db.revision_exists(rid), 
    F("no such revision '%s'") % rid);

  L(FL("annotate file file_path '%s'") % file);

  // find the version of the file requested
  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  N(roster.has_node(sp), 
    F("no such file '%s' in revision '%s'") % file % rid);
  node_t node = roster.get_node(sp);
  N(is_file_t(node), 
    F("'%s' in revision '%s' is not a file") % file % rid);

  file_t file_node = downcast_to_file_t(node);
  L(FL("annotate for file_id %s") % file_node->self);
  do_annotate(app, file_node, rid, app.opts.brief);
}

CMD(identify, N_("debug"), N_("[PATH]"),
    N_("calculate identity of PATH or stdin"),
    &option::none)
{
  if (!(args.size() == 0 || args.size() == 1))
    throw usage(name);

  data dat;

  if (args.size() == 1)
    {
      read_localized_data(file_path_external(idx(args, 0)), 
                          dat, app.lua);
    }
  else
    {
      dat = get_stdin();
    }

  hexenc<id> ident;
  calculate_ident(dat, ident);
  cout << ident << "\n";
}

CMD(cat, N_("informative"),
    N_("FILENAME"),
    N_("write file from database to stdout"),
    &option::revision)
{
  if (args.size() != 1)
    throw usage(name);

  if (app.revision_selectors.size() == 0)
    app.require_workspace();

  transaction_guard guard(app.db, false);

  revision_id rid;
  if (app.revision_selectors.size() == 0)
    app.work.get_revision_id(rid);
  else
    complete(app, idx(app.revision_selectors, 0)(), rid);
  N(app.db.revision_exists(rid), 
    F("no such revision '%s'") % rid);

  // Paths are interpreted as standard external ones when we're in a
  // workspace, but as project-rooted external ones otherwise.
  file_path fp;
  split_path sp;
  fp = file_path_external(idx(args, 0));
  fp.split(sp);

  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  N(roster.has_node(sp), F("no file '%s' found in revision '%s'") % fp % rid);
  node_t node = roster.get_node(sp);
  N((!null_node(node->self) && is_file_t(node)), F("no file '%s' found in revision '%s'") % fp % rid);

  file_t file_node = downcast_to_file_t(node);
  file_id ident = file_node->content;
  file_data dat;
  L(FL("dumping file '%s'") % ident);
  app.db.get_file_version(ident, dat);
  cout.write(dat.inner()().data(), dat.inner()().size());

  guard.commit();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
