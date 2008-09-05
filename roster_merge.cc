// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
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

#include "basic_io.hh"
#include "vocab.hh"
#include "roster_merge.hh"
#include "parallel_iter.hh"
#include "safe_map.hh"
#include "transforms.hh"

using boost::shared_ptr;

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;

template <> void
dump(invalid_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "invalid_name_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(directory_loop_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "directory_loop_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(orphaned_node_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "orphaned_node_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(multiple_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "multiple_name_conflict on node: " << conflict.nid << " "
      << "left parent: " << conflict.left.first << " "
      << "basename: " << conflict.left.second << " "
      << "right parent: " << conflict.right.first << " "
      << "basename: " << conflict.right.second << "\n";
  out = oss.str();
}

template <> void
dump(duplicate_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "duplicate_name_conflict between left node: " << conflict.left_nid << " "
      << "and right node: " << conflict.right_nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(attribute_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "attribute_conflict on node: " << conflict.nid << " "
      << "attr: '" << conflict.key << "' "
      << "left: " << conflict.left.first << " '" << conflict.left.second << "' "
      << "right: " << conflict.right.first << " '" << conflict.right.second << "'\n";
  out = oss.str();
}

template <> void
dump(file_content_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "file_content_conflict on node: " << conflict.nid << " "
      << "left: " << conflict.left << " "
      << "right: " << conflict.right << "\n";
  out = oss.str();
}

bool
roster_merge_result::is_clean() const
{
  return !has_non_content_conflicts()
    && !has_content_conflicts();
}

bool
roster_merge_result::has_content_conflicts() const
{
  return file_content_conflicts.size() > 0;
}

bool
roster_merge_result::has_non_content_conflicts() const
{
  return missing_root_dir
    || !invalid_name_conflicts.empty()
    || !directory_loop_conflicts.empty()
    || !orphaned_node_conflicts.empty()
    || !multiple_name_conflicts.empty()
    || !duplicate_name_conflicts.empty()
    || !attribute_conflicts.empty();
}
static void
dump_conflicts(roster_merge_result const & result, string & out)
{
  if (result.missing_root_dir)
    out += (FL("missing_root_conflict: root directory has been removed\n")).str();

  dump(result.invalid_name_conflicts, out);
  dump(result.directory_loop_conflicts, out);

  dump(result.orphaned_node_conflicts, out);
  dump(result.multiple_name_conflicts, out);
  dump(result.duplicate_name_conflicts, out);

  dump(result.attribute_conflicts, out);
  dump(result.file_content_conflicts, out);
}

template <> void
dump(roster_merge_result const & result, string & out)
{
  dump_conflicts(result, out);

  string roster_part;
  dump(result.roster, roster_part);
  out += "\n\n";
  out += roster_part;
}

void
roster_merge_result::log_conflicts() const
{
  string str;
  dump_conflicts(*this, str);
  L(FL("%s") % str);
}

namespace
{
  enum node_type { file_type, dir_type };

  node_type
  get_type(roster_t const & roster, node_id const nid)
  {
    node_t n = roster.get_node(nid);

    if (is_file_t(n))
      return file_type;
    else if (is_dir_t(n))
      return dir_type;
    else
      I(false);
  }
}

namespace
{
  namespace syms
  {
    symbol const ancestor_file_id("ancestor_file_id");
    symbol const ancestor_name("ancestor_name");
    symbol const attr_name("attr_name");
    symbol const attribute("attribute");
    symbol const conflict("conflict");
    symbol const content("content");
    symbol const directory_loop_created("directory_loop_created");
    symbol const duplicate_name("duplicate_name");
    symbol const invalid_name("invalid_name");
    symbol const left_attr_state("left_attr_state");
    symbol const left_attr_value("left_attr_value");
    symbol const left_file_id("left_file_id");
    symbol const left_name("left_name");
    symbol const left_type("left_type");
    symbol const missing_root("missing_root");
    symbol const multiple_names("multiple_names");
    symbol const node_type("node_type");
    symbol const orphaned_directory("orphaned_directory");
    symbol const orphaned_file("orphaned_file");
    symbol const right_attr_state("right_attr_state");
    symbol const right_attr_value("right_attr_value");
    symbol const right_file_id("right_file_id");
    symbol const right_name("right_name");
    symbol const right_type("right_type");
  }
}

static void
put_added_conflict_left (basic_io::stanza & st,
                         content_merge_adaptor & adaptor,
                         node_id const nid)
{
  // We access the roster via the adaptor, to be sure we use the left
  // roster; avoids typos in long parameter lists.

  // If we get a workspace adaptor here someday, we should add the required
  // access functions to content_merge_adaptor.

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> roster(db_adaptor.rosters[db_adaptor.left_rid]);
  file_path name;

  roster->get_name (nid, name);

  if (file_type == get_type (*roster, nid))
    {
      file_id fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, nid, fid);
      st.push_str_pair(syms::left_type, "added file");
      st.push_file_pair(syms::left_name, name);
      st.push_binary_pair(syms::left_file_id, fid.inner());
    }
  else
    {
      st.push_str_pair(syms::left_type, "added directory");
      st.push_file_pair(syms::left_name, name);
    }
}

static void
put_added_conflict_right (basic_io::stanza & st,
                          content_merge_adaptor & adaptor,
                          node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != roster);

  file_path name;

  roster->get_name (nid, name);

  if (file_type == get_type (*roster, nid))
    {
      file_id fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, nid, fid);

      st.push_str_pair(syms::right_type, "added file");
      st.push_file_pair(syms::right_name, name);
      st.push_binary_pair(syms::right_file_id, fid.inner());
    }
  else
    {
      st.push_str_pair(syms::right_type, "added directory");
      st.push_file_pair(syms::right_name, name);
    }
}

static void
put_rename_conflict_left (basic_io::stanza & st,
                          content_merge_adaptor & adaptor,
                          node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> ancestor_roster(db_adaptor.rosters[db_adaptor.lca]);
  I(0 != ancestor_roster);
  boost::shared_ptr<roster_t const> left_roster(db_adaptor.rosters[db_adaptor.left_rid]);

  file_path ancestor_name;
  file_path left_name;

  ancestor_roster->get_name (nid, ancestor_name);
  left_roster->get_name (nid, left_name);

  if (file_type == get_type (*left_roster, nid))
    {
      st.push_str_pair(syms::left_type, "renamed file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, nid, left_fid);
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::left_type, "renamed directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
    }
}

static void
put_rename_conflict_right (basic_io::stanza & st,
                           content_merge_adaptor & adaptor,
                           node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> ancestor_roster(db_adaptor.rosters[db_adaptor.lca]);
  I(0 != ancestor_roster);
  boost::shared_ptr<roster_t const> right_roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != right_roster);

  file_path ancestor_name;
  file_path right_name;

  ancestor_roster->get_name (nid, ancestor_name);
  right_roster->get_name (nid, right_name);

  if (file_type == get_type (*right_roster, nid))
    {
      st.push_str_pair(syms::right_type, "renamed file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, nid, right_fid);
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::right_type, "renamed directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::right_name, right_name);
    }
}

static void
put_attr_state_left (basic_io::stanza & st, attribute_conflict const & conflict)
{
  if (conflict.left.first)
    st.push_str_pair(syms::left_attr_value, conflict.left.second());
  else
    st.push_str_pair(syms::left_attr_state, "dropped");
}

static void
put_attr_state_right (basic_io::stanza & st, attribute_conflict const & conflict)
{
  if (conflict.right.first)
    st.push_str_pair(syms::right_attr_value, conflict.right.second());
  else
    st.push_str_pair(syms::right_attr_state, "dropped");
}

static void
put_attr_conflict (basic_io::stanza & st,
                   content_merge_adaptor & adaptor,
                   attribute_conflict const & conflict)
{
  // Always report ancestor, left, and right information, for completeness

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));

  // This ensures that the ancestor roster is computed
  boost::shared_ptr<roster_t const> ancestor_roster;
  revision_id ancestor_rid;
  db_adaptor.get_ancestral_roster (conflict.nid, ancestor_rid, ancestor_roster);

  boost::shared_ptr<roster_t const> left_roster(db_adaptor.rosters[db_adaptor.left_rid]);
  I(0 != left_roster);
  boost::shared_ptr<roster_t const> right_roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != right_roster);

  file_path ancestor_name;
  file_path left_name;
  file_path right_name;

  ancestor_roster->get_name (conflict.nid, ancestor_name);
  left_roster->get_name (conflict.nid, left_name);
  right_roster->get_name (conflict.nid, right_name);

  if (file_type == get_type (*ancestor_roster, conflict.nid))
    {
      st.push_str_pair(syms::node_type, "file");
      st.push_str_pair(syms::attr_name, conflict.key());
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, conflict.nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      // FIXME: don't have this. st.push_str_pair(syms::ancestor_attr_value, ???);
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, conflict.nid, left_fid);
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
      put_attr_state_left (st, conflict);
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, conflict.nid, right_fid);
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
      put_attr_state_right (st, conflict);
    }
  else
    {
      st.push_str_pair(syms::node_type, "directory");
      st.push_str_pair(syms::attr_name, conflict.key());
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      // FIXME: don't have this. st.push_str_pair(syms::ancestor_attr_value, ???);
      st.push_file_pair(syms::left_name, left_name);
      put_attr_state_left (st, conflict);
      st.push_file_pair(syms::right_name, right_name);
      put_attr_state_right (st, conflict);
    }
}

