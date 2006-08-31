// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <sstream>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <queue>

#include "work.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "localized_file_io.hh"
#include "platform-wrapped.hh"
#include "restrictions.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "simplestring_xform.hh"
#include "revision.hh"
#include "inodeprint.hh"
#include "diff_patch.hh"
#include "ui.hh"

using std::deque;
using std::exception;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

using boost::lexical_cast;

// workspace / book-keeping file code

static string const attr_file_name(".mt-attrs");
static string const inodeprints_file_name("inodeprints");
static string const local_dump_file_name("debug");
static string const options_file_name("options");
static string const user_log_file_name("log");
static string const revision_file_name("revision");

static void
get_revision_path(bookkeeping_path & m_path)
{
  m_path = bookkeeping_root / revision_file_name;
  L(FL("revision path is %s") % m_path);
}

static void
get_options_path(bookkeeping_path & o_path)
{
  o_path = bookkeeping_root / options_file_name;
  L(FL("options path is %s") % o_path);
}

static void
get_inodeprints_path(bookkeeping_path & ip_path)
{
  ip_path = bookkeeping_root / inodeprints_file_name;
  L(FL("inodeprints path is %s") % ip_path);
}

// routines for manipulating the bookkeeping directory

// revision file contains a partial revision describing the workspace
static void
get_work_rev(revision_t & rev)
{
  bookkeeping_path rev_path;
  get_revision_path(rev_path);
  data rev_data;
  MM(rev_data);
  try
    {
      read_data(rev_path, rev_data);
    }
  catch(exception & e)
    {
      E(false, F("workspace is corrupt: reading %s: %s")
        % rev_path % e.what());
    }

  read_revision(rev_data, rev);
  // Currently the revision must have only one ancestor.
  I(rev.edges.size() == 1);
}

void
workspace::put_work_rev(revision_t const & rev)
{
  // Currently the revision must have only one ancestor.
  MM(rev);
  I(rev.edges.size() == 1);
  rev.check_sane();

  data rev_data;
  write_revision(rev, rev_data);

  bookkeeping_path rev_path;
  get_revision_path(rev_path);
  write_data(rev_path, rev_data);
}


// work file containing rearrangement from uncommitted adds/drops/renames
void
workspace::get_work_cset(cset & w)
{
  revision_t rev;
  get_work_rev(rev);

  w = edge_changes(rev.edges.begin());
}

// base revision ID
void
workspace::get_revision_id(revision_id & c)
{
  revision_t rev;
  get_work_rev(rev);
  c = edge_old_revision(rev.edges.begin());
}

// structures derived from the work revision, the database, and possibly
// the workspace
void
workspace::get_base_revision(revision_id & rid,
                             roster_t & ros,
                             marking_map & mm)
{
  get_revision_id(rid);

  if (!null_id(rid))
    {

      N(db.revision_exists(rid),
        F("base revision %s does not exist in database") % rid);

      db.get_roster(rid, ros, mm);
    }

  L(FL("base roster has %d entries") % ros.all_nodes().size());
}

void
workspace::get_base_revision(revision_id & rid,
                             roster_t & ros)
{
  marking_map mm;
  get_base_revision(rid, ros, mm);
}

void
workspace::get_base_roster(roster_t & ros)
{
  revision_id rid;
  marking_map mm;
  get_base_revision(rid, ros, mm);
}

void
workspace::get_current_roster_shape(roster_t & ros, node_id_source & nis)
{
  get_base_roster(ros);
  cset cs;
  get_work_cset(cs);
  editable_roster_base er(ros, nis);
  cs.apply_to(er);
}

void
workspace::get_base_and_current_roster_shape(roster_t & base_roster,
                                             roster_t & current_roster,
                                             node_id_source & nis)
{
  get_base_roster(base_roster);
  current_roster = base_roster;
  cset cs;
  get_work_cset(cs);
  editable_roster_base er(current_roster, nis);
  cs.apply_to(er);
}

// user log file

void
workspace::get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(FL("user log path is %s") % ul_path);
}

void
workspace::read_user_log(data & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  if (file_exists(ul_path))
    {
      read_data(ul_path, dat);
    }
}

