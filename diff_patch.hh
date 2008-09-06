#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "rev_types.hh"
// XXX needed for gcc 3.3 which will otherwise complain that struct file_path
// is just a forward in rev_types.hh and therefor leads to an incomplete type
#include "paths.hh"

class database;
class lua_hooks;

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

  // For use when one side of the merge is dropped
  virtual void record_file(file_id const & parent_ident,
                           file_id const & merged_ident,
                           file_data const & parent_data,
                           file_data const & merged_data) = 0;

  virtual void get_ancestral_roster(node_id nid,
                                    revision_id & rid,
                                    boost::shared_ptr<roster_t const> & anc) = 0;

  virtual void get_version(file_id const & ident,
                           file_data & dat) const = 0;

  virtual ~content_merge_adaptor() {}
};

struct
content_merge_database_adaptor
  : public content_merge_adaptor
{
  database & db;
  revision_id lca;
  revision_id left_rid;
  revision_id right_rid;
  marking_map const & left_mm;
  marking_map const & right_mm;
  std::map<revision_id, boost::shared_ptr<roster_t const> > rosters;
  content_merge_database_adaptor(database & db,
                                 revision_id const & left,
                                 revision_id const & right,
                                 marking_map const & left_mm,
                                 marking_map const & right_mm);
  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void cache_roster(revision_id const & rid,
                    boost::shared_ptr<roster_t const> roster);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct
content_merge_workspace_adaptor
  : public content_merge_adaptor
{
  std::map<file_id, file_data> temporary_store;
  database & db;
  revision_id const lca;
  boost::shared_ptr<roster_t const> base;
  marking_map const & left_mm;
  marking_map const & right_mm;
  std::map<revision_id, boost::shared_ptr<roster_t const> > rosters;
  std::map<file_id, file_path> content_paths;
  content_merge_workspace_adaptor(database & db,
                                  revision_id const & lca,
                                  boost::shared_ptr<roster_t const> base,
                                  marking_map const & left_mm,
                                  marking_map const & right_mm,
                                  std::map<file_id, file_path> const & paths)
    : db(db), lca(lca), base(base),
      left_mm(left_mm), right_mm(right_mm), content_paths(paths)
  {}

  void cache_roster(revision_id const & rid,
                    boost::shared_ptr<roster_t const> roster);

  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;
};

struct
content_merge_checkout_adaptor
  : public content_merge_adaptor
{
  database & db;
  content_merge_checkout_adaptor(database & db)
    : db(db)
  {}

  void record_merge(file_id const & left_ident,
                    file_id const & right_ident,
                    file_id const & merged_ident,
                    file_data const & left_data,
                    file_data const & right_data,
                    file_data const & merged_data);

  void record_file(file_id const & parent_ident,
                   file_id const & merged_ident,
                   file_data const & parent_data,
                   file_data const & merged_data);

  void get_ancestral_roster(node_id nid,
                            revision_id & rid,
                            boost::shared_ptr<roster_t const> & anc);

  void get_version(file_id const & ident,
                   file_data & dat) const;

};


struct content_merger
{
  lua_hooks & lua;
  roster_t const & anc_ros;
  roster_t const & left_ros;
  roster_t const & right_ros;

  content_merge_adaptor & adaptor;

  content_merger(lua_hooks & lua,
                 roster_t const & anc_ros,
                 roster_t const & left_ros,
                 roster_t const & right_ros,
                 content_merge_adaptor & adaptor)
    : lua(lua),
      anc_ros(anc_ros),
      left_ros(left_ros),
      right_ros(right_ros),
      adaptor(adaptor)
  {}

  // Attempt merge3 on a file (line by line). Return true and valid data if
  // it would succeed; false and invalid data otherwise.
  bool attempt_auto_merge(file_path const & anc_path, // inputs
                          file_path const & left_path,
                          file_path const & right_path,
                          file_id const & ancestor_id,
                          file_id const & left_id,
                          file_id const & right_id,
                          file_data & left_data, // outputs
                          file_data & right_data,
                          file_data & merge_data);

  // Attempt merge3 on a file (line by line). If it succeeded, store results
  // in database and return true and valid merged_id; return false
  // otherwise.
  bool try_auto_merge(file_path const & anc_path,
                      file_path const & left_path,
                      file_path const & right_path,
                      file_path const & merged_path,
                      file_id const & ancestor_id,
                      file_id const & left_id,
                      file_id const & right,
                      file_id & merged_id);

  bool try_user_merge(file_path const & anc_path,
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