static void
put_content_conflict (basic_io::stanza & st,
                      content_merge_adaptor & adaptor,
                      file_content_conflict const & conflict)
{
  // Always report ancestor, left, and right information, for completeness

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));

  // This ensures that the ancestor roster is computed
  boost::shared_ptr<roster_t const> ancestor_roster;
  revision_id ancestor_rid;
  db_adaptor.get_ancestral_roster (conflict.nid, ancestor_rid, ancestor_roster);

  boost::shared_ptr<roster_t const> left_roster(db_adaptor.rosters[db_adaptor.left_rid]);
  I(0 != left_roster);
  boost::shared_ptr<roster_t const> right_roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != right_roster);

  file_path ancestor_name;
  file_path left_name;
  file_path right_name;

  ancestor_roster->get_name (conflict.nid, ancestor_name);
  left_roster->get_name (conflict.nid, left_name);
  right_roster->get_name (conflict.nid, right_name);

  if (file_type == get_type (*ancestor_roster, conflict.nid))
    {
      st.push_str_pair(syms::node_type, "file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, conflict.nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, conflict.nid, left_fid);
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, conflict.nid, right_fid);
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::node_type, "directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
      st.push_file_pair(syms::right_name, right_name);
    }
}

static void
put_stanza (basic_io::stanza & st,
            std::ostream & output)
{
  // We have to declare the printer here, rather than more globally,
  // because adaptor.get_ancestral_roster uses a basic_io::printer
  // internally, and there can only be one active at a time.
  basic_io::printer pr;
  output << "\n";
  pr.print_stanza(st);
  output.write(pr.buf.data(), pr.buf.size());
}

void
roster_merge_result::report_missing_root_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool const basic_io,
                                                   std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  if (missing_root_dir)
    {
      node_id left_root, right_root;
      left_root = left_roster.root()->self;
      right_root = right_roster.root()->self;

      // these must be different for this conflict to happen
      I(left_root != right_root);

      shared_ptr<roster_t const> left_lca_roster, right_lca_roster;
      revision_id left_lca_rid, right_lca_rid;
      file_path left_lca_name, right_lca_name;

      adaptor.get_ancestral_roster(left_root, left_lca_rid,
                                   left_lca_roster);
      adaptor.get_ancestral_roster(right_root, right_lca_rid,
                                   right_lca_roster);

      left_lca_roster->get_name(left_root, left_lca_name);
      right_lca_roster->get_name(right_root, right_lca_name);

      node_id left_lca_root = left_lca_roster->root()->self;
      node_id right_lca_root = right_lca_roster->root()->self;

      basic_io::stanza st;

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::missing_root);
      else
        P(F("conflict: missing root directory"));

      if (left_root != left_lca_root && right_root == right_lca_root)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the left") % left_lca_name);

          if (!right_roster.has_node(left_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::right_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the right") % left_lca_name);
            }
        }
      else if (left_root == left_lca_root && right_root != right_lca_root)
        {
          if (!left_roster.has_node(right_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::left_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the left") % right_lca_name);
            }

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the right") % right_lca_name);
        }
      else if (left_root != left_lca_root && right_root != right_lca_root)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the left") % left_lca_name);

          if (!right_roster.has_node(left_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::right_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the right") % left_lca_name);
            }

          if (!left_roster.has_node(right_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::left_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the left") % right_lca_name);
            }

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the right") % right_lca_name);
        }
      // else
      // other conflicts can cause the root dir to be left detached
      // for example, merging two independently created projects
      // in these cases don't report anything about pivot_root

      if (basic_io)
        put_stanza (st, output);
    }
}

void
roster_merge_result::report_invalid_name_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool basic_io,
                                                   std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < invalid_name_conflicts.size(); ++i)
    {
      invalid_name_conflict const & conflict = invalid_name_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      shared_ptr<roster_t const> lca_roster, parent_lca_roster;
      revision_id lca_rid, parent_lca_rid;
      file_path lca_name, lca_parent_name;
      basic_io::stanza st;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);
      lca_roster->get_name(conflict.parent_name.first, lca_parent_name);

      adaptor.get_ancestral_roster(conflict.parent_name.first,
                                   parent_lca_rid, parent_lca_roster);

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::invalid_name);
      else
        P(F("conflict: invalid name _MTN in root directory"));

      if (left_roster.root()->self == conflict.parent_name.first)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, lca_parent_name.as_external());
            }
          else
            P(F("'%s' pivoted to root on the left")
              % lca_parent_name);

          file_path right_name;
          right_roster.get_name(conflict.nid, right_name);
          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_right (st, adaptor, conflict.nid);
              else
                P(F("'%s' renamed to '%s' on the right")
                  % lca_name % right_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_right (st, adaptor, conflict.nid);
              else
                P(F("'%s' added in revision %s on the right")
                  % right_name % lca_rid);
            }
        }
      else if (right_roster.root()->self == conflict.parent_name.first)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, lca_parent_name.as_external());
            }
          else
            P(F("'%s' pivoted to root on the right")
              % lca_parent_name);

          file_path left_name;
          left_roster.get_name(conflict.nid, left_name);
          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_left (st, adaptor, conflict.nid);
              else
                P(F("'%s' renamed to '%s' on the left")
                  % lca_name % left_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_left (st, adaptor, conflict.nid);
              else
                P(F("'%s' added in revision %s on the left")
                  % left_name % lca_rid);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_directory_loop_conflicts(roster_t const & left_roster,
                                                     roster_t const & right_roster,
                                                     content_merge_adaptor & adaptor,
                                                     bool basic_io,
                                                     std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < directory_loop_conflicts.size(); ++i)
    {
      directory_loop_conflict const & conflict = directory_loop_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      file_path left_name, right_name, left_parent_name, right_parent_name;

      left_roster.get_name(conflict.nid, left_name);
      right_roster.get_name(conflict.nid, right_name);

      left_roster.get_name(conflict.parent_name.first, left_parent_name);
      right_roster.get_name(conflict.parent_name.first, right_parent_name);

      shared_ptr<roster_t const> lca_roster;
      revision_id lca_rid;
      file_path lca_name, lca_parent_name;
      basic_io::stanza st;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);
      lca_roster->get_name(conflict.parent_name.first, lca_parent_name);

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::directory_loop_created);
      else
        P(F("conflict: directory loop created"));

      if (left_name != lca_name)
        {
          if (basic_io)
            put_rename_conflict_left (st, adaptor, conflict.nid);
          else
            P(F("'%s' renamed to '%s' on the left")
              % lca_name % left_name);
        }

      if (right_name != lca_name)
        {
          if (basic_io)
            put_rename_conflict_right (st, adaptor, conflict.nid);
          else
            P(F("'%s' renamed to '%s' on the right")
              % lca_name % right_name);
        }

      if (left_parent_name != lca_parent_name)
        {
          if (basic_io)
            put_rename_conflict_left (st, adaptor, conflict.parent_name.first);
          else
            P(F("'%s' renamed to '%s' on the left")
              % lca_parent_name % left_parent_name);
        }

      if (right_parent_name != lca_parent_name)
        {
          if (basic_io)
            put_rename_conflict_right (st, adaptor, conflict.parent_name.first);
          else
            P(F("'%s' renamed to '%s' on the right")
              % lca_parent_name % right_parent_name);
        }

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_orphaned_node_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    bool basic_io,
                                                    std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < orphaned_node_conflicts.size(); ++i)
    {
      orphaned_node_conflict const & conflict = orphaned_node_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      shared_ptr<roster_t const> lca_roster, parent_lca_roster;
      revision_id lca_rid, parent_lca_rid;
      file_path lca_name;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      adaptor.get_ancestral_roster(conflict.parent_name.first,
                                   parent_lca_rid, parent_lca_roster);

      lca_roster->get_name(conflict.nid, lca_name);

      node_type type = get_type(*lca_roster, conflict.nid);

      basic_io::stanza st;

      if (type == file_type)
          if (basic_io)
            st.push_str_pair(syms::conflict, syms::orphaned_file);
          else
            P(F("conflict: orphaned file '%s' from revision %s")
              % lca_name % lca_rid);
      else
        {
          if (basic_io)
            st.push_str_pair(syms::conflict, syms::orphaned_directory);
          else
            P(F("conflict: orphaned directory '%s' from revision %s")
              % lca_name % lca_rid);
        }

      if (left_roster.has_node(conflict.parent_name.first) &&
          !right_roster.has_node(conflict.parent_name.first))
        {
          file_path orphan_name, parent_name;
          left_roster.get_name(conflict.nid, orphan_name);
          left_roster.get_name(conflict.parent_name.first, parent_name);

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "deleted directory");
              st.push_str_pair(syms::ancestor_name, parent_name.as_external());
            }
          else
            P(F("parent directory '%s' was deleted on the right")
              % parent_name);

          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_left (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was renamed from '%s' on the left")
                    % orphan_name % lca_name);
                else
                  P(F("directory '%s' was renamed from '%s' on the left")
                    % orphan_name % lca_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_left (st, adaptor, conflict.nid);
              else
                {
                  if (type == file_type)
                    P(F("file '%s' was added on the left")
                      % orphan_name);
                  else
                    P(F("directory '%s' was added on the left")
                      % orphan_name);
                }
            }
        }
      else if (!left_roster.has_node(conflict.parent_name.first) &&
               right_roster.has_node(conflict.parent_name.first))
        {
          file_path orphan_name, parent_name;
          right_roster.get_name(conflict.nid, orphan_name);
          right_roster.get_name(conflict.parent_name.first, parent_name);

          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "deleted directory");
              st.push_str_pair(syms::ancestor_name, parent_name.as_external());
            }
          else
            P(F("parent directory '%s' was deleted on the left")
              % parent_name);

          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_right (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was renamed from '%s' on the right")
                    % orphan_name % lca_name);
                else
                  P(F("directory '%s' was renamed from '%s' on the right")
                    % orphan_name % lca_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_right (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was added on the right")
                    % orphan_name);
                else
                  P(F("directory '%s' was added on the right")
                    % orphan_name);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza (st, output);
    }
}