void
workspace::write_user_log(data const & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  write_data(ul_path, dat);
}

void
workspace::blank_user_log()
{
  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);
}

bool
workspace::has_contents_user_log()
{
  data user_log_message;
  read_user_log(user_log_message);
  return user_log_message().length() > 0;
}

// _MTN/options handling.

void
workspace::get_ws_options(utf8 & database_option,
                          utf8 & branch_option,
                          utf8 & key_option,
                          utf8 & keydir_option)
{
  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      if (path_exists(o_path))
        {
          data dat;
          read_data(o_path, dat);

          basic_io::input_source src(dat(), o_path.as_external());
          basic_io::tokenizer tok(src);
          basic_io::parser parser(tok);

          while (parser.symp())
            {
              string opt, val;
              parser.sym(opt);
              parser.str(val);

              if (opt == "database")
                database_option = val;
              else if (opt == "branch")
                branch_option = val;
              else if (opt == "key")
                key_option = val;
              else if (opt == "keydir")
                keydir_option =val;
              else
                W(F("unrecognized key '%s' in options file %s - ignored")
                  % opt % o_path);
            }
        }
    }
  catch(exception & e)
    {
      W(F("Failed to read options file %s: %s") % o_path % e.what());
    }
}

void
workspace::set_ws_options(utf8 & database_option,
                          utf8 & branch_option,
                          utf8 & key_option,
                          utf8 & keydir_option)
{
  // If caller passes an empty string for any of the incoming options,
  // we want to leave that option as is in _MTN/options, not write out
  // an empty option.
  utf8 old_database_option, old_branch_option;
  utf8 old_key_option, old_keydir_option;
  get_ws_options(old_database_option, old_branch_option,
                 old_key_option, old_keydir_option);

  if (database_option().empty())
    database_option = old_database_option;
  if (branch_option().empty())
    branch_option = old_branch_option;
  if (key_option().empty())
    key_option = old_key_option;
  if (keydir_option().empty())
    keydir_option = old_keydir_option;

  basic_io::stanza st;
  if (!database_option().empty())
    st.push_str_pair(string("database"), database_option());
  if (!branch_option().empty())
    st.push_str_pair(string("branch"), branch_option());
  if (!key_option().empty())
    st.push_str_pair(string("key"), key_option());
  if (!keydir_option().empty())
    st.push_str_pair(string("keydir"), keydir_option());

  basic_io::printer pr;
  pr.print_stanza(st);

  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      write_data(o_path, pr.buf);
    }
  catch(exception & e)
    {
      W(F("Failed to write options file %s: %s") % o_path % e.what());
    }
}

// local dump file

void
workspace::get_local_dump_path(bookkeeping_path & d_path)
{
  d_path = bookkeeping_root / local_dump_file_name;
  L(FL("local dump path is %s") % d_path);
}

// inodeprint file

static bool
in_inodeprints_mode()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return file_exists(ip_path);
}

static void
read_inodeprints(data & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  read_data(ip_path, dat);
}

static void
write_inodeprints(data const & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  write_data(ip_path, dat);
}

void
workspace::enable_inodeprints()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  data dat;
  write_data(ip_path, dat);
}

void
workspace::maybe_update_inodeprints()
{
  if (!in_inodeprints_mode())
    return;

  inodeprint_map ipm_new;
  temp_node_id_source nis;
  roster_t old_roster, new_roster;

  get_base_and_current_roster_shape(old_roster, new_roster, nis);
  update_current_roster_from_filesystem(new_roster);

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      node_id nid = i->first;
      if (old_roster.has_node(nid))
        {
          node_t old_node = old_roster.get_node(nid);
          if (is_file_t(old_node))
            {
              node_t new_node = i->second;
              I(is_file_t(new_node));

              file_t old_file = downcast_to_file_t(old_node);
              file_t new_file = downcast_to_file_t(new_node);

              if (new_file->content == old_file->content)
                {
                  split_path sp;
                  new_roster.get_name(nid, sp);
                  file_path fp(sp);
                  hexenc<inodeprint> ip;
                  if (inodeprint_file(fp, ip))
                    ipm_new.insert(inodeprint_entry(fp, ip));
                }
            }
        }
    }
  data dat;
  write_inodeprint_map(ipm_new, dat);
  write_inodeprints(dat);
}

