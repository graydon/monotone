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
  if (app.restriction_includes(path) && known.find(sp) == known.end())
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

/*
// FIXME_ROSTERS: disabled until rewritten to use rosters

void 
build_rename(file_path const & src,
             file_path const & dst,
             manifest_map const & man,
             app_state & app,
             change_set::path_rearrangement & pr)
{
  N(!src.empty(), F("invalid source path ''"));
  N(!dst.empty(), F("invalid destination path ''"));

  change_set::path_rearrangement pr_new, pr_concatenated;
  path_set ps;
  extract_path_set(man, ps);
  apply_path_rearrangement(pr, ps);    

  bool src_dir_p = false;
  bool dst_dir_p = false;

  N(known_path(src, ps, src_dir_p), 
    F("%s does not exist in current revision\n") % src);

  N(!known_path(dst, ps, dst_dir_p), 
    F("%s already exists in current revision\n") % dst);

  P(F("adding %s -> %s to working copy rename set\n") % src % dst);
  if (src_dir_p)
    pr_new.renamed_dirs.insert(std::make_pair(src, dst));
  else 
    pr_new.renamed_files.insert(std::make_pair(src, dst));

  if (app.execute && (path_exists(src) || !path_exists(dst)))
    move_path(src, dst);

  // read attribute map if available
  file_path attr_path;
  get_attr_path(attr_path);

  if (path_exists(attr_path))
  {
    data attr_data;
    read_data(attr_path, attr_data);
    attr_map attrs;
    read_attr_map(attr_data, attrs);

    // make sure there aren't pre-existing attributes that we'd accidentally
    // pick up
    N(attrs.find(dst) == attrs.end(), 
      F("%s has existing attributes in %s; clean them up first") % dst % attr_file_name);

    // only write out a new attribute map if we find attrs to move
    attr_map::iterator a = attrs.find(src);
    if (a != attrs.end())
    {
      attrs[dst] = (*a).second;
      attrs.erase(a);

      P(F("moving attributes for %s to %s\n") % src % dst);

      write_attr_map(attr_data, attrs);
      write_data(attr_path, attr_data);
    }
  }

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
}
*/

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
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "MT/options");
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

void 
get_attr_path(file_path & a_path)
{
  a_path = file_path_internal(attr_file_name);
  L(F("attribute map path is %s\n") % a_path);
}

namespace
{
  namespace syms
  {
    std::string const file("file");
  }
}

void 
read_attr_map(data const & dat, attr_map & attr)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, attr_file_name);
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  std::string file, name, value;

  attr.clear();

  while (parser.symp(syms::file))
    {
      parser.sym();
      parser.str(file);
      file_path fp = file_path_internal(file);

      while (parser.symp() && 
             !parser.symp(syms::file)) 
        {
          parser.sym(name);
          parser.str(value);
          attr[fp][name] = value;
        }
    }
}

void 
write_attr_map(data & dat, attr_map const & attr)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::file, i->first.as_internal());

      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
          st.push_str_pair(j->first, j->second);          

      pr.print_stanza(st);
    }

  dat = oss.str();
}


static void 
apply_attributes(app_state & app, attr_map const & attr)
{
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        app.lua.hook_apply_attribute (j->first,
                                      i->first, 
                                      j->second);
}

string const encoding_attribute("encoding");
string const binary_encoding("binary");
string const default_encoding("default");

string const manual_merge_attribute("manual_merge");

static bool find_in_attr_map(attr_map const & attr,
                             file_path const & file,
                             std::string const & attr_key,
                             std::string & attr_val)
{
  attr_map::const_iterator f = attr.find(file);
  if (f == attr.end())
    return false;

  std::map<std::string, std::string>::const_iterator a = f->second.find(attr_key);
  if (a == f->second.end())
    return false;

  attr_val = a->second;
  return true;
}

bool get_attribute_from_db(file_path const & file,
                           std::string const & attr_key,
                           manifest_map const & man,
                           std::string & attr_val,
                           app_state & app)
{
  file_path fp;
  get_attr_path(fp);
  manifest_map::const_iterator i = man.find(fp);
  if (i == man.end())
    return false;

  file_id fid = manifest_entry_id(i);
  if (!app.db.file_version_exists(fid))
    return false;

  file_data attr_data;
  app.db.get_file_version(fid, attr_data);

  attr_map attr;
  read_attr_map(data(attr_data.inner()()), attr);

  return find_in_attr_map(attr, file, attr_key, attr_val);
}

bool get_attribute_from_working_copy(file_path const & file,
                                     std::string const & attr_key,
                                     std::string & attr_val)
{
  file_path fp;
  get_attr_path(fp);
  if (!file_exists(fp))
    return false;
  
  data attr_data;
  read_data(fp, attr_data);

  attr_map attr;
  read_attr_map(attr_data, attr);

  return find_in_attr_map(attr, file, attr_key, attr_val);
}

void update_any_attrs(app_state & app)
{
/*
// FIXME_ROSTERS: disabled until rewritten to use rosters
  file_path fp;
  data attr_data;
  attr_map attr;

  get_attr_path(fp);
  if (!file_exists(fp))
    return;

  read_data(fp, attr_data);
  read_attr_map(attr_data, attr);
  apply_attributes(app, attr);
*/
}