void
roster_merge_result::report_multiple_name_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    bool basic_io,
                                                    std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < multiple_name_conflicts.size(); ++i)
    {
      multiple_name_conflict const & conflict = multiple_name_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      file_path left_name, right_name;

      left_roster.get_name(conflict.nid, left_name);
      right_roster.get_name(conflict.nid, right_name);

      shared_ptr<roster_t const> lca_roster;
      revision_id lca_rid;
      file_path lca_name;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);

      node_type type = get_type(*lca_roster, conflict.nid);

      basic_io::stanza st;

      if (basic_io)
        {
          st.push_str_pair(syms::conflict, syms::multiple_names);
          put_rename_conflict_left (st, adaptor, conflict.nid);
          put_rename_conflict_right (st, adaptor, conflict.nid);
        }
      else
        {
          if (type == file_type)
            P(F("conflict: multiple names for file '%s' from revision %s")
              % lca_name % lca_rid);
          else
            P(F("conflict: multiple names for directory '%s' from revision %s")
              % lca_name % lca_rid);

          P(F("renamed to '%s' on the left") % left_name);
          P(F("renamed to '%s' on the right") % right_name);
        }

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_duplicate_name_conflicts(roster_t const & left_roster,
                                                     roster_t const & right_roster,
                                                     content_merge_adaptor & adaptor,
                                                     bool const basic_io,
                                                     std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < duplicate_name_conflicts.size(); ++i)
    {
      duplicate_name_conflict const & conflict = duplicate_name_conflicts[i];
      MM(conflict);

      node_id left_nid, right_nid;

      left_nid = conflict.left_nid;
      right_nid = conflict.right_nid;

      I(!roster.is_attached(left_nid));
      I(!roster.is_attached(right_nid));

      file_path left_name, right_name;

      left_roster.get_name(left_nid, left_name);
      right_roster.get_name(right_nid, right_name);

      shared_ptr<roster_t const> left_lca_roster, right_lca_roster;
      revision_id left_lca_rid, right_lca_rid;

      adaptor.get_ancestral_roster(left_nid, left_lca_rid, left_lca_roster);
      adaptor.get_ancestral_roster(right_nid, right_lca_rid, right_lca_roster);

      // In most cases, the left_name equals the right_name. However, maybe
      // a parent directory got renamed on one side. In that case, the names
      // don't match, but it's still the same directory (by node id), to
      // which we want to add the same file (by name).

      basic_io::stanza st;

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::duplicate_name);
      else
        {
          if (left_name == right_name)
            {
              file_path dir;
              path_component basename;
              left_name.dirname_basename(dir, basename);
              P(F("conflict: duplicate name '%s' for the directory '%s'") % basename % dir);
            }
          else
            {
              file_path left_dir, right_dir;
              path_component left_basename, right_basename;
              left_name.dirname_basename(left_dir, left_basename);
              right_name.dirname_basename(right_dir, right_basename);
              I(left_basename == right_basename);
              P(F("conflict: duplicate name '%s' for the directory\n"
                  "          named '%s' on the left and\n"
                  "          named '%s' on the right.")
                % left_basename % left_dir % right_dir);
            }
        }

      node_type left_type  = get_type(left_roster, left_nid);
      node_type right_type = get_type(right_roster, right_nid);

      if (!left_lca_roster->has_node(right_nid) &&
          !right_lca_roster->has_node(left_nid))
        {
          if (basic_io)
            put_added_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("added as a new file on the left"));
              else
                P(F("added as a new directory on the left"));
            }

          if (basic_io)
            put_added_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("added as a new file on the right"));
              else
                P(F("added as a new directory on the right"));
            }
         }
      else if (!left_lca_roster->has_node(right_nid) &&
               right_lca_roster->has_node(left_nid))
        {
          file_path left_lca_name;
          left_lca_roster->get_name(left_nid, left_lca_name);

          if (basic_io)
            put_rename_conflict_left (st, adaptor, left_nid);
          else
            if (left_type == file_type)
              P(F("renamed from file '%s' on the left") % left_lca_name);
            else
              P(F("renamed from directory '%s' on the left") % left_lca_name);

          if (basic_io)
            put_added_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("added as a new file on the right"));
              else
                P(F("added as a new directory on the right"));
            }
        }
      else if (left_lca_roster->has_node(right_nid) &&
               !right_lca_roster->has_node(left_nid))
        {
          file_path right_lca_name;
          right_lca_roster->get_name(right_nid, right_lca_name);

          if (basic_io)
            put_added_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("added as a new file on the left"));
              else
                P(F("added as a new directory on the left"));
            }

          if (basic_io)
            put_rename_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("renamed from file '%s' on the right") % right_lca_name);
              else
                P(F("renamed from directory '%s' on the right") % right_lca_name);
            }
        }
      else if (left_lca_roster->has_node(right_nid) &&
               right_lca_roster->has_node(left_nid))
        {
          file_path left_lca_name, right_lca_name;
          left_lca_roster->get_name(left_nid, left_lca_name);
          right_lca_roster->get_name(right_nid, right_lca_name);

          if (basic_io)
            put_rename_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("renamed from file '%s' on the left") % left_lca_name);
              else
                P(F("renamed from directory '%s' on the left") % left_lca_name);
            }

          if (basic_io)
            put_rename_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("renamed from file '%s' on the right") % right_lca_name);
              else
                P(F("renamed from directory '%s' on the right") % right_lca_name);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_attribute_conflicts(roster_t const & left_roster,
                                                roster_t const & right_roster,
                                                content_merge_adaptor & adaptor,
                                                bool basic_io,
                                                std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < attribute_conflicts.size(); ++i)
    {
      attribute_conflict const & conflict = attribute_conflicts[i];
      MM(conflict);

      if (basic_io)
        {
          basic_io::stanza st;

          st.push_str_pair(syms::conflict, syms::attribute);
          put_attr_conflict (st, adaptor, conflict);
          put_stanza (st, output);
        }
      else
        {
          node_type type = get_type(roster, conflict.nid);

          if (roster.is_attached(conflict.nid))
            {
              file_path name;
              roster.get_name(conflict.nid, name);

              if (type == file_type)
                P(F("conflict: multiple values for attribute '%s' on file '%s'")
                  % conflict.key % name);
              else
                P(F("conflict: multiple values for attribute '%s' on directory '%s'")
                  % conflict.key % name);

              if (conflict.left.first)
                P(F("set to '%s' on the left") % conflict.left.second);
              else
                P(F("deleted on the left"));

              if (conflict.right.first)
                P(F("set to '%s' on the right") % conflict.right.second);
              else
                P(F("deleted on the right"));
            }
          else
            {
              // This node isn't attached in the merged roster, due to another
              // conflict (ie renamed to different names). So report the
              // ancestor name and the left and right names.

              file_path left_name, right_name;
              left_roster.get_name(conflict.nid, left_name);
              right_roster.get_name(conflict.nid, right_name);

              shared_ptr<roster_t const> lca_roster;
              revision_id lca_rid;
              file_path lca_name;

              adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
              lca_roster->get_name(conflict.nid, lca_name);

              if (type == file_type)
                P(F("conflict: multiple values for attribute '%s' on file '%s' from revision %s")
                  % conflict.key % lca_name % lca_rid);
              else
                P(F("conflict: multiple values for attribute '%s' on directory '%s' from revision %s")
                  % conflict.key % lca_name % lca_rid);

              if (conflict.left.first)
                {
                  if (type == file_type)
                    P(F("set to '%s' on left file '%s'")
                      % conflict.left.second % left_name);
                  else
                    P(F("set to '%s' on left directory '%s'")
                      % conflict.left.second % left_name);
                }
              else
                {
                  if (type == file_type)
                    P(F("deleted from left file '%s'")
                      % left_name);
                  else
                    P(F("deleted from left directory '%s'")
                      % left_name);
                }

              if (conflict.right.first)
                {
                  if (type == file_type)
                    P(F("set to '%s' on right file '%s'")
                      % conflict.right.second % right_name);
                  else
                    P(F("set to '%s' on right directory '%s'")
                      % conflict.right.second % right_name);
                }
              else
                {
                  if (type == file_type)
                    P(F("deleted from right file '%s'")
                      % right_name);
                  else
                    P(F("deleted from right directory '%s'")
                      % right_name);
                }
            }
        }
    }
}

