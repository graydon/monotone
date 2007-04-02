// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <sstream>
#include <cstring>
#include <cerrno>
#include <queue>

#include <boost/lexical_cast.hpp>

#include "work.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "file_io.hh"
#include "platform-wrapped.hh"
#include "restrictions.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "simplestring_xform.hh"
#include "revision.hh"
#include "inodeprint.hh"
#include "diff_patch.hh"
#include "ui.hh"
#include "charset.hh"
#include "lua_hooks.hh"

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
get_options_path(system_path const & workspace, system_path & o_path)
{
  o_path = workspace / bookkeeping_root.as_internal() / options_file_name;
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
void
workspace::get_work_rev(revision_t & rev)
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
  // Mark it so it doesn't creep into the database.
  rev.made_for = made_for_workspace;
}

void
workspace::put_work_rev(revision_t const & rev)
{
  MM(rev);
  I(rev.made_for == made_for_workspace);
  rev.check_sane();

  data rev_data;
  write_revision(rev, rev_data);

  bookkeeping_path rev_path;
  get_revision_path(rev_path);
  write_data(rev_path, rev_data);
}

// structures derived from the work revision, the database, and possibly
// the workspace

static void
get_roster_for_rid(revision_id const & rid,
                   database::cached_roster & cr,
                   database & db)
{
  // We may be asked for a roster corresponding to the null rid, which
  // is not in the database.  In this situation, what is wanted is an empty
  // roster (and marking map).
  if (null_id(rid))
    {
      cr.first = boost::shared_ptr<roster_t const>(new roster_t);
      cr.second = boost::shared_ptr<marking_map const>(new marking_map);
    }
  else
    {
      N(db.revision_exists(rid),
        F("base revision %s does not exist in database") % rid);
      db.get_roster(rid, cr);
    }
  L(FL("base roster has %d entries") % cr.first->all_nodes().size());
}

void
workspace::get_parent_rosters(parent_map & parents)
{
  revision_t rev;
  get_work_rev(rev);

  parents.clear();
  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); i++)
    {
      database::cached_roster cr;
      get_roster_for_rid(edge_old_revision(i), cr, db);
      safe_insert(parents, make_pair(edge_old_revision(i), cr));
    }
}

void
workspace::get_current_roster_shape(roster_t & ros, node_id_source & nis)
{
  revision_t rev;
  get_work_rev(rev);
  revision_id new_rid(fake_id());

  // If there is just one parent, it might be the null ID, which
  // make_roster_for_revision does not handle correctly.
  if (rev.edges.size() == 1 && null_id(edge_old_revision(rev.edges.begin())))
    {
      I(ros.all_nodes().size() == 0);
      editable_roster_base er(ros, nis);
      edge_changes(rev.edges.begin()).apply_to(er);
    }
  else
    {
      marking_map dummy;
      make_roster_for_revision(rev, new_rid, ros, dummy, db, nis);
    }
}

// user log file

void
workspace::get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(FL("user log path is %s") % ul_path);
}

void
workspace::read_user_log(utf8 & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  if (file_exists(ul_path))
    {
      data tmp;
      read_data(ul_path, tmp);
      system_to_utf8(external(tmp()), dat);
    }
}

void
workspace::write_user_log(utf8 const & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  external tmp;
  utf8_to_system_best_effort(dat, tmp);
  write_data(ul_path, data(tmp()));
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
  utf8 user_log_message;
  read_user_log(user_log_message);
  return user_log_message().length() > 0;
}

// _MTN/options handling.

void
workspace::get_ws_options(system_path & database_option,
                          branch_name & branch_option,
                          rsa_keypair_id & key_option,
                          system_path & keydir_option)
{
  system_path empty_path;
  get_ws_options_from_path(empty_path, database_option,
                branch_option, key_option, keydir_option);
}

