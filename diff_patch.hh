#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "manifest.hh"
#include "vocab.hh"

#include <string>
#include <vector>
#include <iostream>

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

bool guess_binary(string const & s);

void unidiff(string const & filename1,
	     string const & filename2,
	     vector<string> const & lines1,
	     vector<string> const & lines2,
	     ostream & ost);

bool merge3(vector<string> const & ancestor,
	    vector<string> const & left,
	    vector<string> const & right,
	    vector<string> & merged);



struct path_id_pair;

// you pass one of these in to the next function, to give it a callback
// strategy for merging individual elements in a manifest.

struct file_merge_provider
{
  // merge3 on a file (line by line)
  virtual bool try_to_merge_files(path_id_pair const & ancestor,
				  path_id_pair const & left,
				  path_id_pair const & right,
				  path_id_pair & merged) = 0;

  // merge2 on a file (line by line)
  virtual bool try_to_merge_files(path_id_pair const & left,
				  path_id_pair const & right,
				  path_id_pair & merged) = 0;
};

struct simple_merge_provider : public file_merge_provider
{
  app_state & app;
  simple_merge_provider(app_state & app);
  virtual bool try_to_merge_files(path_id_pair const & ancestor,
				  path_id_pair const & left,
				  path_id_pair const & right,
				  path_id_pair & merged);
  virtual bool try_to_merge_files(path_id_pair const & left,
				  path_id_pair const & right,
				  path_id_pair & merged);
  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);
  virtual void get_right_version(path_id_pair const & pip, file_data & dat);

};

struct update_merge_provider : public simple_merge_provider
{
  map<file_id, file_data> temporary_store;
  update_merge_provider(app_state & app);
  virtual void record_merge(file_id const & left_ident, 
			    file_id const & right_ident, 
			    file_id const & merged_ident,
			    file_data const & left_data, 
			    file_data const & merged_data);
  virtual void get_right_version(path_id_pair const & pip, file_data & dat);
  virtual ~update_merge_provider() {}
};

// this does a set-theoretic merge3 on the manifests

bool merge3(manifest_map const & ancestor,
	    manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged);

// ditto but the weaker merge2

bool merge2(manifest_map const & left,
	    manifest_map const & right,
	    app_state & app,
	    file_merge_provider & file_merger,
	    manifest_map & merged);


#endif // __DIFF_PATCH_HH__