void
roster_merge_result::report_file_content_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool basic_io,
                                                   std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < file_content_conflicts.size(); ++i)
    {
      file_content_conflict const & conflict = file_content_conflicts[i];
      MM(conflict);


      if (basic_io)
        {
          basic_io::stanza st;

          st.push_str_pair(syms::conflict, syms::content);
          put_content_conflict (st, adaptor, conflict);
          put_stanza (st, output);
        }
      else
        {
          if (roster.is_attached(conflict.nid))
            {
              file_path name;
              roster.get_name(conflict.nid, name);

              P(F("conflict: content conflict on file '%s'")
                % name);
              P(F("content hash is %s on the left") % conflict.left);
              P(F("content hash is %s on the right") % conflict.right);
            }
          else
            {
              // this node isn't attached in the merged roster and there
              // isn't really a good name for it so report both the left
              // and right names using a slightly different format

              file_path left_name, right_name;
              left_roster.get_name(conflict.nid, left_name);
              right_roster.get_name(conflict.nid, right_name);

              shared_ptr<roster_t const> lca_roster;
              revision_id lca_rid;
              file_path lca_name;

              adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
              lca_roster->get_name(conflict.nid, lca_name);

              P(F("conflict: content conflict on file '%s' from revision %s")
                % lca_name % lca_rid);
              P(F("content hash is %s on the left in file '%s'")
                % conflict.left % left_name);
              P(F("content hash is %s on the right in file '%s'")
                % conflict.right % right_name);
            }
        }
    }
}

void
roster_merge_result::clear()
{
  missing_root_dir = false;
  invalid_name_conflicts.clear();
  directory_loop_conflicts.clear();

  orphaned_node_conflicts.clear();
  multiple_name_conflicts.clear();
  duplicate_name_conflicts.clear();

  attribute_conflicts.clear();
  file_content_conflicts.clear();

  roster = roster_t();
}

