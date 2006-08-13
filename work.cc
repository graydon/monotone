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

#include "app_state.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "localized_file_io.hh"
#include "platform-wrapped.hh"
#include "restrictions.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "simplestring_xform.hh"
#include "vocab.hh"
#include "work.hh"
#include "revision.hh"

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
static string const work_file_name("work");
static string const user_log_file_name("log");


// attribute map file

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
      if (app.lua.hook_ignore_file(path) || app.db.is_dbfile(path))
        ignored.insert(sp);
      else
        unknown.insert(sp);
    }
}


void
find_missing(roster_t const & new_roster_shape, node_restriction const & mask,
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
find_unknown_and_ignored(app_state & app, path_restriction const & mask,
                         vector<file_path> const & roots,
                         path_set & unknown, path_set & ignored)
{
  revision_t rev;
  roster_t new_roster;
  path_set known;
  temp_node_id_source nis;

  get_current_roster_shape(new_roster, nis, app);

  new_roster.extract_path_set(known);

  file_itemizer u(app, known, unknown, ignored, mask);
  for (vector<file_path>::const_iterator 
         i = roots.begin(); i != roots.end(); ++i)
    {
      walk_tree(*i, u);
    }
}


class
addition_builder
  : public tree_walker
{
  app_state & app;
  roster_t & ros;
  editable_roster_base & er;
public:
  addition_builder(app_state & a,
                   roster_t & r,
                   editable_roster_base & e)
    : app(a), ros(r), er(e)
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
        I(ident_existing_file(path, ident, app.lua));
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
  app.lua.hook_init_attributes(path, attrs);
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
  if (app.lua.hook_ignore_file(path) || app.db.is_dbfile(path))
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

void
perform_additions(path_set const & paths, app_state & app, bool recursive)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis, app);

  editable_roster_base er(new_roster, nis);

  if (!new_roster.has_root())
    {
      split_path root;
      root.push_back(the_null_component);
      er.attach_node(er.create_dir_node(), root);
    }

  I(new_roster.has_root());
  addition_builder build(app, new_roster, er);

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

  cset new_work;
  make_cset(base_roster, new_roster, new_work);
  put_work_cset(new_work);
  update_any_attrs(app);
}

void
perform_deletions(path_set const & paths, app_state & app)
{
  if (paths.empty())
    return;

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis, app);

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
                  N(app.recursive,
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
          if (app.execute && path_exists(name))
            delete_file_or_dir_shallow(name);
        }
      todo.pop_front();
      if (i != paths.rend())
        {
          todo.push_back(*i);
          ++i;
        }
    }

  cset new_work;
  make_cset(base_roster, new_roster, new_work);
  put_work_cset(new_work);
  update_any_attrs(app);
}

static void
add_parent_dirs(split_path const & dst, roster_t & ros, node_id_source & nis,
                app_state & app)
{
  editable_roster_base er(ros, nis);
  addition_builder build(app, ros, er);

  split_path dirname;
  path_component basename;
  dirname_basename(dst, dirname, basename);

  // FIXME: this is a somewhat odd way to use the builder
  build.visit_dir(dirname);
}

void
perform_rename(set<file_path> const & src_paths,
               file_path const & dst_path,
               app_state & app)
{
  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  split_path dst;
  set<split_path> srcs;
  set< pair<split_path, split_path> > renames;

  I(!src_paths.empty());

  get_base_and_current_roster_shape(base_roster, new_roster, nis, app);

  dst_path.split(dst);

  if (src_paths.size() == 1 && !new_roster.has_node(dst))
    {
      // "rename SRC DST" case
      split_path s;
      src_paths.begin()->split(s);
      renames.insert( make_pair(s, dst) );
      add_parent_dirs(dst, new_roster, nis, app);
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

  cset new_work;
  make_cset(base_roster, new_roster, new_work);
  put_work_cset(new_work);

  if (app.execute)
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
  update_any_attrs(app);
}

void
perform_pivot_root(file_path const & new_root, file_path const & put_old,
                   app_state & app)
{
  split_path new_root_sp, put_old_sp, root_sp;
  new_root.split(new_root_sp);
  put_old.split(put_old_sp);
  file_path().split(root_sp);

  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  get_base_and_current_roster_shape(base_roster, new_roster, nis, app);

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
    cset new_work;
    make_cset(base_roster, new_roster, new_work);
    put_work_cset(new_work);
  }
  if (app.execute)
    {
      empty_file_content_source efcs;
      editable_working_tree e(app, efcs);
      cs.apply_to(e);
    }
  update_any_attrs(app);
}


