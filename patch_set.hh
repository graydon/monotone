#ifndef __PATCH_SET_HH__
#define __PATCH_SET_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>
#include <map>
#include <vector>

#include "vocab.hh"
#include "manifest.hh"
#include "app_state.hh"
#include "packet.hh"

// this file defines the algorithms responsible for analyzing and
// classifying changes to manifests and files. it also constructs packet
// sequences and textual summaries representing a given set of classified
// changes.

struct patch_delta
{
  patch_delta(file_id const & o, 
	      file_id const & n,
	      file_path const & p);
  file_id id_old;
  file_id id_new;
  // this is necessary for the case when someone (eg. "commit") wants to
  // pull the post-image of a patch_delta from the filesystem which is not,
  // alas, addressable by sha-1 <g>
  file_path path; 
};

struct patch_addition
{
  patch_addition(file_id const & i, file_path const & p);
  file_id ident;
  file_path path;
};

struct patch_move
{
  patch_move(file_path const & o, file_path const & n);
  file_path path_old;
  file_path path_new;
};

struct patch_set
{
  manifest_id m_old;
  manifest_id m_new;
  std::set<patch_addition> f_adds;
  std::set<patch_delta> f_deltas;
  std::set<patch_move> f_moves;
  std::set<file_path> f_dels;
};

void manifests_to_patch_set(manifest_map const & m_old,
			    manifest_map const & m_new,
			    app_state & app,
			    patch_set & ps);

void patch_set_to_text_summary(patch_set const & ps, 
			       std::ostream & str);

void patch_set_to_packets(patch_set const & ps, 
			  app_state & app,
			  packet_consumer & cons);

bool operator<(const patch_addition & a, 
	       const patch_addition & b);

bool operator<(const patch_move & a, 
	       const patch_move & b);

bool operator<(const patch_delta & a, 
	       const patch_delta & b);

#endif // __PATCH_SET_HH__
