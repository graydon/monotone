// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "work.hh"

#include <ostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <queue>

#include "lexical_cast.hh"
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
#include "app_state.hh"
#include "database.hh"
#include "roster.hh"

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

static char const inodeprints_file_name[] = "inodeprints";
static char const local_dump_file_name[] = "debug";
static char const options_file_name[] = "options";
static char const user_log_file_name[] = "log";
static char const revision_file_name[] = "revision";

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
  o_path = workspace / bookkeeping_root_component / options_file_name;
  L(FL("options path is %s") % o_path);
}

static void
get_inodeprints_path(bookkeeping_path & ip_path)
{
  ip_path = bookkeeping_root / inodeprints_file_name;
  L(FL("inodeprints path is %s") % ip_path);
}

static void
get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(FL("user log path is %s") % ul_path);
}

//

bool
directory_is_workspace(system_path const & dir)
{
  // as far as the users of this function are concerned, a version 0
  // workspace (MT directory instead of _MTN) does not count.
  return directory_exists(dir / bookkeeping_root_component);
}

bool workspace::found;
bool workspace::branch_is_sticky;

void
workspace::require_workspace()
{
  N(workspace::found,
    F("workspace required but not found"));
}

void
workspace::require_workspace(i18n_format const & explanation)
{
  N(workspace::found,
    F("workspace required but not found\n%s") % explanation.str());
}

void
workspace::create_workspace(options const & opts,
                            lua_hooks & lua,
                            system_path const & new_dir)
{
  N(!new_dir.empty(), F("invalid directory ''"));

  L(FL("creating workspace in %s") % new_dir);

  mkdir_p(new_dir);
  go_to_workspace(new_dir);
  mark_std_paths_used();

  N(!directory_exists(bookkeeping_root),
    F("monotone bookkeeping directory '%s' already exists in '%s'")
    % bookkeeping_root % new_dir);

  L(FL("creating bookkeeping directory '%s' for workspace in '%s'")
    % bookkeeping_root % new_dir);

  mkdir_p(bookkeeping_root);

  workspace::found = true;
  workspace::set_ws_options(opts, true);
  workspace::write_ws_format();

  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);

  if (lua.hook_use_inodeprints())
    {
      bookkeeping_path ip_path;
      get_inodeprints_path(ip_path);
      write_data(ip_path, empty);
    }

  bookkeeping_path dump_path;
  workspace::get_local_dump_path(dump_path);
  // The 'false' means that, e.g., if we're running checkout,
  // then it's okay for dumps to go into our starting working
  // dir's _MTN rather than the new workspace dir's _MTN.
  global_sanity.set_dump_path(system_path(dump_path, false).as_external());
}

// Normal-use constructor.
workspace::workspace(app_state & app, bool writeback_options)
  : lua(app.lua)
{
  require_workspace();
  if (writeback_options)
    set_ws_options(app.opts, false);
}

workspace::workspace(app_state & app, i18n_format const & explanation,
                     bool writeback_options)
  : lua(app.lua)
{
  require_workspace(explanation);
  if (writeback_options)
    set_ws_options(app.opts, false);
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
get_roster_for_rid(database & db,
                   revision_id const & rid,
                   cached_roster & cr)
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
workspace::get_parent_rosters(database & db, parent_map & parents)
{
  revision_t rev;
  get_work_rev(rev);

  parents.clear();
  for (edge_map::const_iterator i = rev.edges.begin();
       i != rev.edges.end(); i++)
    {
      cached_roster cr;
      get_roster_for_rid(db, edge_old_revision(i), cr);
      safe_insert(parents, make_pair(edge_old_revision(i), cr));
    }
}

void
workspace::get_current_roster_shape(database & db,
                                    node_id_source & nis,
                                    roster_t & ros)
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
      make_roster_for_revision(db, nis, rev, new_rid, ros, dummy);
    }
}

bool
workspace::has_changes(database & db)
{
  parent_map parents;
  get_parent_rosters(db, parents);

  // if we have more than one parent roster then this workspace contains
  // a merge which means this is always a committable change
  if (parents.size() > 1)
    return true;

  temp_node_id_source nis;
  roster_t new_roster, old_roster = parent_roster(parents.begin());

  get_current_roster_shape(db, nis, new_roster);
  update_current_roster_from_filesystem(new_roster);

  return !(old_roster == new_roster);
}

// user log file

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