// objects and routines for manipulating the workspace itself
namespace {

struct file_itemizer : public tree_walker
{
  database & db;
  lua_hooks & lua;
  path_set & known;
  path_set & unknown;
  path_set & ignored;
  path_restriction const & mask;
  file_itemizer(database & db, lua_hooks & lua,
                path_set & k, path_set & u, path_set & i, 
                path_restriction const & r)
    : db(db), lua(lua), known(k), unknown(u), ignored(i), mask(r) {}
  virtual void visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

void
file_itemizer::visit_dir(file_path const & path)
{
  this->visit_file(path);
}

void
file_itemizer::visit_file(file_path const & path)
{
  split_path sp;
  path.split(sp);

  if (mask.includes(sp) && known.find(sp) == known.end())
    {
      if (lua.hook_ignore_file(path) || db.is_dbfile(path))
        ignored.insert(sp);
      else
        unknown.insert(sp);
    }
}

class
addition_builder
  : public tree_walker
{
  database & db;
  lua_hooks & lua;
  roster_t & ros;
  editable_roster_base & er;
public:
  addition_builder(database & db, lua_hooks & lua,
                   roster_t & r, editable_roster_base & e)
    : db(db), lua(lua), ros(r), er(e)
  {}
  virtual void visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
  void add_node_for(split_path const & sp);
};

void
addition_builder::add_node_for(split_path const & sp)
{
  file_path path(sp);

  node_id nid = the_null_node;
  switch (get_path_status(path))
    {
    case path::nonexistent:
      return;
    case path::file:
      {
        file_id ident;
        I(ident_existing_file(path, ident, lua));
        nid = er.create_file_node(ident);
      }
      break;
    case path::directory:
      nid = er.create_dir_node();
      break;
    }

  I(nid != the_null_node);
  er.attach_node(nid, sp);

  map<string, string> attrs;
  lua.hook_init_attributes(path, attrs);
  if (attrs.size() > 0)
    {
      for (map<string, string>::const_iterator i = attrs.begin();
           i != attrs.end(); ++i)
        er.set_attr(sp, attr_key(i->first), attr_value(i->second));
    }
}


void
addition_builder::visit_dir(file_path const & path)
{
  this->visit_file(path);
}

void
addition_builder::visit_file(file_path const & path)
{
  if (lua.hook_ignore_file(path) || db.is_dbfile(path))
    {
      P(F("skipping ignorable file %s") % path);
      return;
    }

  split_path sp;
  path.split(sp);
  if (ros.has_node(sp))
    {
      if (sp.size() > 1)
        P(F("skipping %s, already accounted for in workspace") % path);
      return;
    }

  split_path prefix;
  I(ros.has_root());
  for (split_path::const_iterator i = sp.begin(); i != sp.end(); ++i)
    {
      prefix.push_back(*i);
      if (!ros.has_node(prefix))
        {
          P(F("adding %s to workspace manifest") % file_path(prefix));
          add_node_for(prefix);
        }
    }
}

struct editable_working_tree : public editable_tree
{
  editable_working_tree(lua_hooks & lua, content_merge_adaptor const & source) 
    : lua(lua), source(source), next_nid(1), root_dir_attached(true)
  {};

  virtual node_id detach_node(split_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, split_path const & dst);

  virtual void apply_delta(split_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id);
  virtual void clear_attr(split_path const & pth,
                          attr_key const & name);
  virtual void set_attr(split_path const & pth,
                        attr_key const & name,
                        attr_value const & val);

  virtual void commit();

