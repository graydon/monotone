#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "cert.hh"
#include "manifest.hh"
#include "vocab.hh"

#include <string>
#include <vector>
#include <iostream>

struct conflict {};

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

bool guess_binary(std::string const & s);

void unidiff(std::string const & filename1,
	     std::string const & filename2,
	     std::vector<std::string> const & lines1,
	     std::vector<std::string> const & lines2,
	     std::ostream & ost);

bool merge3(std::vector<std::string> const & ancestor,
	    std::vector<std::string> const & left,
	    std::vector<std::string> const & right,
	    std::vector<std::string> & merged);



struct path_id_pair;

// you pass one of these in to the next function, to give it a callback
// strategy for merging individual elements in a manifest.

struct file_merge_provider
{
  // merge3 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & ancestor,
				  file_id const & left,
				  file_id const & right,
				  file_id & merged) = 0;

  // merge2 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & left,
				  file_id const & right,
				  file_id & merged) = 0;
};

struct simple_merge_provider : public file_merge_provider
{
  app_state & app;
  simple_merge_provider(app_state & app);
  // merge3 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & ancestor,
				  file_id const & left,
				  file_id const & right,
				  file_id & merged) = 0;

  // merge2 on a file (line by line)
  virtual bool try_to_merge_files(file_path const & path,
				  file_id const & left,
				  file_id const & right,
				  file_id & merged) = 0;
  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);
  virtual void get_version(file_path const & path,
			   file_id const & id,			   
			   file_data & dat);

};

struct update_merge_provider : public simple_merge_provider
{
  std::map<file_id, file_data> temporary_store;
  update_merge_provider(app_state & app);
  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);
  virtual void get_version(file_path const & path,
			   file_id const & id,			   
			   file_data & dat);
  virtual ~update_merge_provider() {}
};

// this does a set-theoretic merge3 on the manifests

bool merge3(manifest_map const & ancestor,
	    manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged,
	    rename_set & left_renames,
	    rename_set & right_renames);

// ditto but the weaker merge2

bool merge2(manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged);


#endif // __DIFF_PATCH_HH__