static void
read_options_file(any_path const & optspath,
                  system_path & database_option,
                  branch_name & branch_option,
                  rsa_keypair_id & key_option,
                  system_path & keydir_option)
{
  data dat;
  try
    {
      read_data(optspath, dat);
    }
  catch (exception & e)
    {
      W(F("Failed to read options file %s: %s") % optspath % e.what());
      return;
    }

  basic_io::input_source src(dat(), optspath.as_external());
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
          % opt % optspath);
    }
}

static void
write_options_file(bookkeeping_path const & optspath,
                   system_path const & database_option,
                   branch_name const & branch_option,
                   rsa_keypair_id const & key_option,
                   system_path const & keydir_option)
{
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
  try
    {
      write_data(optspath, data(pr.buf));
    }
  catch(exception & e)
    {
      W(F("Failed to write options file %s: %s") % optspath % e.what());
    }
}

void
workspace::get_ws_options(options & opts)
{
  if (!workspace::found)
    return;

  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;

  bookkeeping_path o_path;
  get_options_path(o_path);
  read_options_file(o_path,
                    database_option, branch_option, key_option, keydir_option);

  // Workspace options are not to override the command line.
  if (!opts.dbname_given)
    {
      opts.dbname = database_option;
    }

  if (!opts.key_dir_given && !opts.conf_dir_given)
    {
      opts.key_dir = keydir_option;
    }

  if (opts.branchname().empty() && !branch_option().empty())
    {
      opts.branchname = branch_option;
      branch_is_sticky = true;
    }

  L(FL("branch name is '%s'") % opts.branchname);

  if (!opts.key_given)
    opts.signing_key = key_option;
}

void
workspace::get_database_option(system_path const & workspace,
                               system_path & database_option)
{
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;

  system_path o_path = (workspace
                        / bookkeeping_root_component
                        / options_file_name);
  read_options_file(o_path,
                    database_option, branch_option, key_option, keydir_option);
}

void
workspace::set_ws_options(options const & opts, bool branch_is_sticky)
{
  N(workspace::found, F("workspace required but not found"));

  bookkeeping_path o_path;
  get_options_path(o_path);

  // If any of the incoming options was empty, we want to leave that option
  // as is in _MTN/options, not write out an empty option.
  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;

  if (file_exists(o_path))
    read_options_file(o_path,
                      database_option, branch_option, key_option, keydir_option);

  if (!opts.dbname.as_internal().empty())
    database_option = opts.dbname;
  if (!opts.key_dir.as_internal().empty())
    keydir_option = opts.key_dir;
  if ((branch_is_sticky || workspace::branch_is_sticky)
      && !opts.branchname().empty())
    branch_option = opts.branchname;
  if (opts.key_given)
    key_option = opts.signing_key;

  write_options_file(o_path,
                     database_option, branch_option, key_option, keydir_option);
}

void
workspace::print_ws_option(utf8 const & opt, std::ostream & output)
{
  N(workspace::found, F("workspace required but not found"));

  bookkeeping_path o_path;
  get_options_path(o_path);

  system_path database_option;
  branch_name branch_option;
  rsa_keypair_id key_option;
  system_path keydir_option;
  read_options_file(o_path,
                    database_option, branch_option, key_option, keydir_option);

  if (opt() == "database")
    output << database_option << '\n';
  else if (opt() == "branch")
    output << branch_option << '\n';
  else if (opt() == "key")
    output << key_option << '\n';
  else if (opt() == "keydir")
    output << keydir_option << '\n';
  else
    N(false, F("'%s' is not a recognized workspace option") % opt);
}

// local dump file

void
workspace::get_local_dump_path(bookkeeping_path & d_path)
{
  N(workspace::found, F("workspace required but not found"));

  d_path = bookkeeping_root / local_dump_file_name;
  L(FL("local dump path is %s") % d_path);
}

// inodeprint file

bool
workspace::in_inodeprints_mode()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return file_exists(ip_path);
}

void
workspace::read_inodeprints(data & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  read_data(ip_path, dat);
}