namespace
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(set<revision_id> const & b_marks,
         set<revision_id> const & b_uncommon_ancestors)
  {
    for (set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               set<revision_id> const & left_marks,
               set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               set<revision_id> const & right_marks,
               set<revision_id> const & right_uncommon_ancestors,
               T & result,
               C & conflict_descriptor)
  {
    if (left == right)
      {
        result = left;
        return true;
      }
    MM(left_marks);
    MM(left_uncommon_ancestors);
    MM(right_marks);
    MM(right_uncommon_ancestors);
    bool left_wins = a_wins(right_marks, right_uncommon_ancestors);
    bool right_wins = a_wins(left_marks, left_uncommon_ancestors);
    // two bools means 4 cases:
    //   left_wins && right_wins
    //     this is ambiguous clean merge, which is theoretically impossible.
    I(!(left_wins && right_wins));
    //   left_wins && !right_wins
    if (left_wins && !right_wins)
      {
        result = left;
        return true;
      }
    //   !left_wins && right_wins
    if (!left_wins && right_wins)
      {
        result = right;
        return true;
      }
    //   !left_wins && !right_wins
    if (!left_wins && !right_wins)
      {
        conflict_descriptor.left = left;
        conflict_descriptor.right = right;
        return false;
      }
    I(false);
  }

  inline void
  create_node_for(node_t const & n, roster_t & new_roster)
  {
    if (is_dir_t(n))
      new_roster.create_dir_node(n->self);
    else if (is_file_t(n))
      new_roster.create_file_node(file_id(), n->self);
    else
      I(false);
  }

  inline void
  insert_if_unborn(node_t const & n,
                   marking_map const & markings,
                   set<revision_id> const & uncommon_ancestors,
                   roster_t const & parent_roster,
                   roster_t & new_roster)
  {
    revision_id const & birth = safe_get(markings, n->self).birth_revision;
    if (uncommon_ancestors.find(birth) != uncommon_ancestors.end())
      create_node_for(n, new_roster);
    else
      {
        // In this branch we are NOT inserting the node into the new roster as it
        // has been deleted from the other side of the merge.
        // In this case, output a warning if there are changes to the file on the
        // side of the merge where it still exists.
        set<revision_id> const & content_marks = safe_get(markings, n->self).file_content;
        bool found_one_ignored_content = false;
        for (set<revision_id>::const_iterator it = content_marks.begin(); it != content_marks.end(); it++)
          {
            if (uncommon_ancestors.find(*it) != uncommon_ancestors.end())
              {
                if (!found_one_ignored_content)
                  {
                    file_path fp;
                    parent_roster.get_name(n->self, fp);
                    W(F("Content changes to the file '%s'\n"
                        "will be ignored during this merge as the file has been\n"
                        "removed on one side of the merge.  Affected revisions include:") % fp);
                  }
                found_one_ignored_content = true;
                W(F("Revision: %s") % encode_hexenc(it->inner()()));
              }
          }
      }
  }

  bool
  would_make_dir_loop(roster_t const & r, node_id nid, node_id parent)
  {
    // parent may not be fully attached yet; that's okay.  that just means
    // we'll run into a node with a null parent somewhere before we hit the
    // actual root; whether we hit the actual root or not, hitting a node
    // with a null parent will tell us that this particular attachment won't
    // create a loop.
    for (node_id curr = parent; !null_node(curr); curr = r.get_node(curr)->parent)
      {
        if (curr == nid)
          return true;
      }
    return false;
  }

  enum side_t { left_side, right_side };

  void
  assign_name(roster_merge_result & result, node_id nid,
              node_id parent, path_component name, side_t side)
  {
    // this function is reponsible for detecting structural conflicts.  by the
    // time we've gotten here, we have a node that's unambiguously decided on
    // a name; but it might be that that name does not exist (because the
    // parent dir is gone), or that it's already taken (by another node), or
    // that putting this node there would create a directory loop.  In all
    // such cases, rather than actually attach the node, we write a conflict
    // structure and leave it detached.

    // the root dir is somewhat special.  it can't be orphaned, and it can't
    // make a dir loop.  it can, however, have a name collision.
    if (null_node(parent))
      {
        I(name.empty());
        if (result.roster.has_root())
          {
            // see comments below about name collisions.
            duplicate_name_conflict c;
            // some other node has already been attached at the root location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case left_side:
                c.left_nid = nid;
                c.right_nid = result.roster.root()->self;
                break;
              case right_side:
                c.left_nid = result.roster.root()->self;
                c.right_nid = nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            result.roster.detach_node(file_path());
            result.duplicate_name_conflicts.push_back(c);
            return;
          }
      }
    else
      {
        // orphan:
        if (!result.roster.has_node(parent))
          {
            orphaned_node_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.orphaned_node_conflicts.push_back(c);
            return;
          }

        dir_t p = downcast_to_dir_t(result.roster.get_node(parent));

        // duplicate name conflict:
        // see the comment in roster_merge.hh for the analysis showing that at
        // most two nodes can participate in a duplicate name conflict.  this code
        // exploits that; after this code runs, there will be no node at the given
        // location in the tree, which means that in principle, if there were a
        // third node that _also_ wanted to go here, when we got around to
        // attaching it we'd have no way to realize it should be a conflict.  but
        // that never happens, so we don't have to keep a lookaside set of
        // "poisoned locations" or anything.
        if (p->has_child(name))
          {
            duplicate_name_conflict c;
            // some other node has already been attached at the named location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case left_side:
                c.left_nid = nid;
                c.right_nid = p->get_child(name)->self;
                break;
              case right_side:
                c.left_nid = p->get_child(name)->self;
                c.right_nid = nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            p->detach_child(name);
            result.duplicate_name_conflicts.push_back(c);
            return;
          }

        if (would_make_dir_loop(result.roster, nid, parent))
          {
            directory_loop_conflict c;
            c.nid = nid;
            c.parent_name = make_pair(parent, name);
            result.directory_loop_conflicts.push_back(c);
            return;
          }
      }
    // hey, we actually made it.  attach the node!
    result.roster.attach_node(nid, parent, name);
  }

  void
  copy_node_forward(roster_merge_result & result, node_t const & n,
                    node_t const & old_n, side_t const & side)
  {
    I(n->self == old_n->self);
    n->attrs = old_n->attrs;
    if (is_file_t(n))
      downcast_to_file_t(n)->content = downcast_to_file_t(old_n)->content;
    assign_name(result, n->self, old_n->parent, old_n->name, side);
  }

} // end anonymous namespace

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             set<revision_id> const & right_uncommon_ancestors,
             roster_merge_result & result)
{
  L(FL("Performing a roster_merge"));

  result.clear();
  MM(left_parent);
  MM(left_markings);
  MM(right_parent);
  MM(right_markings);
  MM(result);

  // First handle lifecycles, by die-die-die merge -- our result will contain
  // everything that is alive in both parents, or alive in one and unborn in
  // the other, exactly.
  {
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            insert_if_unborn(i.left_data(),
                             left_markings, left_uncommon_ancestors, left_parent,
                             result.roster);
            break;

          case parallel::in_right:
            insert_if_unborn(i.right_data(),
                             right_markings, right_uncommon_ancestors, right_parent,
                             result.roster);
            break;

          case parallel::in_both:
            create_node_for(i.left_data(), result.roster);
            break;
          }
      }
  }

  // okay, our roster now contains a bunch of empty, detached nodes.  fill
  // them in one at a time with *-merge.
  {
    node_map::const_iterator left_i, right_i;
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    node_map::const_iterator new_i = result.roster.all_nodes().begin();
    marking_map::const_iterator left_mi = left_markings.begin();
    marking_map::const_iterator right_mi = right_markings.begin();
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            {
              node_t const & left_n = i.left_data();
              // we skip nodes that aren't in the result roster (were
              // deleted in the lifecycles step above)
              if (result.roster.has_node(left_n->self))
                {
                  // attach this node from the left roster. this may cause
                  // a name collision with the previously attached node from
                  // the other side of the merge.
                  copy_node_forward(result, new_i->second, left_n, left_side);
                  ++new_i;
                }
              ++left_mi;
              break;
            }

          case parallel::in_right:
            {
              node_t const & right_n = i.right_data();
              // we skip nodes that aren't in the result roster
              if (result.roster.has_node(right_n->self))
                {
                  // attach this node from the right roster. this may cause
                  // a name collision with the previously attached node from
                  // the other side of the merge.
                  copy_node_forward(result, new_i->second, right_n, right_side);
                  ++new_i;
                }
              ++right_mi;
              break;
            }

          case parallel::in_both:
            {
              I(new_i->first == i.left_key());
              I(left_mi->first == i.left_key());
              I(right_mi->first == i.right_key());
              node_t const & left_n = i.left_data();
              marking_t const & left_marking = left_mi->second;
              node_t const & right_n = i.right_data();
              marking_t const & right_marking = right_mi->second;
              node_t const & new_n = new_i->second;
              // merge name
              {
                pair<node_id, path_component> left_name, right_name, new_name;
                multiple_name_conflict conflict(new_n->self);
                left_name = make_pair(left_n->parent, left_n->name);
                right_name = make_pair(right_n->parent, right_n->name);
                if (merge_scalar(left_name,
                                 left_marking.parent_name,
                                 left_uncommon_ancestors,
                                 right_name,
                                 right_marking.parent_name,
                                 right_uncommon_ancestors,
                                 new_name, conflict))
                  {
                    side_t winning_side;

                    if (new_name == left_name)
                      winning_side = left_side;
                    else if (new_name == right_name)
                      winning_side = right_side;
                    else
                      I(false);

                    // attach this node from the winning side of the merge. if
                    // there is a name collision the previously attached node
                    // (which is blocking this one) must come from the other
                    // side of the merge.
                    assign_name(result, new_n->self,
                                new_name.first, new_name.second, winning_side);

                  }
                else
                  {
                    // unsuccessful merge; leave node detached and save
                    // conflict object
                    result.multiple_name_conflicts.push_back(conflict);
                  }
              }
              // if a file, merge content
              if (is_file_t(new_n))
                {
                  file_content_conflict conflict(new_n->self);
                  if (merge_scalar(downcast_to_file_t(left_n)->content,
                                   left_marking.file_content,
                                   left_uncommon_ancestors,
                                   downcast_to_file_t(right_n)->content,
                                   right_marking.file_content,
                                   right_uncommon_ancestors,
                                   downcast_to_file_t(new_n)->content,
                                   conflict))
                    {
                      // successful merge
                    }
                  else
                    {
                      downcast_to_file_t(new_n)->content = file_id();
                      result.file_content_conflicts.push_back(conflict);
                    }
                }
              // merge attributes
              {
                full_attr_map_t::const_iterator left_ai = left_n->attrs.begin();
                full_attr_map_t::const_iterator right_ai = right_n->attrs.begin();
                parallel::iter<full_attr_map_t> attr_i(left_n->attrs,
                                                       right_n->attrs);
                while(attr_i.next())
                {
                  switch (attr_i.state())
                    {
                    case parallel::invalid:
                      I(false);
                    case parallel::in_left:
                      safe_insert(new_n->attrs, attr_i.left_value());
                      break;
                    case parallel::in_right:
                      safe_insert(new_n->attrs, attr_i.right_value());
                      break;
                    case parallel::in_both:
                      pair<bool, attr_value> new_value;
                      attribute_conflict conflict(new_n->self);
                      conflict.key = attr_i.left_key();
                      I(conflict.key == attr_i.right_key());
                      if (merge_scalar(attr_i.left_data(),
                                       safe_get(left_marking.attrs,
                                                attr_i.left_key()),
                                       left_uncommon_ancestors,
                                       attr_i.right_data(),
                                       safe_get(right_marking.attrs,
                                                attr_i.right_key()),
                                       right_uncommon_ancestors,
                                       new_value,
                                       conflict))
                        {
                          // successful merge
                          safe_insert(new_n->attrs,
                                      make_pair(attr_i.left_key(),
                                                     new_value));
                        }
                      else
                        {
                          // unsuccessful merge
                          // leave out the attr entry entirely, and save the
                          // conflict
                          result.attribute_conflicts.push_back(conflict);
                        }
                      break;
                    }

                }
              }
            }
            ++left_mi;
            ++right_mi;
            ++new_i;
            break;
          }
      }
    I(left_mi == left_markings.end());
    I(right_mi == right_markings.end());
    I(new_i == result.roster.all_nodes().end());
  }

  // now check for the possible global problems
  if (!result.roster.has_root())
    result.missing_root_dir = true;
  else
    {
      // we can't have an illegal _MTN dir unless we have a root node in the
      // first place...
      dir_t result_root = result.roster.root();

      if (result_root->has_child(bookkeeping_root_component))
        {
          invalid_name_conflict conflict;
          node_t n = result_root->get_child(bookkeeping_root_component);
          conflict.nid = n->self;
          conflict.parent_name.first = n->parent;
          conflict.parent_name.second = n->name;
          I(n->name == bookkeeping_root_component);

          result.roster.detach_node(n->self);
          result.invalid_name_conflicts.push_back(conflict);
        }
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "constants.hh"
#include "roster_delta.hh"

// cases for testing:
//
// (DONE:)
//
// lifecycle, file and dir
//    alive in both
//    alive in one and unborn in other (left vs. right)
//    alive in one and dead in other (left vs. right)
//
// mark merge:
//   same in both, same mark
//   same in both, diff marks
//   different, left wins with 1 mark
//   different, right wins with 1 mark
//   different, conflict with 1 mark
//   different, left wins with 2 marks
//   different, right wins with 2 marks
//   different, conflict with 1 mark winning, 1 mark losing
//   different, conflict with 2 marks both conflicting
//
// for:
//   node name and parent, file and dir
//   node attr, file and dir
//   file content
//
// attr lifecycle:
//   seen in both -->mark merge cases, above
//   live in one and unseen in other -->live
//   dead in one and unseen in other -->dead
//
// two diff nodes with same name
// directory loops
// orphans
// illegal node ("_MTN")
// missing root dir
//
// (NEEDED:)
//
// interactions:
//   in-node name conflict prevents other problems:
//     in-node name conflict + possible between-node name conflict
//        a vs. b, plus a, b, exist in result
//        left: 1: a
//              2: b
//        right: 1: b
//               3: a
//     in-node name conflict + both possible names orphaned
//        a/foo vs. b/foo conflict, + a, b exist in parents but deleted in
//        children
//        left: 1: a
//              2: a/foo
//        right:
//              3: b
//              2: b/foo
//     in-node name conflict + directory loop conflict
//        a/bottom vs. b/bottom, with a and b both moved inside it
//     in-node name conflict + one name illegal
//        _MTN vs. foo
//   in-node name conflict causes other problems:
//     in-node name conflict + causes missing root dir
//        "" vs. foo and bar vs. ""
//   between-node name conflict prevents other problems:
//     between-node name conflict + both nodes orphaned
//        this is not possible
//     between-node name conflict + both nodes cause loop
//        this is not possible
//     between-node name conflict + both nodes illegal
//        two nodes that both merge to _MTN
//        this is not possible
//   between-node name conflict causes other problems:
//     between-node name conflict + causes missing root dir
//        two nodes that both want ""

typedef enum { scalar_a, scalar_b, scalar_conflict } scalar_val;

template <> void
dump(scalar_val const & v, string & out)
{
  switch (v)
    {
    case scalar_a:
      out = "scalar_a";
      break;
    case scalar_b:
      out = "scalar_b";
      break;
    case scalar_conflict:
      out = "scalar_conflict";
      break;
    }
}

void string_to_set(string const & from, set<revision_id> & to)
{
  to.clear();
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      char label = (*i - '0') << 4 + (*i - '0');
      to.insert(revision_id(string(constants::idlen_bytes, label)));
    }
}