  virtual ~editable_working_tree();
private:
  lua_hooks & lua;
  content_merge_adaptor const & source;
  node_id next_nid;
  std::map<bookkeeping_path, file_id> written_content;
  std::map<bookkeeping_path, file_path> rename_add_drop_map;
  bool root_dir_attached;
};


struct content_merge_empty_adaptor : public content_merge_adaptor
{
  virtual void get_version(file_path const &, 
                           file_id const &, file_data &) const
  { I(false); }
  virtual void record_merge(file_id const &, file_id const &,
                            file_id const &, file_data const &,
                            file_data const &)
  { I(false); }
  virtual void get_ancestral_roster(node_id, boost::shared_ptr<roster_t> &)
  { I(false); }
};

// editable_working_tree implementation

static inline bookkeeping_path
path_for_nid(node_id nid)
{
  return bookkeeping_root / "tmp" / lexical_cast<string>(nid);
}

// Attaching/detaching the root directory:
//   This is tricky, because we don't want to simply move it around, like
// other directories.  That would require some very snazzy handling of the
// _MTN directory, and never be possible on windows anyway[1].  So, what we do
// is fake it -- whenever we want to move the root directory into the
// temporary dir, we instead create a new dir in the temporary dir, move
// all of the root's contents into this new dir, and make a note that the root
// directory is logically non-existent.  Whenever we want to move some
// directory out of the temporary dir and onto the root directory, we instead
// check that the root is logically nonexistent, move its contents, and note
// that it exists again.
//
// [1] Because the root directory is our working directory, and thus locked in
// place.  We _could_ chdir out, then move _MTN out, then move the real root
// directory into our newly-moved _MTN, etc., but aside from being very finicky,
// this would require that we know our root directory's name relative to its
// parent.

node_id
editable_working_tree::detach_node(split_path const & src)
{
  I(root_dir_attached);
  node_id nid = next_nid++;
  file_path src_pth(src);
  bookkeeping_path dst_pth = path_for_nid(nid);
  safe_insert(rename_add_drop_map, make_pair(dst_pth, src_pth));
  make_dir_for(dst_pth);
  if (src_pth == file_path())
    {
      // root dir detach, so we move contents, rather than the dir itself
      mkdir_p(dst_pth);
      vector<utf8> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<utf8>::const_iterator i = files.begin(); i != files.end(); ++i)
        move_file(src_pth / (*i)(), dst_pth / (*i)());
      for (vector<utf8>::const_iterator i = dirs.begin(); i != dirs.end(); ++i)
        if (!bookkeeping_path::is_bookkeeping_path((*i)()))
          move_dir(src_pth / (*i)(), dst_pth / (*i)());
      root_dir_attached = false;
    }
  else
    move_path(src_pth, dst_pth);
  return nid;
}

void
editable_working_tree::drop_detached_node(node_id nid)
{
  bookkeeping_path pth = path_for_nid(nid);
  map<bookkeeping_path, file_path>::const_iterator i
    = rename_add_drop_map.find(pth);
  I(i != rename_add_drop_map.end());
  P(F("dropping %s") % i->second);
  safe_erase(rename_add_drop_map, pth);
  delete_file_or_dir_shallow(pth);
}

node_id
editable_working_tree::create_dir_node()
{
  node_id nid = next_nid++;
  bookkeeping_path pth = path_for_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path %s already exists") % pth);
  mkdir_p(pth);
  return nid;
}

node_id
editable_working_tree::create_file_node(file_id const & content)
{
  node_id nid = next_nid++;
  bookkeeping_path pth = path_for_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path %s already exists") % pth);
  safe_insert(written_content, make_pair(pth, content));
  // Defer actual write to moment of attachment, when we know the path
  // and can thus determine encoding / linesep convention.
  return nid;
}

void
editable_working_tree::attach_node(node_id nid, split_path const & dst)
{
  bookkeeping_path src_pth = path_for_nid(nid);
  file_path dst_pth(dst);

  // Possibly just write data out into the workspace, if we're doing
  // a file-create (not a dir-create or file/dir rename).
  if (!path_exists(src_pth))
    {
      I(root_dir_attached);
      map<bookkeeping_path, file_id>::const_iterator i
        = written_content.find(src_pth);
      if (i != written_content.end())
        {
          P(F("adding %s") % dst_pth);
          file_data dat;
          source.get_version(dst_pth, i->second, dat);
          write_localized_data(dst_pth, dat.inner(), lua);
          return;
        }
    }

  // FIXME: it is weird to do this here, instead of up above, but if we do it
  // up above a lot of tests break.  those tests are arguably broken -- they
  // depend on 'update' clobbering existing, non-versioned files -- but
  // putting this up there doesn't actually help, since if we abort in the
  // middle of an update to avoid clobbering a file, we just end up leaving
  // the working copy in an inconsistent state instead.  so for now, we leave
  // this check down here.
  require_path_is_nonexistent(dst_pth,
                              F("path '%s' already exists, cannot create") % dst_pth);

  // If we get here, we're doing a file/dir rename, or a dir-create.
  map<bookkeeping_path, file_path>::const_iterator i
    = rename_add_drop_map.find(src_pth);
  if (i != rename_add_drop_map.end())
    {
      P(F("renaming %s to %s") % i->second % dst_pth);
      safe_erase(rename_add_drop_map, src_pth);
    }
  else
    P(F("adding %s") % dst_pth);
  if (dst_pth == file_path())
    {
      // root dir attach, so we move contents, rather than the dir itself
      vector<utf8> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<utf8>::const_iterator i = files.begin(); i != files.end(); ++i)
        {
          I(!bookkeeping_path::is_bookkeeping_path((*i)()));
          move_file(src_pth / (*i)(), dst_pth / (*i)());
        }
      for (vector<utf8>::const_iterator i = dirs.begin(); i != dirs.end(); ++i)
        {
          I(!bookkeeping_path::is_bookkeeping_path((*i)()));
          move_dir(src_pth / (*i)(), dst_pth / (*i)());
        }
      delete_dir_shallow(src_pth);
      root_dir_attached = true;
    }
  else
    // This will complain if the move is actually impossible
    move_path(src_pth, dst_pth);
}