bool
workspace::get_ws_options_from_path(system_path const & workspace,
                          system_path & database_option,
                          branch_name & branch_option,
                          rsa_keypair_id & key_option,
                          system_path & keydir_option)
{
  any_path * o_path;
  bookkeeping_path ws_o_path;
  system_path sys_o_path;
  
  if (workspace.empty())
    {
      get_options_path(ws_o_path);
      o_path = & ws_o_path;
    }
  else
    {
      get_options_path(workspace, sys_o_path);
      o_path = & sys_o_path;
    }
  
  try
    {
      if (path_exists(*o_path))
        {
          data dat;
          read_data(*o_path, dat);

          basic_io::input_source src(dat(), o_path->as_external());
          basic_io::tokenizer tok(src);
          basic_io::parser parser(tok);

          while (parser.symp())
            {
              string opt, val;
              parser.sym(opt);
              parser.str(val);

              if (opt == "database")
                database_option = system_path(val);
              else if (opt == "branch")
                branch_option = branch_name(val);
              else if (opt == "key")
                internalize_rsa_keypair_id(utf8(val), key_option);
              else if (opt == "keydir")
                keydir_option = system_path(val);
              else
                W(F("unrecognized key '%s' in options file %s - ignored")
                  % opt % o_path);
            }
          return true;
        }
      else
        return false;
    }
  catch(exception & e)
    {
      W(F("Failed to read options file %s: %s") % *o_path % e.what());
    }
  
  return false;
}