template <typename S> void
test_a_scalar_merge_impl(scalar_val left_val, string const & left_marks_str,
                         string const & left_uncommon_str,
                         scalar_val right_val, string const & right_marks_str,
                         string const & right_uncommon_str,
                         scalar_val expected_outcome)
{
  MM(left_val);
  MM(left_marks_str);
  MM(left_uncommon_str);
  MM(right_val);
  MM(right_marks_str);
  MM(right_uncommon_str);
  MM(expected_outcome);

  S scalar;
  roster_t left_parent, right_parent;
  marking_map left_markings, right_markings;
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  roster_merge_result result;

  set<revision_id> left_marks, right_marks;

  MM(left_parent);
  MM(right_parent);
  MM(left_markings);
  MM(right_markings);
  MM(left_uncommon_ancestors);
  MM(right_uncommon_ancestors);
  MM(left_marks);
  MM(right_marks);
  MM(result);

  string_to_set(left_marks_str, left_marks);
  scalar.setup_parent(left_val, left_marks, left_parent, left_markings);
  string_to_set(right_marks_str, right_marks);
  scalar.setup_parent(right_val, right_marks, right_parent, right_markings);

  string_to_set(left_uncommon_str, left_uncommon_ancestors);
  string_to_set(right_uncommon_str, right_uncommon_ancestors);

  roster_merge(left_parent, left_markings, left_uncommon_ancestors,
               right_parent, right_markings, right_uncommon_ancestors,
               result);

  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_parent, left_markings, right_parent, right_markings);

  scalar.check_result(left_val, right_val, result, expected_outcome);
}

static const revision_id root_rid(string(constants::idlen_bytes, '\0'));
static const file_id arbitrary_file(string(constants::idlen_bytes, '\0'));

struct base_scalar
{
  testing_node_id_source nis;
  node_id root_nid;
  node_id thing_nid;
  base_scalar() : root_nid(nis.next()), thing_nid(nis.next())
  {}

  void
  make_dir(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_dir_node(nid);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_file(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_file_node(arbitrary_file, nid);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    marking.file_content.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_root(roster_t & r, marking_map & markings)
  {
    make_dir("", root_nid, r, markings);
  }
};

struct file_scalar : public virtual base_scalar
{
  file_path thing_name;
  file_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_file("thing", thing_nid, r, markings);
  }
};

struct dir_scalar : public virtual base_scalar
{
  file_path thing_name;
  dir_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_dir("thing", thing_nid, r, markings);
  }
};

struct name_shared_stuff : public virtual base_scalar
{
  virtual file_path path_for(scalar_val val) = 0;
  path_component pc_for(scalar_val val)
  {
    return path_for(val).basename();
  }
  virtual node_id parent_for(scalar_val val) = 0;

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        {
          file_path fp;
          result.roster.get_name(thing_nid, fp);
          I(fp == path_for(expected_val));
        }
        break;
      case scalar_conflict:
        multiple_name_conflict const & c = idx(result.multiple_name_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.left == make_pair(parent_for(left_val), pc_for(left_val)));
        I(c.right == make_pair(parent_for(right_val), pc_for(right_val)));
        I(null_node(result.roster.get_node(thing_nid)->parent));
        I(result.roster.get_node(thing_nid)->name.empty());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        // attach implicitly checks that we were already detached
        result.roster.attach_node(thing_nid, file_path_internal("thing"));
        result.multiple_name_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }

  virtual ~name_shared_stuff() {};
};

template <typename T>
struct basename_scalar : public name_shared_stuff, public T
{
  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a" : "b");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return root_nid;
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~basename_scalar() {}
};

template <typename T>
struct parent_scalar : public virtual name_shared_stuff, public T
{
  node_id a_dir_nid, b_dir_nid;
  parent_scalar() : a_dir_nid(nis.next()), b_dir_nid(nis.next())
  {}

  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a/thing" : "b/thing");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return ((val == scalar_a) ? a_dir_nid : b_dir_nid);
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    make_dir("a", a_dir_nid, r, markings);
    make_dir("b", b_dir_nid, r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~parent_scalar() {}
};

template <typename T>
struct attr_scalar : public virtual base_scalar, public T
{
  attr_value attr_value_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return attr_value((val == scalar_a) ? "a" : "b");
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.set_attr(this->T::thing_name, attr_key("test_key"), attr_value_for(val));
    markings.find(thing_nid)->second.attrs[attr_key("test_key")] = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(result.roster.get_node(thing_nid)->attrs[attr_key("test_key")]
          == make_pair(true, attr_value_for(expected_val)));
        break;
      case scalar_conflict:
        attribute_conflict const & c = idx(result.attribute_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.key == attr_key("test_key"));
        I(c.left == make_pair(true, attr_value_for(left_val)));
        I(c.right == make_pair(true, attr_value_for(right_val)));
        full_attr_map_t const & attrs = result.roster.get_node(thing_nid)->attrs;
        I(attrs.find(attr_key("test_key")) == attrs.end());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        result.roster.set_attr(this->T::thing_name, attr_key("test_key"),
                               attr_value("conflict -- RESOLVED"));
        result.attribute_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

struct file_content_scalar : public virtual file_scalar
{
  file_id content_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_id(string(constants::idlen_bytes,
                          (val == scalar_a) ? '\xaa' : '\xbb'));
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    make_thing(r, markings);
    downcast_to_file_t(r.get_node(thing_name))->content = content_for(val);
    markings.find(thing_nid)->second.file_content = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(downcast_to_file_t(result.roster.get_node(thing_nid))->content
          == content_for(expected_val));
        break;
      case scalar_conflict:
        file_content_conflict const & c = idx(result.file_content_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.left == content_for(left_val));
        I(c.right == content_for(right_val));
        file_id & content = downcast_to_file_t(result.roster.get_node(thing_nid))->content;
        I(null_id(content));
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        content = file_id(string(constants::idlen_bytes, '\xff'));
        result.file_content_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

void
test_a_scalar_merge(scalar_val left_val, string const & left_marks_str,
                    string const & left_uncommon_str,
                    scalar_val right_val, string const & right_marks_str,
                    string const & right_uncommon_str,
                    scalar_val expected_outcome)
{
  test_a_scalar_merge_impl<basename_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                          right_val, right_marks_str, right_uncommon_str,
                                                          expected_outcome);
  test_a_scalar_merge_impl<basename_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                         right_val, right_marks_str, right_uncommon_str,
                                                         expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                        right_val, right_marks_str, right_uncommon_str,
                                                        expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                       right_val, right_marks_str, right_uncommon_str,
                                                       expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                      right_val, right_marks_str, right_uncommon_str,
                                                      expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                     right_val, right_marks_str, right_uncommon_str,
                                                     expected_outcome);
  test_a_scalar_merge_impl<file_content_scalar>(left_val, left_marks_str, left_uncommon_str,
                                                right_val, right_marks_str, right_uncommon_str,
                                                expected_outcome);
}

UNIT_TEST(roster_merge, scalar_merges)
{
  // Notation: a1* means, "value is a, this is node 1 in the graph, it is
  // marked".  ".2" means, "value is unimportant and different from either a
  // or b, this is node 2 in the graph, it is not marked".
  //
  // Backslashes with dots after them mean, the C++ line continuation rules
  // are annoying when it comes to drawing ascii graphs -- the dot is only to
  // stop the backslash from having special meaning to the parser.  So just
  // ignore them :-).

  //   same in both, same mark
  //               a1*
  //              / \.
  //             a2  a3
  test_a_scalar_merge(scalar_a, "1", "2", scalar_a, "1", "3", scalar_a);

  //   same in both, diff marks
  //               .1*
  //              / \.
  //             a2* a3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_a, "3", "3", scalar_a);

  //   different, left wins with 1 mark
  //               a1*
  //              / \.
  //             b2* a3
  test_a_scalar_merge(scalar_b, "2", "2", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 1 mark
  //               a1*
  //              / \.
  //             a2  b3*
   test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "3", "3", scalar_b);

  //   different, conflict with 1 mark
  //               .1*
  //              / \.
  //             a2* b3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "3", "3", scalar_conflict);

  //   different, left wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "45", "3456", scalar_b);

  //   different, conflict with 1 mark winning, 1 mark losing
  //               .1*
  //              / \.
  //             a2* a3*
  //              \ / \.
  //               a4  b5*
  test_a_scalar_merge(scalar_a, "23", "24", scalar_b, "5", "5", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* a3*
  //            / \ /
  //           b4* a5
  test_a_scalar_merge(scalar_b, "4", "4", scalar_a, "23", "35", scalar_conflict);

  //   different, conflict with 2 marks both conflicting
  //
  //               .1*
  //              / \.
  //             .2  a3*
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "3", "3", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* .3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "45", "3456", scalar_conflict);

  //
  //               _.1*_
  //              /     \.
  //             .2      .3
  //            / \     / \.
  //           a4* a5* b6* b7*
  //            \ /     \ /
  //             a8      b9
  test_a_scalar_merge(scalar_a, "45", "2458", scalar_b, "67", "3679", scalar_conflict);
}

namespace
{
  const revision_id a_uncommon1(string(constants::idlen_bytes, '\xaa'));
  const revision_id a_uncommon2(string(constants::idlen_bytes, '\xbb'));
  const revision_id b_uncommon1(string(constants::idlen_bytes, '\xcc'));
  const revision_id b_uncommon2(string(constants::idlen_bytes, '\xdd'));
  const revision_id common1(string(constants::idlen_bytes, '\xee'));
  const revision_id common2(string(constants::idlen_bytes, '\xff'));

