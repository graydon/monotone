#include "cmd.hh"

#include "transforms.hh"
#include "simplestring_xform.hh"
#include "packet.hh"
#include "annotate.hh"
#include "diff_patch.hh"
#include "localized_file_io.hh"

#include <iostream>

using std::cout;
using std::ostream_iterator;
using std::string;
using std::vector;

// fload and fmerge are simple commands for debugging the line
// merger.

CMD(fload, N_("debug"), "", N_("load file contents into db"), OPT_NONE)
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
    OPT_NONE)
{
  if (args.size() != 3)
    throw usage(name);

  file_id anc_id(idx(args, 0)()), left_id(idx(args, 1)()), right_id(idx(args, 2)());
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

CMD(annotate, N_("informative"), N_("PATH"),
    N_("print annotated copy of the file from REVISION"),
    OPT_REVISION % OPT_BRIEF)
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
    get_revision_id(rid);
  else
    complete(app, idx(app.revision_selectors, 0)(), rid);

  N(!null_id(rid), F("no revision for file '%s' in database") % file);
  N(app.db.revision_exists(rid), F("no such revision '%s'") % rid);

  L(FL("annotate file file_path '%s'") % file);

  // find the version of the file requested
  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  N(roster.has_node(sp), F("no such file '%s' in revision '%s'") % file % rid);
  node_t node = roster.get_node(sp);
  N(is_file_t(node), F("'%s' in revision '%s' is not a file") % file % rid);

  file_t file_node = downcast_to_file_t(node);
  L(FL("annotate for file_id %s") % file_node->self);
  do_annotate(app, file_node, rid);
}

CMD(identify, N_("debug"), N_("[PATH]"),
    N_("calculate identity of PATH or stdin"),
    OPT_NONE)
{
  if (!(args.size() == 0 || args.size() == 1))
    throw usage(name);

  data dat;

  if (args.size() == 1)
    {
      read_localized_data(file_path_external(idx(args, 0)), dat, app.lua);
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
    OPT_REVISION)
{
  if (args.size() != 1)
    throw usage(name);

  if (app.revision_selectors.size() == 0)
    app.require_workspace();

  transaction_guard guard(app.db, false);

  revision_id rid;
  if (app.revision_selectors.size() == 0)
    get_revision_id(rid);
  else
    complete(app, idx(app.revision_selectors, 0)(), rid);
  N(app.db.revision_exists(rid), F("no such revision '%s'") % rid);

  // paths are interpreted as standard external ones when we're in a
  // workspace, but as project-rooted external ones otherwise
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