void
workspace::set_ws_options(system_path & database_option,
                          branch_name & branch_option,
                          rsa_keypair_id & key_option,
                          system_path & keydir_option)
{
  // If caller passes an empty string for any of the incoming options,
  // we want to leave that option as is in _MTN/options, not write out
  // an empty option.
  system_path old_database_option;
  branch_name old_branch_option;
  rsa_keypair_id old_key_option;
  system_path old_keydir_option;
  get_ws_options(old_database_option, old_branch_option,
                 old_key_option, old_keydir_option);

  if (database_option.as_internal().empty())
    database_option = old_database_option;
  if (branch_option().empty())
    branch_option = old_branch_option;
  if (key_option().empty())
    key_option = old_key_option;
  if (keydir_option.as_internal().empty())
    keydir_option = old_keydir_option;

  basic_io::stanza st;
  if (!database_option.as_internal().empty())
    st.push_str_pair(symbol("database"), database_option.as_internal());
  if (!branch_option().empty())
    st.push_str_pair(symbol("branch"), branch_option());
  if (!key_option().empty())
    {
      utf8 key;
      externalize_rsa_keypair_id(key_option, key);
      st.push_str_pair(symbol("key"), key());
    }
  if (!keydir_option.as_internal().empty())
    st.push_str_pair(symbol("keydir"), keydir_option.as_internal());

  basic_io::printer pr;
  pr.print_stanza(st);

  bookkeeping_path o_path;
  get_options_path(o_path);
  try
    {
      write_data(o_path, data(pr.buf));
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
  roster_t new_roster;

  get_current_roster_shape(new_roster, nis);
  update_current_roster_from_filesystem(new_roster);

  parent_map parents;
  get_parent_rosters(parents);

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      node_id nid = i->first;
      if (!is_file_t(i->second))
        continue;
      file_t new_file = downcast_to_file_t(i->second);
      bool all_same = true;

      for (parent_map::const_iterator parent = parents.begin();
           parent != parents.end(); ++parent)
        {
          roster_t const & parent_ros = parent_roster(parent);
          if (parent_ros.has_node(nid))
            {
              node_t old_node = parent_ros.get_node(nid);
              I(is_file_t(old_node));
              file_t old_file = downcast_to_file_t(old_node);

              if (new_file->content != old_file->content)
                {
                  all_same = false;
                  break;
                }
            }
        }

      if (all_same)
        {
          split_path sp;
          new_roster.get_name(nid, sp);
          file_path fp(sp);
          hexenc<inodeprint> ip;
          if (inodeprint_file(fp, ip))
            ipm_new.insert(inodeprint_entry(fp, ip));
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
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};


bool
file_itemizer::visit_dir(file_path const & path)
{
  this->visit_file(path);

  split_path sp;
  path.split(sp);
  return known.find(sp) != known.end();
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


struct workspace_itemizer : public tree_walker
{
  roster_t & roster;
  path_set const & known;
  node_id_source & nis;

  workspace_itemizer(roster_t & roster, path_set const & paths, 
                     node_id_source & nis);
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

workspace_itemizer::workspace_itemizer(roster_t & roster, 
                                       path_set const & paths, 
                                       node_id_source & nis)
    : roster(roster), known(paths), nis(nis)
{
  split_path root_path;
  file_path().split(root_path);
  node_id root_nid = roster.create_dir_node(nis);
  roster.attach_node(root_nid, root_path);
}

bool
workspace_itemizer::visit_dir(file_path const & path)
{
  split_path sp;
  path.split(sp);
  node_id nid = roster.create_dir_node(nis);
  roster.attach_node(nid, sp);
  return known.find(sp) != known.end();
}

void
workspace_itemizer::visit_file(file_path const & path)
{
  split_path sp;
  path.split(sp);
  file_id fid;
  node_id nid = roster.create_file_node(fid, nis);
  roster.attach_node(nid, sp);
}


class
addition_builder
  : public tree_walker
{
  database & db;
  lua_hooks & lua;
  roster_t & ros;
  editable_roster_base & er;
  bool respect_ignore;
public:
  addition_builder(database & db, lua_hooks & lua,
                   roster_t & r, editable_roster_base & e,
                   bool i = true)
    : db(db), lua(lua), ros(r), er(e), respect_ignore(i)
  {}
  virtual bool visit_dir(file_path const & path);
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
        I(ident_existing_file(path, ident));
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


bool
addition_builder::visit_dir(file_path const & path)
{
  this->visit_file(path);
  return true;
}

void
addition_builder::visit_file(file_path const & path)
{
  if ((respect_ignore && lua.hook_ignore_file(path)) || db.is_dbfile(path))
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
      if (!is_dir_t(ros.get_node(prefix)))
        {
          N(prefix == sp,
            F("cannot add %s, because %s is recorded as a file in the workspace manifest")
            % file_path(sp) % file_path(sp));
          break;
        }
    }
}

struct editable_working_tree : public editable_tree
{
  editable_working_tree(lua_hooks & lua, content_merge_adaptor const & source,
                        bool const messages) 
    : lua(lua), source(source), next_nid(1), root_dir_attached(true),
      messages(messages)
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
  std::map<bookkeeping_path, file_path> rename_add_drop_map;
  bool root_dir_attached;
  bool messages;
};


struct simulated_working_tree : public editable_tree
{
  roster_t & workspace;
  node_id_source & nis;
  
  path_set blocked_paths;
  map<node_id, split_path> nid_map;
  int conflicts;

  simulated_working_tree(roster_t & r, temp_node_id_source & n)
    : workspace(r), nis(n), conflicts(0) {}

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

  virtual ~simulated_working_tree();
};


struct content_merge_empty_adaptor : public content_merge_adaptor
{
  virtual void get_version(file_id const &, file_data &) const
  { I(false); }
  virtual void record_merge(file_id const &, file_id const &,
                            file_id const &, file_data const &,
                            file_data const &)
  { I(false); }
  virtual void get_ancestral_roster(node_id, boost::shared_ptr<roster_t const> &)
  { I(false); }
};

// editable_working_tree implementation

static inline bookkeeping_path
path_for_detached_nids()
{
  return bookkeeping_root / "detached";
}

static inline bookkeeping_path
path_for_detached_nid(node_id nid)
{
  return path_for_detached_nids() / lexical_cast<string>(nid);
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
  bookkeeping_path dst_pth = path_for_detached_nid(nid);
  safe_insert(rename_add_drop_map, make_pair(dst_pth, src_pth));
  if (src_pth == file_path())
    {
      // root dir detach, so we move contents, rather than the dir itself
      mkdir_p(dst_pth);
      vector<utf8> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<utf8>::const_iterator i = files.begin(); i != files.end(); ++i)
        move_file(src_pth / (*i)(), dst_pth / (*i)());
      for (vector<utf8>::const_iterator i = dirs.begin(); i != dirs.end(); ++i)
        if (!bookkeeping_path::internal_string_is_bookkeeping_path(*i))
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
  bookkeeping_path pth = path_for_detached_nid(nid);
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
  bookkeeping_path pth = path_for_detached_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path %s already exists") % pth);
  mkdir_p(pth);
  return nid;
}

node_id
editable_working_tree::create_file_node(file_id const & content)
{
  node_id nid = next_nid++;
  bookkeeping_path pth = path_for_detached_nid(nid);
  require_path_is_nonexistent(pth,
                              F("path %s already exists") % pth);
  file_data dat;
  source.get_version(content, dat);
  write_data(pth, dat.inner());

  return nid;
}

void
editable_working_tree::attach_node(node_id nid, split_path const & dst)
{
  bookkeeping_path src_pth = path_for_detached_nid(nid);
  file_path dst_pth(dst);

  map<bookkeeping_path, file_path>::const_iterator i
    = rename_add_drop_map.find(src_pth);
  if (i != rename_add_drop_map.end())
    {
      if (messages)
        P(F("renaming %s to %s") % i->second % dst_pth);
      safe_erase(rename_add_drop_map, src_pth);
    }
  else if (messages)
     P(F("adding %s") % dst_pth);

  if (dst_pth == file_path())
    {
      // root dir attach, so we move contents, rather than the dir itself
      vector<utf8> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<utf8>::const_iterator i = files.begin(); i != files.end(); ++i)
        {
          I(!bookkeeping_path::internal_string_is_bookkeeping_path(*i));
          move_file(src_pth / (*i)(), dst_pth / (*i)());
        }
      for (vector<utf8>::const_iterator i = dirs.begin(); i != dirs.end(); ++i)
        {
          I(!bookkeeping_path::internal_string_is_bookkeeping_path(*i));
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
  calculate_ident(pth_unsplit, curr_id_raw);
  file_id curr_id(curr_id_raw);
  E(curr_id == old_id,
    F("content of file '%s' has changed, not overwriting") % pth_unsplit);
  P(F("modifying %s") % pth_unsplit);

  file_data dat;
  source.get_version(new_id, dat);
  write_data(pth_unsplit, dat.inner());
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


node_id
simulated_working_tree::detach_node(split_path const & src)
{
  node_id nid = workspace.detach_node(src);
  nid_map.insert(make_pair(nid, src));
  return nid;
}

void
simulated_working_tree::drop_detached_node(node_id nid)
{
  node_t node = workspace.get_node(nid);
  if (is_dir_t(node)) 
    {
      dir_t dir = downcast_to_dir_t(node);
      if (!dir->children.empty())
        {
          map<node_id, split_path>::const_iterator i = nid_map.find(nid);
          I(i != nid_map.end());
          split_path path = i->second;
          W(F("cannot drop non-empty directory '%s'") % path);
          conflicts++;
        }
    }
}

node_id
simulated_working_tree::create_dir_node()
{
  return workspace.create_dir_node(nis);
}

node_id
simulated_working_tree::create_file_node(file_id const & content)
{
  return workspace.create_file_node(content, nis);
}

void
simulated_working_tree::attach_node(node_id nid, split_path const & dst)
{
  // this check is needed for checkout because we're using a roster to
  // represent paths that *may* block the checkout. however to represent
  // these we *must* have a root node in the roster which will *always*
  // block us. so here we check for that case and avoid it.

  if (workspace_root(dst) && workspace.has_root())
    return;

  if (workspace.has_node(dst))
    {
      W(F("attach node %d blocked by unversioned path '%s'") % nid % dst);
      blocked_paths.insert(dst);
      conflicts++;
    }
  else
    {
      split_path dirname;
      path_component basename;
      dirname_basename(dst, dirname, basename);

      if (blocked_paths.find(dirname) == blocked_paths.end())
        workspace.attach_node(nid, dst);
      else
        {
          W(F("attach node %d blocked by blocked parent '%s'") % nid % dst);
          blocked_paths.insert(dst);
        }
    }
}

void
simulated_working_tree::apply_delta(split_path const & path,
                                    file_id const & old_id,
                                    file_id const & new_id)
{
  // this may fail if path is not a file but that will be caught
  // earlier in update_current_roster_from_filesystem
}

void
simulated_working_tree::clear_attr(split_path const & pth,
                                   attr_key const & name)
{
}

void
simulated_working_tree::set_attr(split_path const & pth,
                                 attr_key const & name,
                                 attr_value const & val)
{
}

void
simulated_working_tree::commit()
{
  N(conflicts == 0, F("%d workspace conflicts") % conflicts);
}

simulated_working_tree::~simulated_working_tree()
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

      // if this node is a file, check the inodeprint cache for changes
      if (!is_dir_t(node) && inodeprint_unchanged(ipm, fp))
        {
          unchanged.insert(sp);
          continue;
        }
      
      // if the node is a directory, check if it exists
      // directories do not have content changes, thus are inserted in the
      // unchanged set
      if (is_dir_t(node))
        {
          if (directory_exists(fp))
              unchanged.insert(sp);
          else
              missing.insert(sp);
          continue;
        }
      
      // the node is a file, check if it exists and has been changed
      file_t file = downcast_to_file_t(node);
      file_id fid;
      if (ident_existing_file(fp, fid))
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

  size_t missing_items = 0;

  // this code is speed critical, hence the use of inode fingerprints so be
  // careful when making changes in here and preferably do some timing tests

  if (!ros.has_root())
    return;

  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      // Only analyze restriction-included files and dirs
      if (!mask.includes(ros, nid))
        continue;

      split_path sp;
      ros.get_name(nid, sp);
      file_path fp(sp);

      if (is_dir_t(node))
        {
          if (!path_exists(fp))
            {
              W(F("missing directory '%s'") % (fp));
              missing_items++;
            }
          else if (!directory_exists(fp))
            {
              W(F("not a directory '%s'") % (fp));
              missing_items++;
            }
        }
      else
        {
          // Only analyze changed files (or all files if inodeprints mode
          // is disabled).
          if (inodeprint_unchanged(ipm, fp))
            continue;

          if (!path_exists(fp))
            {
              W(F("missing file '%s'") % (fp));
              missing_items++;
            }
          else if (!file_exists(fp))
            {
              W(F("not a file '%s'") % (fp));
              missing_items++;
            }

          file_t file = downcast_to_file_t(node);
          ident_existing_file(fp, file->content);
        }

    }

  N(missing_items == 0,
    F("%d missing items; use '%s ls missing' to view\n"
      "To restore consistency, on each missing item run either\n"
      " '%s drop ITEM' to remove it permanently, or\n"
      " '%s revert ITEM' to restore it.\n"
      "To handle all at once, simply use\n"
      " '%s drop --missing' or\n"
      " '%s revert --missing'")
    % missing_items % ui.prog_name % ui.prog_name % ui.prog_name
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
workspace::perform_additions(path_set const & paths,
                             bool recursive, bool respect_ignore)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(new_roster, nis);

  editable_roster_base er(new_roster, nis);

  if (!new_roster.has_root())
    {
      split_path root;
      root.push_back(the_null_component);
      er.attach_node(er.create_dir_node(), root);
    }

  I(new_roster.has_root());
  addition_builder build(db, lua, new_roster, er, respect_ignore);

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
          file_path path(*i);
          switch (get_path_status(path))
            {
            case path::nonexistent:
              N(false, F("no such file or directory: '%s'") % path);
              break;
            case path::file:
              build.visit_file(path);
              break;
            case path::directory:
              build.visit_dir(path);
              break;
            }
        }
    }

  parent_map parents;
  get_parent_rosters(parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  put_work_rev(new_work);
  update_any_attrs();
}

static bool
in_parent_roster(const parent_map & parents, const node_id & nid)
{
  for (parent_map::const_iterator i = parents.begin();
       i != parents.end();
       i++)
    {
      if (parent_roster(i).has_node(nid))
        return true;
    }
  
  return false;
}

void
workspace::perform_deletions(path_set const & paths, 
                             bool recursive, bool bookkeep_only)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(new_roster, nis);

  parent_map parents;
  get_parent_rosters(parents);

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
          if (!bookkeep_only && path_exists(name) && in_parent_roster(parents, n->self))
            {
              if (is_dir_t(n))
                {
                  if (directory_empty(name))
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("directory %s not empty - it will be dropped but not deleted") % name);
                }
              else
                {
                  file_t file = downcast_to_file_t(n);
                  file_id fid;
                  I(ident_existing_file(name, fid));
                  if (file->content == fid)
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("file %s changed - it will be dropped but not deleted") % name);
                }
            }
          P(F("dropping %s from workspace manifest") % name);
          new_roster.drop_detached_node(new_roster.detach_node(p));
        }
      todo.pop_front();
      if (i != paths.rend())
        {
          todo.push_back(*i);
          ++i;
        }
    }

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  put_work_rev(new_work);
  update_any_attrs();
}