void
editable_working_tree::apply_delta(split_path const & pth,
                                   file_id const & old_id,
                                   file_id const & new_id)
{
  file_path pth_unsplit(pth);
  require_path_is_file(pth_unsplit,
                       F("file '%s' does not exist") % pth_unsplit,
                       F("file '%s' is a directory") % pth_unsplit);
  hexenc<id> curr_id_raw;
  calculate_ident(pth_unsplit, curr_id_raw, lua);
  file_id curr_id(curr_id_raw);
  E(curr_id == old_id,
    F("content of file '%s' has changed, not overwriting") % pth_unsplit);
  P(F("modifying %s") % pth_unsplit);

  file_data dat;
  source.get_version(pth_unsplit, new_id, dat);
  write_localized_data(pth_unsplit, dat.inner(), lua);
}

void
editable_working_tree::clear_attr(split_path const & pth,
                                  attr_key const & name)
{
  // FIXME_ROSTERS: call a lua hook
}

void
editable_working_tree::set_attr(split_path const & pth,
                                attr_key const & name,
                                attr_value const & val)
{
  // FIXME_ROSTERS: call a lua hook
}

void
editable_working_tree::commit()
{
  I(rename_add_drop_map.empty());
  I(root_dir_attached);
}

editable_working_tree::~editable_working_tree()
{
}

}; // anonymous namespace

static void
add_parent_dirs(split_path const & dst, roster_t & ros, node_id_source & nis,
                database & db, lua_hooks & lua)
{
  editable_roster_base er(ros, nis);
  addition_builder build(db, lua, ros, er);

  split_path dirname;
  path_component basename;
  dirname_basename(dst, dirname, basename);

  // FIXME: this is a somewhat odd way to use the builder
  build.visit_dir(dirname);
}

inline static bool
inodeprint_unchanged(inodeprint_map const & ipm, file_path const & path)
{
  inodeprint_map::const_iterator old_ip = ipm.find(path);
  if (old_ip != ipm.end())
    {
      hexenc<inodeprint> ip;
      if (inodeprint_file(path, ip) && ip == old_ip->second)
          return true; // unchanged
      else
          return false; // changed or unavailable
    }
  else
    return false; // unavailable
}

// updating rosters from the workspace

// TODO: unchanged, changed, missing might be better as set<node_id>

// note that this does not take a restriction because it is used only by
// automate_inventory which operates on the entire, unrestricted, working
// directory.