void
workspace::write_inodeprints(data const & dat)
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
workspace::maybe_update_inodeprints(database & db)
{
  if (!in_inodeprints_mode())
    return;

  inodeprint_map ipm_new;
  temp_node_id_source nis;
  roster_t new_roster;

  get_current_roster_shape(db, nis, new_roster);
  update_current_roster_from_filesystem(new_roster);

  parent_map parents;
  get_parent_rosters(db, parents);

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
          file_path fp;
          new_roster.get_name(nid, fp);
          hexenc<inodeprint> ip;
          if (inodeprint_file(fp, ip))
            ipm_new.insert(inodeprint_entry(fp, ip));
        }
    }
  data dat;
  write_inodeprint_map(ipm_new, dat);
  write_inodeprints(dat);
}

bool
workspace::ignore_file(file_path const & path)
{
  return lua.hook_ignore_file(path);
}

void
workspace::init_attributes(file_path const & path, editable_roster_base & er)
{
  map<string, string> attrs;
  lua.hook_init_attributes(path, attrs);
  if (attrs.size() > 0)
    for (map<string, string>::const_iterator i = attrs.begin();
         i != attrs.end(); ++i)
      er.set_attr(path, attr_key(i->first), attr_value(i->second));
}

// objects and routines for manipulating the workspace itself
namespace {

struct file_itemizer : public tree_walker
{
  database & db;
  workspace & work;
  set<file_path> & known;
  set<file_path> & unknown;
  set<file_path> & ignored;
  path_restriction const & mask;
  file_itemizer(database & db, workspace & work,
                set<file_path> & k,
                set<file_path> & u,
                set<file_path> & i,
                path_restriction const & r)
    : db(db), work(work), known(k), unknown(u), ignored(i), mask(r) {}
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};


bool
file_itemizer::visit_dir(file_path const & path)
{
  this->visit_file(path);
  return known.find(path) != known.end();
}

void
file_itemizer::visit_file(file_path const & path)
{
  if (mask.includes(path) && known.find(path) == known.end())
    {
      if (work.ignore_file(path) || db.is_dbfile(path))
        ignored.insert(path);
      else
        unknown.insert(path);
    }
}


struct workspace_itemizer : public tree_walker
{
  roster_t & roster;
  set<file_path> const & known;
  node_id_source & nis;

  workspace_itemizer(roster_t & roster, set<file_path> const & paths,
                     node_id_source & nis);
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

workspace_itemizer::workspace_itemizer(roster_t & roster,
                                       set<file_path> const & paths,
                                       node_id_source & nis)
    : roster(roster), known(paths), nis(nis)
{
  node_id root_nid = roster.create_dir_node(nis);
  roster.attach_node(root_nid, file_path_internal(""));
}

bool
workspace_itemizer::visit_dir(file_path const & path)
{
  node_id nid = roster.create_dir_node(nis);
  roster.attach_node(nid, path);
  return known.find(path) != known.end();
}

void
workspace_itemizer::visit_file(file_path const & path)
{
  file_id fid;
  node_id nid = roster.create_file_node(fid, nis);
  roster.attach_node(nid, path);
}


class
addition_builder
  : public tree_walker
{
  database & db;
  workspace & work;
  roster_t & ros;
  editable_roster_base & er;
  bool respect_ignore;
public:
  addition_builder(database & db, workspace & work,
                   roster_t & r, editable_roster_base & e,
                   bool i = true)
    : db(db), work(work), ros(r), er(e), respect_ignore(i)
  {}
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
  void add_nodes_for(file_path const & path, file_path const & goal);
};

void
addition_builder::add_nodes_for(file_path const & path,
                                file_path const & goal)
{
  // this check suffices to terminate the recursion; our caller guarantees
  // that the roster has a root node, which will be a directory.
  if (ros.has_node(path))
    {
      N(is_dir_t(ros.get_node(path)),
        F("cannot add %s, because %s is recorded as a file "
          "in the workspace manifest") % goal % path);
      return;
    }

  add_nodes_for(path.dirname(), goal);
  P(F("adding %s to workspace manifest") % path);

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
  er.attach_node(nid, path);

  work.init_attributes(path, er);
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
  if ((respect_ignore && work.ignore_file(path)) || db.is_dbfile(path))
    {
      P(F("skipping ignorable file %s") % path);
      return;
    }

  if (ros.has_node(path))
    {
      if (!path.empty())
        P(F("skipping %s, already accounted for in workspace") % path);
      return;
    }

  I(ros.has_root());
  add_nodes_for(path, path);
}

struct editable_working_tree : public editable_tree
{
  editable_working_tree(workspace & work, content_merge_adaptor const & source,
                        bool const messages)
    : work(work), source(source), next_nid(1), root_dir_attached(true),
      messages(messages)
  {};