  const file_id fid1(string(constants::idlen_bytes, '\x11'));
  const file_id fid2(string(constants::idlen_bytes, '\x22'));
}

static void
make_dir(roster_t & r, marking_map & markings,
         revision_id const & birth_rid, revision_id const & parent_name_rid,
         string const & name, node_id nid)
{
  r.create_dir_node(nid);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_file(roster_t & r, marking_map & markings,
          revision_id const & birth_rid, revision_id const & parent_name_rid,
          revision_id const & file_content_rid,
          string const & name, file_id const & content,
          node_id nid)
{
  r.create_file_node(content, nid);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  marking.file_content.insert(file_content_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_node_lifecycle_objs(roster_t & r, marking_map & markings, revision_id const & uncommon,
                         string const & name, node_id common_dir_nid, node_id common_file_nid,
                         node_id & safe_dir_nid, node_id & safe_file_nid, node_id_source & nis)
{
  make_dir(r, markings, common1, common1, "common_old_dir", common_dir_nid);
  make_file(r, markings, common1, common1, common1, "common_old_file", fid1, common_file_nid);
  safe_dir_nid = nis.next();
  make_dir(r, markings, uncommon, uncommon, name + "_safe_dir", safe_dir_nid);
  safe_file_nid = nis.next();
  make_file(r, markings, uncommon, uncommon, uncommon, name + "_safe_file", fid1, safe_file_nid);
  make_dir(r, markings, common1, common1, name + "_dead_dir", nis.next());
  make_file(r, markings, common1, common1, common1, name + "_dead_file", fid1, nis.next());
}

UNIT_TEST(roster_merge, node_lifecycle)
{
  roster_t a_roster, b_roster;
  marking_map a_markings, b_markings;
  set<revision_id> a_uncommon, b_uncommon;
  // boilerplate to get uncommon revision sets...
  a_uncommon.insert(a_uncommon1);
  a_uncommon.insert(a_uncommon2);
  b_uncommon.insert(b_uncommon1);
  b_uncommon.insert(b_uncommon2);
  testing_node_id_source nis;
  // boilerplate to set up a root node...
  {
    node_id root_nid = nis.next();
    make_dir(a_roster, a_markings, common1, common1, "", root_nid);
    make_dir(b_roster, b_markings, common1, common1, "", root_nid);
  }
  // create some nodes on each side
  node_id common_dir_nid = nis.next();
  node_id common_file_nid = nis.next();
  node_id a_safe_dir_nid, a_safe_file_nid, b_safe_dir_nid, b_safe_file_nid;
  make_node_lifecycle_objs(a_roster, a_markings, a_uncommon1, "a", common_dir_nid, common_file_nid,
                           a_safe_dir_nid, a_safe_file_nid, nis);
  make_node_lifecycle_objs(b_roster, b_markings, b_uncommon1, "b", common_dir_nid, common_file_nid,
                           b_safe_dir_nid, b_safe_file_nid, nis);
  // do the merge
  roster_merge_result result;
  roster_merge(a_roster, a_markings, a_uncommon, b_roster, b_markings, b_uncommon, result);
  I(result.is_clean());
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(a_roster, a_markings, b_roster, b_markings);
  // 7 = 1 root + 2 common + 2 safe a + 2 safe b
  I(result.roster.all_nodes().size() == 7);
  // check that they're the right ones...
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  a_roster.get_node(common_dir_nid), false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  a_roster.get_node(common_file_nid), false));
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  b_roster.get_node(common_dir_nid), false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  b_roster.get_node(common_file_nid), false));
  I(shallow_equal(result.roster.get_node(a_safe_dir_nid),
                  a_roster.get_node(a_safe_dir_nid), false));
  I(shallow_equal(result.roster.get_node(a_safe_file_nid),
                  a_roster.get_node(a_safe_file_nid), false));
  I(shallow_equal(result.roster.get_node(b_safe_dir_nid),
                  b_roster.get_node(b_safe_dir_nid), false));
  I(shallow_equal(result.roster.get_node(b_safe_file_nid),
                  b_roster.get_node(b_safe_file_nid), false));
}

UNIT_TEST(roster_merge, attr_lifecycle)
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  MM(left_roster);
  MM(left_markings);
  MM(right_roster);
  MM(right_markings);
  set<revision_id> old_revs, left_revs, right_revs;
  string_to_set("0", old_revs);
  string_to_set("1", left_revs);
  string_to_set("2", right_revs);
  revision_id old_rid = *old_revs.begin();
  testing_node_id_source nis;
  node_id dir_nid = nis.next();
  make_dir(left_roster, left_markings, old_rid, old_rid, "", dir_nid);
  make_dir(right_roster, right_markings, old_rid, old_rid, "", dir_nid);
  node_id file_nid = nis.next();
  make_file(left_roster, left_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);
  make_file(right_roster, right_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);

  // put one live and one dead attr on each thing on each side, with uncommon
  // marks on them
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_dead"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_dead"), left_revs));

  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_dead"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_dead"), right_revs));

  roster_merge_result result;
  MM(result);
  roster_merge(left_roster, left_markings, left_revs,
               right_roster, right_markings, right_revs,
               result);
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);
  I(result.roster.all_nodes().size() == 2);
  I(result.roster.get_node(dir_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(result.roster.get_node(file_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
}

struct structural_conflict_helper
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  set<revision_id> old_revs, left_revs, right_revs;
  revision_id old_rid, left_rid, right_rid;
  testing_node_id_source nis;
  node_id root_nid;
  roster_merge_result result;

  virtual void setup() = 0;
  virtual void check() = 0;

  void test()
  {
    MM(left_roster);
    MM(left_markings);
    MM(right_roster);
    MM(right_markings);
    string_to_set("0", old_revs);
    string_to_set("1", left_revs);
    string_to_set("2", right_revs);
    old_rid = *old_revs.begin();
    left_rid = *left_revs.begin();
    right_rid = *right_revs.begin();
    root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "", root_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "", root_nid);

    setup();

    MM(result);
    roster_merge(left_roster, left_markings, left_revs,
                 right_roster, right_markings, right_revs,
                 result);
    // go ahead and check the roster_delta code too, while we're at it...
    test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);

    check();
  }

  virtual ~structural_conflict_helper() {}
};

// two diff nodes with same name
struct simple_duplicate_name_conflict : public structural_conflict_helper
{
  node_id left_nid, right_nid;
  virtual void setup()
  {
    left_nid = nis.next();
    make_dir(left_roster, left_markings, left_rid, left_rid, "thing", left_nid);
    right_nid = nis.next();
    make_dir(right_roster, right_markings, right_rid, right_rid, "thing", right_nid);
  }