void
workspace::classify_roster_paths(roster_t const & ros,
                                 path_set & unchanged,
                                 path_set & changed,
                                 path_set & missing)
{
  temp_node_id_source nis;
  inodeprint_map ipm;

  if (in_inodeprints_mode())
    {
      data dat;
      read_inodeprints(dat);
      read_inodeprint_map(dat, ipm);
    }

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      split_path sp;
      ros.get_name(nid, sp);

      file_path fp(sp);

      if (is_dir_t(node) || inodeprint_unchanged(ipm, fp))
        {
          // dirs don't have content changes
          unchanged.insert(sp);
        }
      else
        {
          file_t file = downcast_to_file_t(node);
          file_id fid;
          if (ident_existing_file(fp, fid, lua))
            {
              if (file->content == fid)
                unchanged.insert(sp);
              else
                changed.insert(sp);
            }
          else
            {
              missing.insert(sp);
            }
        }
    }
}

void
workspace::update_current_roster_from_filesystem(roster_t & ros)
{
  update_current_roster_from_filesystem(ros, node_restriction());
}

void
workspace::update_current_roster_from_filesystem(roster_t & ros,
                                                 node_restriction const & mask)
{
  temp_node_id_source nis;
  inodeprint_map ipm;

  if (in_inodeprints_mode())
    {
      data dat;
      read_inodeprints(dat);
      read_inodeprint_map(dat, ipm);
    }

  size_t missing_files = 0;

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      // Only analyze files further, not dirs.
      if (! is_file_t(node))
        continue;

      // Only analyze restriction-included files.
      if (!mask.includes(ros, nid))
        continue;

      split_path sp;
      ros.get_name(nid, sp);
      file_path fp(sp);

      // Only analyze changed files (or all files if inodeprints mode
      // is disabled).
      if (inodeprint_unchanged(ipm, fp))
        continue;

      file_t file = downcast_to_file_t(node);
      if (!ident_existing_file(fp, file->content, lua))
        {
          W(F("missing %s") % (fp));
          missing_files++;
        }
    }

  N(missing_files == 0,
    F("%d missing files; use '%s ls missing' to view\n"
      "to restore consistency, on each missing file run either\n"
      "'%s drop FILE' to remove it permanently, or\n"
      "'%s revert FILE' to restore it\n"
      "or to handle all at once, simply '%s drop --missing'\n"
      "or '%s revert --missing'")
    % missing_files % ui.prog_name % ui.prog_name % ui.prog_name
    % ui.prog_name % ui.prog_name);
}

void
workspace::find_missing(roster_t const & new_roster_shape,
                        node_restriction const & mask,
                        path_set & missing)
{
  node_map const & nodes = new_roster_shape.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster_shape.is_root(nid) && mask.includes(new_roster_shape, nid))
        {
          split_path sp;
          new_roster_shape.get_name(nid, sp);
          file_path fp(sp);

          if (!path_exists(fp))
            missing.insert(sp);
        }
    }
}

void
workspace::find_unknown_and_ignored(path_restriction const & mask,
				    vector<file_path> const & roots,
                                    path_set & unknown, path_set & ignored)
{
  path_set known;
  roster_t new_roster;
  temp_node_id_source nis;

  get_current_roster_shape(new_roster, nis);

  new_roster.extract_path_set(known);

  file_itemizer u(db, lua, known, unknown, ignored, mask);
  for (vector<file_path>::const_iterator 
         i = roots.begin(); i != roots.end(); ++i)
    {
      walk_tree(*i, u);
    }
}

void
workspace::perform_additions(path_set const & paths, bool recursive)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis);

  editable_roster_base er(new_roster, nis);

  if (!new_roster.has_root())
    {
      split_path root;
      root.push_back(the_null_component);
      er.attach_node(er.create_dir_node(), root);
    }

  I(new_roster.has_root());
  addition_builder build(db, lua, new_roster, er);

  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (recursive)
        {
          // NB.: walk_tree will handle error checking for non-existent paths
          walk_tree(file_path(*i), build);
        }
      else
        {
          // in the case where we're just handled a set of paths, we use the builder
          // in this strange way.
          build.visit_file(file_path(*i));
        }
    }

  revision_id base_rev;
  get_revision_id(base_rev);

  revision_t new_work;
  make_revision(base_rev, base_roster, new_roster, new_work);
  put_work_rev(new_work);
  update_any_attrs();
}