  virtual node_id detach_node(file_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, file_path const & dst);

  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id);
  virtual void clear_attr(file_path const & pth,
                          attr_key const & name);
  virtual void set_attr(file_path const & pth,
                        attr_key const & name,
                        attr_value const & val);

  virtual void commit();

  virtual ~editable_working_tree();
private:
  workspace & work;
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

  set<file_path> blocked_paths;
  map<node_id, file_path> nid_map;
  int conflicts;

  simulated_working_tree(roster_t & r, temp_node_id_source & n)
    : workspace(r), nis(n), conflicts(0) {}

  virtual node_id detach_node(file_path const & src);
  virtual void drop_detached_node(node_id nid);

  virtual node_id create_dir_node();
  virtual node_id create_file_node(file_id const & content);
  virtual void attach_node(node_id nid, file_path const & dst);

  virtual void apply_delta(file_path const & pth,
                           file_id const & old_id,
                           file_id const & new_id);
  virtual void clear_attr(file_path const & pth,
                          attr_key const & name);
  virtual void set_attr(file_path const & pth,
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
                            file_id const &,
                            file_data const &, file_data const &,
                            file_data const &)
  { I(false); }
  virtual void get_ancestral_roster(node_id, revision_id &, boost::shared_ptr<roster_t const> &)
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
  return path_for_detached_nids() / path_component(lexical_cast<string>(nid));
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
editable_working_tree::detach_node(file_path const & src_pth)
{
  I(root_dir_attached);
  node_id nid = next_nid++;
  bookkeeping_path dst_pth = path_for_detached_nid(nid);
  safe_insert(rename_add_drop_map, make_pair(dst_pth, src_pth));
  if (src_pth == file_path())
    {
      // root dir detach, so we move contents, rather than the dir itself
      mkdir_p(dst_pth);
      vector<path_component> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<path_component>::const_iterator i = files.begin();
           i != files.end(); ++i)
        move_file(src_pth / *i, dst_pth / *i);
      for (vector<path_component>::const_iterator i = dirs.begin();
           i != dirs.end(); ++i)
        if (!bookkeeping_path::internal_string_is_bookkeeping_path(utf8((*i)())))
          move_dir(src_pth / *i, dst_pth / *i);
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
editable_working_tree::attach_node(node_id nid, file_path const & dst_pth)
{
  bookkeeping_path src_pth = path_for_detached_nid(nid);

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
      vector<path_component> files, dirs;
      read_directory(src_pth, files, dirs);
      for (vector<path_component>::const_iterator i = files.begin();
           i != files.end(); ++i)
        {
          I(!bookkeeping_path::internal_string_is_bookkeeping_path(utf8((*i)())));
          move_file(src_pth / *i, dst_pth / *i);
        }
      for (vector<path_component>::const_iterator i = dirs.begin();
           i != dirs.end(); ++i)
        {
          I(!bookkeeping_path::internal_string_is_bookkeeping_path(utf8((*i)())));
          move_dir(src_pth / *i, dst_pth / *i);
        }
      delete_dir_shallow(src_pth);
      root_dir_attached = true;
    }
  else
    // This will complain if the move is actually impossible
    move_path(src_pth, dst_pth);
}

void
editable_working_tree::apply_delta(file_path const & pth,
                                   file_id const & old_id,
                                   file_id const & new_id)
{
  require_path_is_file(pth,
                       F("file '%s' does not exist") % pth,
                       F("file '%s' is a directory") % pth);
  file_id curr_id;
  calculate_ident(pth, curr_id);
  E(curr_id == old_id,
    F("content of file '%s' has changed, not overwriting") % pth);
  P(F("modifying %s") % pth);

  file_data dat;
  source.get_version(new_id, dat);
  write_data(pth, dat.inner());
}

void
editable_working_tree::clear_attr(file_path const & pth,
                                  attr_key const & name)
{
  // FIXME_ROSTERS: call a lua hook
}

void
editable_working_tree::set_attr(file_path const & pth,
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
simulated_working_tree::detach_node(file_path const & src)
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
          map<node_id, file_path>::const_iterator i = nid_map.find(nid);
          I(i != nid_map.end());
          W(F("cannot drop non-empty directory '%s'") % i->second);
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
simulated_working_tree::attach_node(node_id nid, file_path const & dst)
{
  // this check is needed for checkout because we're using a roster to
  // represent paths that *may* block the checkout. however to represent
  // these we *must* have a root node in the roster which will *always*
  // block us. so here we check for that case and avoid it.
  if (dst.empty() && workspace.has_root())
    return;

  if (workspace.has_node(dst))
    {
      W(F("attach node %d blocked by unversioned path '%s'") % nid % dst);
      blocked_paths.insert(dst);
      conflicts++;
    }
  else if (dst.empty())
    {
      // the parent of the workspace root cannot be in the blocked set
      // this attach would have been caught above if it were a problem
      workspace.attach_node(nid, dst);
    }
  else
    {
      file_path parent = dst.dirname();

      if (blocked_paths.find(parent) == blocked_paths.end())
        workspace.attach_node(nid, dst);
      else
        {
          W(F("attach node %d blocked by blocked parent '%s'")
            % nid % parent);
          blocked_paths.insert(dst);
        }
    }
}

void
simulated_working_tree::apply_delta(file_path const & path,
                                    file_id const & old_id,
                                    file_id const & new_id)
{
  // this may fail if path is not a file but that will be caught
  // earlier in update_current_roster_from_filesystem
}

void
simulated_working_tree::clear_attr(file_path const & pth,
                                   attr_key const & name)
{
}

void
simulated_working_tree::set_attr(file_path const & pth,
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
add_parent_dirs(database & db, node_id_source & nis, workspace & work,
                file_path const & dst, roster_t & ros)
{
  editable_roster_base er(ros, nis);
  addition_builder build(db, work, ros, er);

  // FIXME: this is a somewhat odd way to use the builder
  build.visit_dir(dst.dirname());
}

// updating rosters from the workspace

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

      file_path fp;
      ros.get_name(nid, fp);

      const path::status status(get_path_status(fp));

      if (is_dir_t(node))
        {
          if (status == path::nonexistent)
            {
              W(F("missing directory '%s'") % (fp));
              missing_items++;
            }
          else if (status != path::directory)
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

          if (status == path::nonexistent)
            {
              W(F("missing file '%s'") % (fp));
              missing_items++;
            }
          else if (status != path::file)
            {
              W(F("not a file '%s'") % (fp));
              missing_items++;
            }

          file_t file = downcast_to_file_t(node);
          ident_existing_file(fp, file->content, status);
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
                        set<file_path> & missing)
{
  node_map const & nodes = new_roster_shape.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster_shape.is_root(nid)
          && mask.includes(new_roster_shape, nid))
        {
          file_path fp;
          new_roster_shape.get_name(nid, fp);
          if (!path_exists(fp))
            missing.insert(fp);
        }
    }
}

void
workspace::find_unknown_and_ignored(database & db,
                                    path_restriction const & mask,
                                    vector<file_path> const & roots,
                                    set<file_path> & unknown,
                                    set<file_path> & ignored)
{
  set<file_path> known;
  roster_t new_roster;
  temp_node_id_source nis;

  get_current_roster_shape(db, nis, new_roster);
  new_roster.extract_path_set(known);

  file_itemizer u(db, *this, known, unknown, ignored, mask);
  for (vector<file_path>::const_iterator
         i = roots.begin(); i != roots.end(); ++i)
    {
      walk_tree(*i, u);
    }
}

void
workspace::perform_additions(database & db, set<file_path> const & paths,
                             bool recursive, bool respect_ignore)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(db, nis, new_roster);

