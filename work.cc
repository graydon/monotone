// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sstream>
#include <cstdio>
#include <cstring>
#include <cerrno>

#include "app_state.hh"
#include "basic_io.hh"
#include "cset.hh"
#include "file_io.hh"
#include "platform.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "work.hh"

// working copy / book-keeping file code

using namespace std;

// attribute map file

string const attr_file_name(".mt-attrs");

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
  if (app.restriction_includes(sp) && known.find(sp) == known.end())
    {
      if (app.lua.hook_ignore_file(path))
        ignored.insert(sp);
      else
        unknown.insert(sp);
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
  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }  

  split_path sp;
  path.split(sp);
  if (ros.has_node(sp))
    {
      P(F("skipping %s, already accounted for in working copy\n") % path);
      return;
    }

  P(F("adding %s to working copy add set\n") % path);

  split_path dirname, prefix;
  path_component basename;
  dirname_basename(sp, dirname, basename);
  I(ros.has_root());
  for (split_path::const_iterator i = dirname.begin(); i != dirname.end();
       ++i)
    {
      prefix.push_back(*i);
      if (i == dirname.begin())
        continue;
      if (!ros.has_node(prefix))
        add_node_for(prefix);
    }

  add_node_for(sp);
}

void
perform_additions(path_set const & paths, app_state & app)
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
    // NB.: walk_tree will handle error checking for non-existent paths
    walk_tree(file_path(*i), build);

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

  for (path_set::const_reverse_iterator i = paths.rbegin(); i != paths.rend(); ++i)
    {
      file_path name(*i);

      if (!new_roster.has_node(*i))
        P(F("skipping %s, not currently tracked\n") % name);
      else
        {
          node_t n = new_roster.get_node(*i);
          if (is_dir_t(n))
            {
              dir_t d = downcast_to_dir_t(n);
              N(d->children.empty(),
                F("cannot remove %s/, it is not empty") % name);
            }
          P(F("adding %s to working copy delete set\n") % name);
          new_roster.drop_detached_node(new_roster.detach_node(*i));
          if (app.execute && path_exists(name))
            delete_file_or_dir_shallow(name);
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
perform_rename(file_path const & src_path,
               file_path const & dst_path,
               app_state & app)
{
  temp_node_id_source nis;
  roster_t base_roster, new_roster;
  split_path src, dst;

  get_base_and_current_roster_shape(base_roster, new_roster, nis, app);

  src_path.split(src);
  dst_path.split(dst);

  N(new_roster.has_node(src),
    F("%s does not exist in current revision\n") % src_path);

  N(!new_roster.has_node(dst),
    F("%s already exists in current revision\n") % dst_path);

  add_parent_dirs(dst, new_roster, nis, app);

  P(F("adding %s -> %s to working copy rename set\n") % src_path % dst_path);

  node_id nid = new_roster.detach_node(src);
  new_roster.attach_node(nid, dst);

  // this should fail if src doesn't exist or dst does
  if (app.execute && (path_exists(src_path) || !path_exists(dst_path)))
    move_path(src_path, dst_path);

  cset new_work;
  make_cset(base_roster, new_roster, new_work);
  put_work_cset(new_work);
  update_any_attrs(app);
}


// work file containing rearrangement from uncommitted adds/drops/renames

std::string const work_file_name("work");

static void get_work_path(bookkeeping_path & w_path)
{
  w_path = bookkeeping_root / work_file_name;
  L(F("work path is %s\n") % w_path);
}

void get_work_cset(cset & w)
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  if (path_exists(w_path))
    {
      L(F("checking for un-committed work file %s\n") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_cset(w_data, w);
      L(F("read cset from %s\n") % w_path);
    }
  else
    {
      L(F("no un-committed work file %s\n") % w_path);
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

std::string revision_file_name("revision");

static void get_revision_path(bookkeeping_path & m_path)
{
  m_path = bookkeeping_root / revision_file_name;
  L(F("revision path is %s\n") % m_path);
}

void get_revision_id(revision_id & c)
{
  c = revision_id();
  bookkeeping_path c_path;
  get_revision_path(c_path);

  require_path_is_file(c_path,
                       F("working copy is corrupt: %s does not exist") % c_path,
                       F("working copy is corrupt: %s is a directory") % c_path);

  data c_data;
  L(F("loading revision id from %s\n") % c_path);
  try
    {
      read_data(c_path, c_data);
    }
  catch(std::exception & e)
    {
      N(false, F("Problem with working directory: %s is unreadable") % c_path);
    }
  c = revision_id(remove_ws(c_data()));
}

void put_revision_id(revision_id const & rev)
{
  bookkeeping_path c_path;
  get_revision_path(c_path);
  L(F("writing revision id to %s\n") % c_path);
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
        F("base revision %s does not exist in database\n") % rid);
      
      app.db.get_roster(rid, ros, mm);
    }

  L(F("base roster has %d entries\n") % ros.all_nodes().size());
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
get_current_restricted_roster(roster_t & ros, node_id_source & nis, app_state & app)
{
  get_current_roster_shape(ros, nis, app);
  update_restricted_roster_from_filesystem(ros, app);
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

void
get_base_and_current_restricted_roster(roster_t & base_roster,
                                       roster_t & current_roster,
                                       node_id_source & nis,
                                       app_state & app)
{
  get_base_and_current_roster_shape(base_roster, current_roster, nis, app);
  update_restricted_roster_from_filesystem(current_roster, app);
}

// user log file

string const user_log_file_name("log");

void
get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(F("user log path is %s\n") % ul_path);
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

string const options_file_name("options");

void 
get_options_path(bookkeeping_path & o_path)
{
  o_path = bookkeeping_root / options_file_name;
  L(F("options path is %s\n") % o_path);
}

void 
read_options_map(data const & dat, options_map & options)
{
  basic_io::input_source src(dat(), "MT/options");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  // don't clear the options which will have settings from the command line
  // options.clear(); 

  std::string opt, val;
  while (parser.symp())
    {
      parser.sym(opt);
      parser.str(val);
      // options[opt] = val;      
      // use non-replacing insert verses replacing with options[opt] = val;
      options.insert(make_pair(opt, val)); 
    }
}

void 
write_options_map(data & dat, options_map const & options)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);

  basic_io::stanza st;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    st.push_str_pair(i->first, i->second());

  pr.print_stanza(st);
  dat = oss.str();
}

// local dump file

static string const local_dump_file_name("debug");

void get_local_dump_path(bookkeeping_path & d_path)
{
  d_path = bookkeeping_root / local_dump_file_name;
  L(F("local dump path is %s\n") % d_path);
}

// inodeprint file

static string const inodeprints_file_name("inodeprints");

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

string const encoding_attribute("mtn:encoding");
string const binary_encoding("binary");
string const default_encoding("default");

string const manual_merge_attribute("mtn:manual_merge");

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
      if (!app.restriction_includes(sp))
        continue;

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
  : app(app), source(source), next_nid(1)
{
}

void
move_path_if_not_already_present(any_path const & old_path,
                                 any_path const & new_path,
                                 app_state & app)
{
}

static inline bookkeeping_path
path_for_nid(node_id nid)
{
  return bookkeeping_root / "tmp" / boost::lexical_cast<std::string>(nid);
}

node_id
editable_working_tree::detach_node(split_path const & src)
{
  node_id nid = next_nid++;
  file_path src_pth(src);
  bookkeeping_path dst_pth = path_for_nid(nid);
  make_dir_for(dst_pth);
  move_path(src_pth, dst_pth);
  return nid;
}

void
editable_working_tree::drop_detached_node(node_id nid)
{
  bookkeeping_path pth = path_for_nid(nid);
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

  // Possibly just write data out into the working copy, if we're doing
  // a file-create (not a dir-create or file/dir rename).
  if (!file_exists(src_pth))
    {
      std::map<bookkeeping_path, file_id>::const_iterator i 
        = written_content.find(src_pth);
      if (i != written_content.end())
        {
          if (file_exists(dst_pth))
            {
              file_id dst_id;
              ident_existing_file(dst_pth, dst_id, app.lua);
              if (i->second == dst_id)
                return;
            }
          file_data dat;
          source.get_file_content(i->second, dat);
          write_localized_data(dst_pth, dat.inner(), app.lua);
          return;
        }
    }

  // If we get here, we're doing a file/dir rename, or a dir-create.
  switch (get_path_status(src_pth))
    {
    case path::nonexistent:
      I(false);
      break;
    case path::file:
      E(!file_exists(dst_pth),
        F("renaming '%s' onto existing file: '%s'\n") 
        % src_pth % dst_pth);
      break;
    case path::directory:
      if (directory_exists(dst_pth))
        return;
      break;
    }
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
    F("content of file '%s' has changed, not overwriting"));
  P(F("updating %s to %s") % pth_unsplit % new_id);

  file_data dat;
  source.get_file_content(new_id, dat);
  // FIXME_ROSTERS: inconsistent with file addition code above, and
  // write_localized_data is poorly designed anyway...
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

editable_working_tree::~editable_working_tree()
{
}