void
workspace::perform_deletions(path_set const & paths, 
                             bool recursive, bool execute)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis);

  // we traverse the the paths backwards, so that we always hit deep paths
  // before shallow paths (because path_set is lexicographically sorted).
  // this is important in cases like
  //    monotone drop foo/bar foo foo/baz
  // where, when processing 'foo', we need to know whether or not it is empty
  // (and thus legal to remove)

  deque<split_path> todo;
  path_set::const_reverse_iterator i = paths.rbegin();
  todo.push_back(*i);
  ++i;

  while (todo.size())
    {
      split_path &p(todo.front());
      file_path name(p);

      if (!new_roster.has_node(p))
        P(F("skipping %s, not currently tracked") % name);
      else
        {
          node_t n = new_roster.get_node(p);
          if (is_dir_t(n))
            {
              dir_t d = downcast_to_dir_t(n);
              if (!d->children.empty())
                {
                  N(recursive,
                    F("cannot remove %s/, it is not empty") % name);
                  for (dir_map::const_iterator j = d->children.begin();
                       j != d->children.end(); ++j)
                    {
                      split_path sp = p;
                      sp.push_back(j->first);
                      todo.push_front(sp);
                    }
                  continue;
                }
            }
          P(F("dropping %s from workspace manifest") % name);
          new_roster.drop_detached_node(new_roster.detach_node(p));
          if (execute && path_exists(name))
            delete_file_or_dir_shallow(name);
        }
      todo.pop_front();
      if (i != paths.rend())
        {
          todo.push_back(*i);
          ++i;
        }
    }

  revision_id base_rev;
  get_revision_id(base_rev);

  revision_t new_work;
  make_revision(base_rev, base_roster, new_roster, new_work);
  put_work_rev(new_work);
  update_any_attrs();
}

void
workspace::perform_rename(set<file_path> const & src_paths,
                          file_path const & dst_path,
                          bool execute)
{
  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  split_path dst;
  set<split_path> srcs;
  set< pair<split_path, split_path> > renames;

  I(!src_paths.empty());

  get_base_and_current_roster_shape(base_roster, new_roster, nis);

  dst_path.split(dst);

  if (src_paths.size() == 1 && !new_roster.has_node(dst))
    {
      // "rename SRC DST" case
      split_path s;
      src_paths.begin()->split(s);
      renames.insert( make_pair(s, dst) );
      add_parent_dirs(dst, new_roster, nis, db, lua);
    }
  else
    {
      // "rename SRC1 SRC2 DST" case
      N(new_roster.has_node(dst),
        F("destination dir %s/ does not exist in current revision") % dst_path);

      N(is_dir_t(new_roster.get_node(dst)),
        F("destination %s is an existing file in current revision") % dst_path);

      for (set<file_path>::const_iterator i = src_paths.begin();
           i != src_paths.end(); i++)
        {
          split_path s;
          i->split(s);
          // TODO "rename . foo/" might be valid? Or should it already have been
          // normalised..., in which case it might be an I().
          N(!s.empty(),
            F("empty path %s is not allowed") % *i);

          path_component src_basename = s.back();
          split_path d(dst);
          d.push_back(src_basename);
          renames.insert( make_pair(s, d) );
        }
    }

  // one iteration to check for existing/missing files
  for (set< pair<split_path, split_path> >::const_iterator i = renames.begin();
       i != renames.end(); i++)
    {
      N(new_roster.has_node(i->first),
        F("%s does not exist in current revision") % file_path(i->first));

      N(!new_roster.has_node(i->second),
        F("destination %s already exists in current revision") % file_path(i->second));
    }

  // do the attach/detaching
  for (set< pair<split_path, split_path> >::const_iterator i = renames.begin();
       i != renames.end(); i++)
    {
      node_id nid = new_roster.detach_node(i->first);
      new_roster.attach_node(nid, i->second);
      P(F("renaming %s to %s in workspace manifest")
        % file_path(i->first)
        % file_path(i->second));
    }

  revision_id base_rev;
  get_revision_id(base_rev);

  revision_t new_work;
  make_revision(base_rev, base_roster, new_roster, new_work);
  put_work_rev(new_work);

  if (execute)
    {
      for (set< pair<split_path, split_path> >::const_iterator i = renames.begin();
           i != renames.end(); i++)
        {
          file_path s(i->first);
          file_path d(i->second);
          // silently skip files where src doesn't exist or dst does
          bool have_src = path_exists(s);
          bool have_dst = path_exists(d);
          if (have_src && !have_dst)
            {
              move_path(s, d);
            }
          else if (!have_src && !have_dst)
            {
              W(F("%s doesn't exist in workspace, skipping") % s);
            }
          else if (have_src && have_dst)
            {
              W(F("destination %s already exists in workspace, skipping") % d);
            }
          else
            {
              L(FL("skipping move_path %s->%s silently, src doesn't exist, dst does")
                % s % d);
            }
        }
    }
  update_any_attrs();
}

