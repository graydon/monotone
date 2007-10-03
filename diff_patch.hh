#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include "roster.hh"

#include <boost/shared_ptr.hpp>

#include <map>
#include "vector.hh"

class app_state;

struct conflict {};

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

void make_diff(std::string const & filename1,
               std::string const & filename2,
               file_id const & id1,
               file_id const & id2,
               data const & data1,
               data const & data2,
               std::ostream & ost,
               diff_type type,
               std::string const & pattern);

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
                            file_data const & right_data,
                            file_data const & merged_data) = 0;

  virtual void get_ancestral_roster(node_id nid,
                                    boost::shared_ptr<roster_t const> & anc) = 0;

  virtual void get_version(file_id const & ident,
                           file_data & dat) const = 0;

  virtual ~content_merge_adaptor() {}
};

struct
content_merge_database_adaptor
  : public content_merge_adaptor
{
  app_state & app;
  revision_id lca;
  marking_map const & mm;
  std::map<revision_id, boost::shared_ptr<roster_t const> > rosters;
  content_merge_database_adaptor (app_state & app,
                                  revision_id const & left,
                                  revision_id const & right,
                                  marking_map const & mm);
  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct
content_merge_workspace_adaptor
  : public content_merge_adaptor
{
  std::map<file_id, file_data> temporary_store;
  app_state & app;
  boost::shared_ptr<roster_t const> base;
  std::map<file_id, file_path> content_paths;
  content_merge_workspace_adaptor (app_state & app,
                                   boost::shared_ptr<roster_t const> base,
                                   std::map<file_id, file_path> const & paths)
    : app(app), base(base), content_paths(paths)
  {}
  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __DIFF_PATCH_HH__
