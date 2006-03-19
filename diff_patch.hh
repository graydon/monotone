#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "cert.hh"
#include "vocab.hh"

#include <boost/shared_ptr.hpp>

#include <iostream>
#include <map>
#include <string>
#include <vector>

struct roster_t;

struct conflict {};

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

void make_diff(std::string const & filename1,
               std::string const & filename2,
               file_id const & id1,
               file_id const & id2,
               std::vector<std::string> const & lines1,
               std::vector<std::string> const & lines2,
               std::ostream & ost,
               diff_type type);

bool merge3(std::vector<std::string> const & ancestor,
            std::vector<std::string> const & left,
            std::vector<std::string> const & right,
            std::vector<std::string> & merged);

struct 
content_merge_adaptor
{
  virtual void record_merge(file_id const & left_ident, 
                            file_id const & right_ident, 
                            file_id const & merged_ident,
                            file_data const & left_data, 
                            file_data const & merged_data) = 0;

  virtual void get_ancestral_roster(node_id nid,
                                    boost::shared_ptr<roster_t> & anc) = 0;
  
  virtual void get_version(file_path const & path,
                           file_id const & ident,                           
                           file_data & dat) = 0;

  virtual ~content_merge_adaptor() {}
};

struct
content_merge_database_adaptor 
  : public content_merge_adaptor
{
  app_state & app;
  revision_id lca;
  marking_map const & mm;
  std::map<revision_id, boost::shared_ptr<roster_t> > rosters;
  content_merge_database_adaptor (app_state & app,
                                  revision_id const & left,
                                  revision_id const & right,
                                  marking_map const & mm);
  void record_merge(file_id const & left_ident, 
                    file_id const & right_ident, 
                    file_id const & merged_ident,
                    file_data const & left_data, 
                    file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            boost::shared_ptr<roster_t> & anc);
  
  void get_version(file_path const & path,
                   file_id const & ident,                           
                   file_data & dat);
};

struct
content_merge_workspace_adaptor
  : public content_merge_adaptor
{
  std::map<file_id, file_data> temporary_store;
  app_state & app;
  boost::shared_ptr<roster_t> base;
  content_merge_workspace_adaptor (app_state & app,
                                   boost::shared_ptr<roster_t> base) 
    : app(app), base(base) 
  {}
  void record_merge(file_id const & left_ident, 
                    file_id const & right_ident, 
                    file_id const & merged_ident,
                    file_data const & left_data, 
                    file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            boost::shared_ptr<roster_t> & anc);
  
  void get_version(file_path const & path,
                   file_id const & ident,                           
                   file_data & dat);
};

struct content_merger
{
  app_state & app;
  roster_t const & anc_ros;
  roster_t const & left_ros;
  roster_t const & right_ros;

  content_merge_adaptor & adaptor;

  content_merger(app_state & app,
                 roster_t const & anc_ros,
                 roster_t const & left_ros, 
                 roster_t const & right_ros,
                 content_merge_adaptor & adaptor);

  // merge3 on a file (line by line)
  bool try_to_merge_files(file_path const & anc_path,
                          file_path const & left_path,
                          file_path const & right_path,
                          file_path const & merged_path,
                          file_id const & ancestor_id,
                          file_id const & left_id,
                          file_id const & right,
                          file_id & merged_id);

  std::string get_file_encoding(file_path const & path,
                                roster_t const & ros);
  
  bool attribute_manual_merge(file_path const & path,
                              roster_t const & ros);
};

#endif // __DIFF_PATCH_HH__