  editable_roster_base er(new_roster, nis);

  if (!new_roster.has_root())
    {
      er.attach_node(er.create_dir_node(), file_path_internal(""));
    }

  I(new_roster.has_root());
  addition_builder build(db, *this, new_roster, er, respect_ignore);

  for (set<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (recursive)
        {
          // NB.: walk_tree will handle error checking for non-existent paths
          walk_tree(*i, build);
        }
      else
        {
          // in the case where we're just handed a set of paths, we use the
          // builder in this strange way.
          switch (get_path_status(*i))
            {
            case path::nonexistent:
              N(false, F("no such file or directory: '%s'") % *i);
              break;
            case path::file:
              build.visit_file(*i);
              break;
            case path::directory:
              build.visit_dir(*i);
              break;
            }
        }
    }

  parent_map parents;
  get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  put_work_rev(new_work);
  update_any_attrs(db);
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
workspace::perform_deletions(database & db,
                             set<file_path> const & paths,
                             bool recursive, bool bookkeep_only)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(db, nis, new_roster);

  parent_map parents;
  get_parent_rosters(db, parents);

  // we traverse the the paths backwards, so that we always hit deep paths
  // before shallow paths (because set<file_path> is lexicographically
  // sorted).  this is important in cases like
  //    monotone drop foo/bar foo foo/baz
  // where, when processing 'foo', we need to know whether or not it is empty
  // (and thus legal to remove)