void
workspace::perform_rename(set<file_path> const & src_paths,
                          file_path const & dst_path,
                          bool bookkeep_only)
{
  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  split_path dst;
  set<split_path> srcs;
  set< pair<split_path, split_path> > renames;

  I(!src_paths.empty());

  get_current_roster_shape(new_roster, nis);

  dst_path.split(dst);

  if (src_paths.size() == 1 && !new_roster.has_node(dst))
    {
      // "rename SRC DST" case
      split_path s;
      src_paths.begin()->split(s);
      N(new_roster.has_node(s),
        F("source file %s is not versioned") % s);
      N(get_path_status(dst_path) != path::directory,
        F("destination name %s already exists as an unversioned directory") % dst);
      renames.insert( make_pair(s, dst) );
      add_parent_dirs(dst, new_roster, nis, db, lua);
    }
  else
    {
      // "rename SRC1 SRC2 DST" case
      N(new_roster.has_node(dst),
        F("destination dir %s/ is not versioned (perhaps add it?)") % dst_path);

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
        F("%s does not exist in current manifest") % file_path(i->first));

      N(!new_roster.has_node(i->second),
        F("destination %s already exists in current manifest") % file_path(i->second));

      split_path parent;
      path_component basename;
      dirname_basename(i->second, parent, basename);
      N(new_roster.has_node(parent),
        F("destination directory %s does not exist in current manifest") % file_path(parent));
      N(is_dir_t(new_roster.get_node(parent)),
        F("destination directory %s is not a directory") % file_path(parent));
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

  parent_map parents;
  get_parent_rosters(parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  put_work_rev(new_work);

  if (!bookkeep_only)
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
              W(F("destination %s already exists in workspace, skipping filesystem rename") % d);
            }
          else
            {
              W(F("skipping move_path in filesystem %s->%s, source doesn't exist, destination does")
                % s % d);
            }
        }
    }
  update_any_attrs();
}