// work file containing rearrangement from uncommitted adds/drops/renames

static void get_work_path(bookkeeping_path & w_path)
{
  w_path = bookkeeping_root / work_file_name;
  L(FL("work path is %s") % w_path);
}

void get_work_cset(cset & w)
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  if (path_exists(w_path))
    {
      L(FL("checking for un-committed work file %s") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_cset(w_data, w);
      L(FL("read cset from %s") % w_path);
    }
  else
    {
      L(FL("no un-committed work file %s") % w_path);
    }
}

void remove_work_cset()
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    delete_file(w_path);
}

void put_work_cset(cset & w)
{
  bookkeeping_path w_path;
  get_work_path(w_path);

  if (w.empty())
    {
      if (file_exists(w_path))
        delete_file(w_path);
    }
  else
    {
      data w_data;
      write_cset(w, w_data);
      write_data(w_path, w_data);
    }
}

// revision file name

string revision_file_name("revision");

static void get_revision_path(bookkeeping_path & m_path)
{
  m_path = bookkeeping_root / revision_file_name;
  L(FL("revision path is %s") % m_path);
}

void get_revision_id(revision_id & c)
{
  c = revision_id();
  bookkeeping_path c_path;
  get_revision_path(c_path);

  require_path_is_file(c_path,
                       F("workspace is corrupt: %s does not exist") % c_path,
                       F("workspace is corrupt: %s is a directory") % c_path);

  data c_data;
  L(FL("loading revision id from %s") % c_path);
  try
    {
      read_data(c_path, c_data);
    }
  catch(exception &)
    {
      N(false, F("Problem with workspace: %s is unreadable") % c_path);
    }
  c = revision_id(remove_ws(c_data()));
}

void put_revision_id(revision_id const & rev)
{
  bookkeeping_path c_path;
  get_revision_path(c_path);
  L(FL("writing revision id to %s") % c_path);
  data c_data(rev.inner()() + "\n");
  write_data(c_path, c_data);
}

void
get_base_revision(app_state & app,
                  revision_id & rid,
                  roster_t & ros,
                  marking_map & mm)
{
  get_revision_id(rid);

  if (!null_id(rid))
    {

      N(app.db.revision_exists(rid),
        F("base revision %s does not exist in database") % rid);

      app.db.get_roster(rid, ros, mm);
    }

  L(FL("base roster has %d entries") % ros.all_nodes().size());
}

void
get_base_revision(app_state & app,
                  revision_id & rid,
                  roster_t & ros)
{
  marking_map mm;
  get_base_revision(app, rid, ros, mm);
}

void
get_base_roster(app_state & app,
                roster_t & ros)
{
  revision_id rid;
  marking_map mm;
  get_base_revision(app, rid, ros, mm);
}

void
get_current_roster_shape(roster_t & ros, node_id_source & nis, app_state & app)
{
  get_base_roster(app, ros);
  cset cs;
  get_work_cset(cs);
  editable_roster_base er(ros, nis);
  cs.apply_to(er);
}

void
get_base_and_current_roster_shape(roster_t & base_roster,
                                  roster_t & current_roster,
                                  node_id_source & nis,
                                  app_state & app)
{
  get_base_roster(app, base_roster);
  current_roster = base_roster;
  cset cs;
  get_work_cset(cs);
  editable_roster_base er(current_roster, nis);
  cs.apply_to(er);
}