  deque<file_path> todo;
  set<file_path>::const_reverse_iterator i = paths.rbegin();
  todo.push_back(*i);
  ++i;

  while (todo.size())
    {
      file_path const & name(todo.front());

      E(!name.empty(),
        F("unable to drop the root directory"));

      if (!new_roster.has_node(name))
        P(F("skipping %s, not currently tracked") % name);
      else
        {
          node_t n = new_roster.get_node(name);
          if (is_dir_t(n))
            {
              dir_t d = downcast_to_dir_t(n);
              if (!d->children.empty())
                {
                  N(recursive,
                    F("cannot remove %s/, it is not empty") % name);
                  for (dir_map::const_iterator j = d->children.begin();
                       j != d->children.end(); ++j)
                    todo.push_front(name / j->first);
                  continue;
                }
            }
          if (!bookkeep_only && path_exists(name)
              && in_parent_roster(parents, n->self))
            {
              if (is_dir_t(n))
                {
                  if (directory_empty(name))
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("directory %s not empty - "
                        "it will be dropped but not deleted") % name);
                }
              else
                {
                  file_t file = downcast_to_file_t(n);
                  file_id fid;
                  I(ident_existing_file(name, fid));
                  if (file->content == fid)
                    delete_file_or_dir_shallow(name);
                  else
                    W(F("file %s changed - "
                        "it will be dropped but not deleted") % name);
                }
            }
          P(F("dropping %s from workspace manifest") % name);
          new_roster.drop_detached_node(new_roster.detach_node(name));
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
  update_any_attrs(db);
}

void
workspace::perform_rename(database & db,
                          set<file_path> const & srcs,
                          file_path const & dst,
                          bool bookkeep_only)
{
  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  set< pair<file_path, file_path> > renames;

  I(!srcs.empty());

  get_current_roster_shape(db, nis, new_roster);

  // validation.  it's okay if the target exists as a file; we just won't
  // clobber it (in !--bookkeep-only mode).  similarly, it's okay if the
  // source does not exist as a file.
  if (srcs.size() == 1 && !new_roster.has_node(dst))
    {
      // "rename SRC DST" case
      file_path const & src = *srcs.begin();
      file_path dpath = dst;

      N(!src.empty(),
        F("cannot rename the workspace root (try '%s pivot_root' instead)")
        % ui.prog_name);
      N(new_roster.has_node(src),
        F("source file %s is not versioned") % src);

      //this allows the 'magic add' of a non-versioned directory to happen in
      //all cases.  previously, mtn mv fileA dir/ woudl fail if dir/ wasn't
      //versioned whereas mtn mv fileA dir/fileA would add dir/ if necessary
      //and then reparent fileA.
      if (get_path_status(dst) == path::directory)
        dpath = dst / src.basename();
      else
        {
          //this handles the case where:
          // touch foo
          // mtn mv foo bar/foo where bar doesn't exist
          file_path parent = dst.dirname();
	        N(get_path_status(parent) == path::directory,
	          F("destination path's parent directory %s/ doesn't exist") % parent);
        }

      renames.insert(make_pair(src, dpath));
      add_parent_dirs(db, nis, *this, dpath, new_roster);
    }
  else
    {
      // "rename SRC1 [SRC2 ...] DSTDIR" case
      N(get_path_status(dst) == path::directory,
        F("destination %s/ is not a directory") % dst);

      for (set<file_path>::const_iterator i = srcs.begin();
           i != srcs.end(); i++)
        {
          N(!i->empty(),
            F("cannot rename the workspace root (try '%s pivot_root' instead)")
            % ui.prog_name);
          N(new_roster.has_node(*i),
            F("source file %s is not versioned") % *i);

          file_path d = dst / i->basename();
          N(!new_roster.has_node(d),
            F("destination %s already exists in the workspace manifest") % d);

          renames.insert(make_pair(*i, d));

          add_parent_dirs(db, nis, *this, d, new_roster);
        }
    }

  // do the attach/detaching
  for (set< pair<file_path, file_path> >::const_iterator i = renames.begin();
       i != renames.end(); i++)
    {
      node_id nid = new_roster.detach_node(i->first);
      new_roster.attach_node(nid, i->second);
      P(F("renaming %s to %s in workspace manifest") % i->first % i->second);
    }

  parent_map parents;
  get_parent_rosters(db, parents);

  revision_t new_work;
  make_revision_for_workspace(parents, new_roster, new_work);
  put_work_rev(new_work);

  if (!bookkeep_only)
    for (set< pair<file_path, file_path> >::const_iterator i = renames.begin();
         i != renames.end(); i++)
      {
        file_path const & s(i->first);
        file_path const & d(i->second);
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
            W(F("destination %s already exists in workspace, "
                "skipping filesystem rename") % d);
          }
        else
          {
            W(F("%s doesn't exist in workspace and %s does, "
                "skipping filesystem rename") % s % d);
          }
      }