void
workspace::perform_pivot_root(file_path const & new_root,
                              file_path const & put_old,
                              bool bookkeep_only)
{
  split_path new_root_sp, put_old_sp, root_sp;
  new_root.split(new_root_sp);
  put_old.split(put_old_sp);
  file_path().split(root_sp);

  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(new_roster, nis);

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
    parent_map parents;
    get_parent_rosters(parents);

    revision_t new_work;
    make_revision_for_workspace(parents, new_roster, new_work);
    put_work_rev(new_work);
  }
  if (!bookkeep_only)
    {
      content_merge_empty_adaptor cmea;
      perform_content_update(cs, cmea);
    }
  update_any_attrs();
}

void
workspace::perform_content_update(cset const & update,
                                  content_merge_adaptor const & ca,
                                  bool const messages)
{
  roster_t roster;
  temp_node_id_source nis;
  path_set known;
  roster_t new_roster;
  bookkeeping_path detached = path_for_detached_nids();

  E(!directory_exists(detached), 
    F("workspace is locked\n"
      "you must clean up and remove the %s directory")
    % detached);

  get_current_roster_shape(new_roster, nis);
  new_roster.extract_path_set(known);

  workspace_itemizer itemizer(roster, known, nis);
  walk_tree(file_path(), itemizer);

  simulated_working_tree swt(roster, nis);
  update.apply_to(swt);

  mkdir_p(detached);

  editable_working_tree ewt(lua, ca, messages);
  update.apply_to(ewt);

  delete_dir_shallow(detached);
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