void
workspace::perform_pivot_root(file_path const & new_root,
                              file_path const & put_old,
                              bool execute)
{
  split_path new_root_sp, put_old_sp, root_sp;
  new_root.split(new_root_sp);
  put_old.split(put_old_sp);
  file_path().split(root_sp);

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis);

  I(new_roster.has_root());
  N(new_roster.has_node(new_root_sp),
    F("proposed new root directory '%s' is not versioned or does not exist") % new_root);
  N(is_dir_t(new_roster.get_node(new_root_sp)),
    F("proposed new root directory '%s' is not a directory") % new_root);
  {
    split_path new_root__MTN;
    (new_root / bookkeeping_root.as_internal()).split(new_root__MTN);
    N(!new_roster.has_node(new_root__MTN),
      F("proposed new root directory '%s' contains illegal path %s") % new_root % bookkeeping_root);
  }

  {
    file_path current_path_to_put_old = (new_root / put_old.as_internal());
    split_path current_path_to_put_old_sp, current_path_to_put_old_parent_sp;
    path_component basename;
    current_path_to_put_old.split(current_path_to_put_old_sp);
    dirname_basename(current_path_to_put_old_sp, current_path_to_put_old_parent_sp, basename);
    N(new_roster.has_node(current_path_to_put_old_parent_sp),
      F("directory '%s' is not versioned or does not exist")
      % file_path(current_path_to_put_old_parent_sp));
    N(is_dir_t(new_roster.get_node(current_path_to_put_old_parent_sp)),
      F("'%s' is not a directory")
      % file_path(current_path_to_put_old_parent_sp));
    N(!new_roster.has_node(current_path_to_put_old_sp),
      F("'%s' is in the way") % current_path_to_put_old);
  }

  cset cs;
  safe_insert(cs.nodes_renamed, make_pair(root_sp, put_old_sp));
  safe_insert(cs.nodes_renamed, make_pair(new_root_sp, root_sp));

  {
    editable_roster_base e(new_roster, nis);
    cs.apply_to(e);
  }
  {
    revision_id base_rev;
    get_revision_id(base_rev);

    revision_t new_work;
    make_revision(base_rev, base_roster, new_roster, new_work);
    put_work_rev(new_work);
  }
  if (execute)
    {
      content_merge_empty_adaptor cmea;
      perform_content_update(cs, cmea);
    }
  update_any_attrs();
}

void
workspace::perform_content_update(cset const & update,
                                  content_merge_adaptor const & ca)
{
  editable_working_tree ewt(lua, ca);
  update.apply_to(ewt);
}

void
workspace::update_any_attrs()
{
  temp_node_id_source nis;
  roster_t new_roster;
  get_current_roster_shape(new_roster, nis);
  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      split_path sp;
      new_roster.get_name(i->first, sp);

      node_t n = i->second;
      for (full_attr_map_t::const_iterator j = n->attrs.begin();
           j != n->attrs.end(); ++j)
        if (j->second.first)
          lua.hook_apply_attribute (j->first(), file_path(sp),
                                    j->second.second());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