  update_any_attrs(db);
}

void
workspace::perform_pivot_root(database & db,
                              file_path const & new_root,
                              file_path const & put_old,
                              bool bookkeep_only)
{
  temp_node_id_source nis;
  roster_t new_roster;
  MM(new_roster);
  get_current_roster_shape(db, nis, new_roster);

  I(new_roster.has_root());
  N(new_roster.has_node(new_root),
    F("proposed new root directory '%s' is not versioned or does not exist")
    % new_root);
  N(is_dir_t(new_roster.get_node(new_root)),
    F("proposed new root directory '%s' is not a directory") % new_root);
  {
    N(!new_roster.has_node(new_root / bookkeeping_root_component),
      F("proposed new root directory '%s' contains illegal path %s")
      % new_root % bookkeeping_root);
  }

  {
    file_path current_path_to_put_old = (new_root / put_old);
    file_path current_path_to_put_old_parent
      = current_path_to_put_old.dirname();

    N(new_roster.has_node(current_path_to_put_old_parent),
      F("directory '%s' is not versioned or does not exist")
      % current_path_to_put_old_parent);
    N(is_dir_t(new_roster.get_node(current_path_to_put_old_parent)),
      F("'%s' is not a directory")
      % current_path_to_put_old_parent);
    N(!new_roster.has_node(current_path_to_put_old),
      F("'%s' is in the way") % current_path_to_put_old);
  }

  cset cs;
  safe_insert(cs.nodes_renamed, make_pair(file_path_internal(""), put_old));
  safe_insert(cs.nodes_renamed, make_pair(new_root, file_path_internal("")));

  {
    editable_roster_base e(new_roster, nis);
    cs.apply_to(e);
  }

  {
    parent_map parents;
    get_parent_rosters(db, parents);

    revision_t new_work;
    make_revision_for_workspace(parents, new_roster, new_work);
    put_work_rev(new_work);
  }
  if (!bookkeep_only)
    {
      content_merge_empty_adaptor cmea;
      perform_content_update(db, cs, cmea);
    }
  update_any_attrs(db);
}

void
workspace::perform_content_update(database & db,
                                  cset const & update,
                                  content_merge_adaptor const & ca,
                                  bool const messages)
{
  roster_t roster;
  temp_node_id_source nis;
  set<file_path> known;
  roster_t new_roster;
  bookkeeping_path detached = path_for_detached_nids();

  E(!directory_exists(detached),
    F("workspace is locked\n"
      "you must clean up and remove the %s directory")
    % detached);

  get_current_roster_shape(db, nis, new_roster);
  new_roster.extract_path_set(known);

  workspace_itemizer itemizer(roster, known, nis);
  walk_tree(file_path(), itemizer);

  simulated_working_tree swt(roster, nis);
  update.apply_to(swt);

  mkdir_p(detached);

  editable_working_tree ewt(*this, ca, messages);
  update.apply_to(ewt);

  delete_dir_shallow(detached);
}

void
workspace::update_any_attrs(database & db)
{
  temp_node_id_source nis;
  roster_t new_roster;
  get_current_roster_shape(db, nis, new_roster);
  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      file_path fp;
      new_roster.get_name(i->first, fp);

      node_t n = i->second;
      for (full_attr_map_t::const_iterator j = n->attrs.begin();
           j != n->attrs.end(); ++j)
        if (j->second.first)
          lua.hook_apply_attribute (j->first(), fp,
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