// user log file

void
get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(FL("user log path is %s") % ul_path);
}

void
read_user_log(data & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  if (file_exists(ul_path))
    {
      read_data(ul_path, dat);
    }
}

void
write_user_log(data const & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  write_data(ul_path, dat);
}

void
blank_user_log()
{
  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);
}

bool
has_contents_user_log()
{
  data user_log_message;
  read_user_log(user_log_message);
  return user_log_message().length() > 0;
}

// options map file

void
get_options_path(bookkeeping_path & o_path)
{
  o_path = bookkeeping_root / options_file_name;
  L(FL("options path is %s") % o_path);
}

void
read_options_map(data const & dat, options_map & options)
{
  basic_io::input_source src(dat(), "_MTN/options");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  // don't clear the options which will have settings from the command line
  // options.clear();

  string opt, val;
  while (parser.symp())
    {
      parser.sym(opt);
      parser.str(val);
      // options[opt] = val;
      // use non-replacing insert versus replacing with options[opt] = val;
      options.insert(make_pair(opt, val));
    }
}

void
write_options_map(data & dat, options_map const & options)
{
  basic_io::printer pr;

  basic_io::stanza st;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    st.push_str_pair(i->first, i->second());

  pr.print_stanza(st);
  dat = pr.buf;
}

// local dump file

void get_local_dump_path(bookkeeping_path & d_path)
{
  d_path = bookkeeping_root / local_dump_file_name;
  L(FL("local dump path is %s") % d_path);
}

// inodeprint file

void
get_inodeprints_path(bookkeeping_path & ip_path)
{
  ip_path = bookkeeping_root / inodeprints_file_name;
}

bool
in_inodeprints_mode()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return file_exists(ip_path);
}

void
read_inodeprints(data & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  read_data(ip_path, dat);
}

void
write_inodeprints(data const & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  write_data(ip_path, dat);
}

void
enable_inodeprints()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  data dat;
  write_data(ip_path, dat);
}


bool
get_attribute_from_roster(roster_t const & ros,
                          file_path const & path,
                          attr_key const & key,
                          attr_value & val)
{
  split_path sp;
  path.split(sp);
  if (ros.has_node(sp))
    {
      node_t n = ros.get_node(sp);
      full_attr_map_t::const_iterator i = n->attrs.find(key);
      if (i != n->attrs.end() && i->second.first)
        {
          val = i->second.second;
          return true;
        }
    }
  return false;
}


void update_any_attrs(app_state & app)
{
  temp_node_id_source nis;
  roster_t new_roster;
  get_current_roster_shape(new_roster, nis, app);
  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      split_path sp;
      new_roster.get_name(i->first, sp);

      // FIXME_RESTRICTIONS: do we need this check?
      // if (!app.restriction_includes(sp))
      //  continue;

      node_t n = i->second;
      for (full_attr_map_t::const_iterator j = n->attrs.begin();
           j != n->attrs.end(); ++j)
        {
          if (j->second.first)
            {
              app.lua.hook_apply_attribute (j->first(),
                                            file_path(sp),
                                            j->second.second());
            }
        }
    }
}

editable_working_tree::editable_working_tree(app_state & app,
                                             file_content_source const & source)
  : app(app), source(source), next_nid(1), root_dir_attached(true)
{
}

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
          source.get_file_content(i->second, dat);
          write_localized_data(dst_pth, dat.inner(), app.lua);
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
  calculate_ident(pth_unsplit, curr_id_raw, app.lua);
  file_id curr_id(curr_id_raw);
  E(curr_id == old_id,
    F("content of file '%s' has changed, not overwriting") % pth_unsplit);
  P(F("modifying %s") % pth_unsplit);

  file_data dat;
  source.get_file_content(new_id, dat);
  write_localized_data(pth_unsplit, dat.inner(), app.lua);
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