  virtual void check()
  {
    I(!result.is_clean());
    duplicate_name_conflict const & c = idx(result.duplicate_name_conflicts, 0);
    I(c.left_nid == left_nid && c.right_nid == right_nid);
    I(c.parent_name == make_pair(root_nid, path_component("thing")));
    // this tests that they were detached, implicitly
    result.roster.attach_node(left_nid, file_path_internal("left"));
    result.roster.attach_node(right_nid, file_path_internal("right"));
    result.duplicate_name_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

// directory loops
struct simple_dir_loop_conflict : public structural_conflict_helper
{
  node_id left_top_nid, right_top_nid;

  virtual void setup()
    {
      left_top_nid = nis.next();
      right_top_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "top", left_top_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "top/bottom", right_top_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "top", right_top_nid);
      make_dir(right_roster, right_markings, old_rid, right_rid, "top/bottom", left_top_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      directory_loop_conflict const & c = idx(result.directory_loop_conflicts, 0);
      I((c.nid == left_top_nid && c.parent_name == make_pair(right_top_nid, path_component("bottom")))
        || (c.nid == right_top_nid && c.parent_name == make_pair(left_top_nid, path_component("bottom"))));
      // this tests it was detached, implicitly
      result.roster.attach_node(c.nid, file_path_internal("resolved"));
      result.directory_loop_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// orphans
struct simple_orphan_conflict : public structural_conflict_helper
{
  node_id a_dead_parent_nid, a_live_child_nid, b_dead_parent_nid, b_live_child_nid;

  // in ancestor, both parents are alive
  // in left, a_dead_parent is dead, and b_live_child is created
  // in right, b_dead_parent is dead, and a_live_child is created

  virtual void setup()
    {
      a_dead_parent_nid = nis.next();
      a_live_child_nid = nis.next();
      b_dead_parent_nid = nis.next();
      b_live_child_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "b_parent", b_dead_parent_nid);
      make_dir(left_roster, left_markings, left_rid, left_rid, "b_parent/b_child", b_live_child_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "a_parent", a_dead_parent_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "a_parent/a_child", a_live_child_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.orphaned_node_conflicts.size() == 2);
      orphaned_node_conflict a, b;
      if (idx(result.orphaned_node_conflicts, 0).nid == a_live_child_nid)
        {
          a = idx(result.orphaned_node_conflicts, 0);
          b = idx(result.orphaned_node_conflicts, 1);
        }
      else
        {
          a = idx(result.orphaned_node_conflicts, 1);
          b = idx(result.orphaned_node_conflicts, 0);
        }
      I(a.nid == a_live_child_nid);
      I(a.parent_name == make_pair(a_dead_parent_nid, path_component("a_child")));
      I(b.nid == b_live_child_nid);
      I(b.parent_name == make_pair(b_dead_parent_nid, path_component("b_child")));
      // this tests it was detached, implicitly
      result.roster.attach_node(a.nid, file_path_internal("resolved_a"));
      result.roster.attach_node(b.nid, file_path_internal("resolved_b"));
      result.orphaned_node_conflicts.pop_back();
      result.orphaned_node_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// illegal node ("_MTN")
struct simple_invalid_name_conflict : public structural_conflict_helper
{
  node_id new_root_nid, bad_dir_nid;

  // in left, new_root is the root (it existed in old, but was renamed in left)
  // in right, new_root is still a subdir, the old root still exists, and a
  // new dir has been created

  virtual void setup()
    {
      new_root_nid = nis.next();
      bad_dir_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "", new_root_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "root_to_be", new_root_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "root_to_be/_MTN", bad_dir_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      invalid_name_conflict const & c = idx(result.invalid_name_conflicts, 0);
      I(c.nid == bad_dir_nid);
      I(c.parent_name == make_pair(new_root_nid, bookkeeping_root_component));
      // this tests it was detached, implicitly
      result.roster.attach_node(bad_dir_nid, file_path_internal("dir_formerly_known_as__MTN"));
      result.invalid_name_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// missing root dir
struct simple_missing_root_dir : public structural_conflict_helper
{
  node_id other_root_nid;

  // left and right each have different root nodes, and each has deleted the
  // other's root node

  virtual void setup()
    {
      other_root_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, old_rid, "", other_root_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.missing_root_dir);
      result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
      result.missing_root_dir = false;
      I(result.is_clean());
      result.roster.check_sane();
    }
};

UNIT_TEST(roster_merge, simple_structural_conflicts)
{
  {
    simple_duplicate_name_conflict t;
    t.test();
  }
  {
    simple_dir_loop_conflict t;
    t.test();
  }
  {
    simple_orphan_conflict t;
    t.test();
  }
  {
    simple_invalid_name_conflict t;
    t.test();
  }
  {
    simple_missing_root_dir t;
    t.test();
  }
}

struct multiple_name_plus_helper : public structural_conflict_helper
{
  node_id name_conflict_nid;
  node_id left_parent, right_parent;
  path_component left_name, right_name;
  void make_multiple_name_conflict(string const & left, string const & right)
  {
    file_path left_path = file_path_internal(left);
    file_path right_path = file_path_internal(right);
    name_conflict_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, left, name_conflict_nid);
    left_parent = left_roster.get_node(left_path)->parent;
    left_name = left_roster.get_node(left_path)->name;
    make_dir(right_roster, right_markings, old_rid, right_rid, right, name_conflict_nid);
    right_parent = right_roster.get_node(right_path)->parent;
    right_name = right_roster.get_node(right_path)->name;
  }
  void check_multiple_name_conflict()
  {
    I(!result.is_clean());
    multiple_name_conflict const & c = idx(result.multiple_name_conflicts, 0);
    I(c.nid == name_conflict_nid);
    I(c.left == make_pair(left_parent, left_name));
    I(c.right == make_pair(right_parent, right_name));
    result.roster.attach_node(name_conflict_nid, file_path_internal("totally_other_name"));
    result.multiple_name_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct multiple_name_plus_duplicate_name : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_multiple_name_conflict("a", "b");
    make_dir(left_roster, left_markings, left_rid, left_rid, "b", b_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "a", a_nid);
  }

  virtual void check()
  {
    // there should just be a single conflict on name_conflict_nid, and a and
    // b should have landed fine
    I(result.roster.get_node(file_path_internal("a"))->self == a_nid);
    I(result.roster.get_node(file_path_internal("b"))->self == b_nid);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_orphan : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b", b_nid);
    make_multiple_name_conflict("a/foo", "b/foo");
  }

  virtual void check()
  {
    I(result.roster.all_nodes().size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_directory_loop : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "b", b_nid);
    make_multiple_name_conflict("a/foo", "b/foo");
    make_dir(left_roster, left_markings, old_rid, left_rid, "a/foo/b", b_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b/foo/a", a_nid);
  }

  virtual void check()
  {
    I(downcast_to_dir_t(result.roster.get_node(name_conflict_nid))->children.size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_invalid_name : public multiple_name_plus_helper
{
  node_id new_root_nid;

  virtual void setup()
  {
    new_root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "new_root", new_root_nid);
    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", new_root_nid);
    make_multiple_name_conflict("new_root/_MTN", "foo");
  }

  virtual void check()
  {
    I(result.roster.root()->self == new_root_nid);
    I(result.roster.all_nodes().size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "", left_root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "right_root", right_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", right_root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "left_root", left_root_nid);
  }
  void check_helper(multiple_name_conflict const & left_c,
                    multiple_name_conflict const & right_c)
  {
    I(left_c.nid == left_root_nid);
    I(left_c.left == make_pair(the_null_node, path_component()));
    I(left_c.right == make_pair(right_root_nid, path_component("left_root")));

    I(right_c.nid == right_root_nid);
    I(right_c.left == make_pair(left_root_nid, path_component("right_root")));
    I(right_c.right == make_pair(the_null_node, path_component()));
  }
  virtual void check()
  {
    I(!result.is_clean());
    I(result.multiple_name_conflicts.size() == 2);

    if (idx(result.multiple_name_conflicts, 0).nid == left_root_nid)
      check_helper(idx(result.multiple_name_conflicts, 0),
                   idx(result.multiple_name_conflicts, 1));
    else
      check_helper(idx(result.multiple_name_conflicts, 1),
                   idx(result.multiple_name_conflicts, 0));

    I(result.missing_root_dir);

    result.roster.attach_node(left_root_nid, file_path());
    result.roster.attach_node(right_root_nid, file_path_internal("totally_other_name"));
    result.multiple_name_conflicts.pop_back();
    result.multiple_name_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct duplicate_name_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, left_rid, left_rid, "", left_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "", right_root_nid);
  }
  virtual void check()
  {
    I(!result.is_clean());
    duplicate_name_conflict const & c = idx(result.duplicate_name_conflicts, 0);
    I(c.left_nid == left_root_nid && c.right_nid == right_root_nid);
    I(c.parent_name == make_pair(the_null_node, path_component()));

    I(result.missing_root_dir);

    // we can't just attach one of these as the root -- see the massive
    // comment on the old_locations member of roster_t, in roster.hh.
    result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
    result.roster.attach_node(left_root_nid, file_path_internal("totally_left_name"));
    result.roster.attach_node(right_root_nid, file_path_internal("totally_right_name"));
    result.duplicate_name_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

UNIT_TEST(roster_merge, complex_structural_conflicts)
{
  {
    multiple_name_plus_duplicate_name t;
    t.test();
  }
  {
    multiple_name_plus_orphan t;
    t.test();
  }
  {
    multiple_name_plus_directory_loop t;
    t.test();
  }
  {
    multiple_name_plus_invalid_name t;
    t.test();
  }
  {
    multiple_name_plus_missing_root t;
    t.test();
  }
  {
    duplicate_name_plus_missing_root t;
    t.test();
  }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
