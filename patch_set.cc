// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sstream>

#include "sanity.hh"
#include "patch_set.hh"
#include "file_io.hh"
#include "transforms.hh"
#include "packet.hh"

//
// a patch set is derived from a pair of manifests.
//
// for each addition between the manifests, we must decide if it represents
// a "new" file or a change from an old file.
// 
// the add is a change if any of these are true:
//   - there is a delete with an identical filename (a "true delta")
//   - there is a delete with an identical sha1 (a move)
//
// these are tried in order. logic for a deletion is symmetrical.
//
// for each true change, we calculate an rdiff and add it to the deltas
// section of the patch set.
//
// for each non-change add, we insert its full data in the adds section of
// the patch set.
//
// true deletes do not need to be explicitly mentionned outside of the
// manifest delta since their status is implicit from the presence of
// adds and deltas -- they are the only deletes not mentionned as deltas.
//
// we then packetize or summarize the data for our users
//

bool operator<(const patch_move & a, 
	       const patch_move & b)
{
  return ((a.path_old < b.path_old)
	  || ((a.path_old == b.path_old) && (a.path_new < b.path_new)));
}

bool operator<(const patch_addition & a, 
	       const patch_addition & b)
{
  return ((a.path < b.path) ||	  
	  ((a.path == b.path) && (a.ident < b.ident)));
}

bool operator<(const patch_delta & a, 
	       const patch_delta & b)
{
  return 
    (a.path < b.path) 
    || ((a.path == a.path) && a.id_new < a.id_new)
    || (((a.path == a.path) && a.id_new == a.id_new) && a.id_old < b.id_old);
}

patch_delta::patch_delta(file_id const & o, 
			 file_id const & n,
			 file_path const & p) 
  : id_old(o), id_new(n), path(p)
{}

patch_addition::patch_addition(file_id const & i, 
			       file_path const & p)
  : ident(i), path(p)
{}

patch_move::patch_move(file_path const & o, file_path const & n)
  : path_old(o), path_new(n)
{}


typedef set<entry>::const_iterator eci;

struct path_id_bijection
{
  size_t size()
  {
    I(forward.size() == backward.size());
    return forward.size();
  }
  
  void add(path_id_pair const & pip)
  {
    I(forward.size() == backward.size());
    // nb: we preserve the bijective property of this map at the expense of
    // possibly missing some add/delete matching cases. for example, if an
    // identical file is deleted as one path name and added as two new path
    // names, we will record it as an add + a move, rather than anything
    // more clever.
    if (exists(pip.path()) || exists(pip.ident()))
      return;
    forward.insert(make_pair(pip.path(), pip.ident()));
    backward.insert(make_pair(pip.ident(), pip.path()));
    I(forward.size() == backward.size());
  }

  void add(entry const & e)
  {
    add(path_id_pair(e));
  }
    
  void del(path_id_pair const & pip)
  {
    I(forward.size() > 0);
    I(backward.size() > 0);
    I(forward.size() == backward.size());
    if (exists(pip.path()))
      {
	file_id fid = get(pip.path());
	forward.erase(pip.path());
	backward.erase(fid);
      }
    else if (exists(pip.ident()))
      {
	file_path pth = get(pip.ident());
	forward.erase(pth);
	backward.erase(pip.ident());
      }
    else
      {
	I(false); // trip assert. you should not have done that.
      }
    I(forward.size() == backward.size());
  }

  void copy_to(set<patch_addition> & adds)
  {
    I(forward.size() == backward.size());
    I(adds.size() == 0);
    for (map<file_id, file_path>::const_iterator i = backward.begin();
	 i != backward.end(); ++i)
      {
	adds.insert(patch_addition(i->first, i->second));
      }
    I(adds.size() == backward.size());
    I(adds.size() == forward.size());    
  }
  
  void del(entry const & e)
  {
    del(path_id_pair(e));
  }

  bool exists(file_path const & p)
  {
    I(forward.size() == backward.size());
    return forward.find(p) != forward.end();
  }

  bool exists(file_id const & i)
  {
    I(forward.size() == backward.size());
    return backward.find(i) != backward.end();
  }
  
  file_id const & get(file_path const & path)
  {
    I(exists(path));
    return forward[path];
  }

  file_path const & get(file_id const & ident)
  {
    I(exists(ident));
    return backward[ident];
  }

  map<file_path, file_id > forward;
  map<file_id, file_path > backward;
};


static void index_adds(set<entry> const & adds,
		       path_id_bijection & mapping)
{
  for(eci i = adds.begin(); i != adds.end(); ++i)
    {
      L(F("indexing add: %s %s\n") % i->first % i->second);
      I(i->first() != "");
      I(i->second.inner()() != "");
      mapping.add(*i);
    }
}

static void classify_dels(set<entry> const & in_dels,
			  path_id_bijection & adds,		  
			  app_state & app,
			  set<file_path> & dels,
			  set<patch_move> & moves,
			  set<patch_delta> & deltas)
{
  size_t const initial_num_adds = adds.size();
  size_t const initial_num_dels = in_dels.size();

  for(eci i = in_dels.begin(); i != in_dels.end(); ++i)
    {
      I(adds.size() + moves.size() + deltas.size() == initial_num_adds);
      I(dels.size() + moves.size() + deltas.size() <= initial_num_dels);

      path_id_pair pip(*i);
      
      if (adds.exists(pip.path()))
	{
	  // there is an add which matches this delete
	  if (app.db.file_version_exists(pip.ident()))
	    {
	      // this is a "true delta"
	      L(F("found true delta %s\n") % pip.path());
	      file_id new_id = adds.get(pip.path());
	      deltas.insert(patch_delta(pip.ident(), new_id, pip.path()));
	      adds.del(pip);
	    } 
	  else
	    {
	      // this is a recoverable error: treat as a true delete
	      // (accompanied by a true insert)
	      L(F("found probable delta %s %s but no pre-version in database\n") 
		% pip.path() % pip.ident());
	      dels.insert(pip.path());
	    }
	}
      else if (adds.exists(pip.ident()))
	{
	  // there is a matching add of a file with the same id, so this is
	  // a "simple delta" (a move)
	  file_path dest = adds.get(pip.ident());
	  L(F("found move %s -> %s\n") % pip.path() % dest);
	  moves.insert(patch_move(pip.path(), dest));
	  adds.del(pip);
	}
      else
	{
	  // this is a "true delete"
	  L(F("found delete %s\n") % pip.path());
	  dels.insert(pip.path());
	}
    }
}


void manifests_to_patch_set(manifest_map const & m_old,
			    manifest_map const & m_new,
			    app_state & app,
			    patch_set & ps)
{
  ps.m_old = hexenc<id>();
  ps.m_new = hexenc<id>();
  ps.f_adds.clear();
  ps.f_deltas.clear();
  ps.f_moves.clear();
  ps.f_dels.clear();

  // calculate ids
  calculate_manifest_map_ident(m_old, ps.m_old);
  calculate_manifest_map_ident(m_new, ps.m_new);  
  L(F("building patch set %s -> %s\n") % ps.m_old % ps.m_new);

  // calculate manifest_changes structure
  manifest_changes changes;
  calculate_manifest_changes(m_old, m_new, changes);
  L(F("constructed manifest_changes (%d dels, %d adds)\n")
    % changes.dels.size() % changes.adds.size());
  
  // analyze adds and dels in manifest_changes
  path_id_bijection add_mapping;
  index_adds(changes.adds, add_mapping);
  size_t num_add_candidates = add_mapping.size();
  classify_dels(changes.dels, add_mapping, app,
		ps.f_dels, ps.f_moves, ps.f_deltas);  
  
  // now copy any remaining unmatched adds into ps.f_adds
  add_mapping.copy_to(ps.f_adds);

  // all done, log and assert to be sure.
  if (ps.f_adds.size() > 0)
    L(F("found %d plain additions\n") % ps.f_adds.size());
  if (ps.f_dels.size() > 0)
    L(F("found %d plain deletes\n") % ps.f_dels.size());  
  if (ps.f_deltas.size() > 0)
    L(F("matched %d del/add pairs as deltas\n") % ps.f_deltas.size());  
  if (ps.f_moves.size() > 0)
    L(F("matched %d del/add pairs as moves\n") % ps.f_moves.size());
  I(ps.f_dels.size() + ps.f_moves.size() + ps.f_deltas.size() == changes.dels.size());
  I(ps.f_adds.size() + ps.f_moves.size() + ps.f_deltas.size() == num_add_candidates);
}



// this produces an imprecise, textual summary of the patch set
void patch_set_to_text_summary(patch_set const & ps, 
			       ostream & str)
{
  for (set<file_path>::const_iterator i = ps.f_dels.begin();
       i != ps.f_dels.end(); ++i)
    str << "  delete " << (*i)() << endl;

  for (set<patch_addition >::const_iterator i = ps.f_adds.begin();
       i != ps.f_adds.end(); ++i)
    str << "  add " << i->path() << " as " << i->ident.inner()() << endl;

  for (set<patch_move>::const_iterator i = ps.f_moves.begin();
       i != ps.f_moves.end(); ++i)
    str << "  move " << i->path_old() << " -> " << i->path_new() << endl;

  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    str << "  patch " << i->path() << " " << i->id_old.inner()() << " -> " << i->id_new.inner()() << endl;
}


void patch_set_to_packets(patch_set const & ps,
			  app_state & app,
			  packet_consumer & cons)
{
  
  // manifest delta packet
  manifest_data m_old_data, m_new_data;

  I(app.db.manifest_version_exists(ps.m_new));
  app.db.get_manifest_version(ps.m_new, m_new_data);
  
  if (app.db.manifest_version_exists(ps.m_old))
    {
      app.db.get_manifest_version(ps.m_old, m_old_data);
      base64< gzip<delta> > del;
      diff(m_old_data.inner(), m_new_data.inner(), del);
      cons.consume_manifest_delta(ps.m_old, ps.m_new, manifest_delta(del));
    }
  else
    {
      cons.consume_manifest_data(ps.m_new, m_new_data);
    }
  
  // new file packets
  for (set<patch_addition>::const_iterator i = ps.f_adds.begin();
       i != ps.f_adds.end(); ++i)
    {
      file_data dat;
      app.db.get_file_version(i->ident, dat);
      cons.consume_file_data(i->ident, dat);
    }
  
  // file delta packets
  for (set<patch_delta>::const_iterator i = ps.f_deltas.begin();
       i != ps.f_deltas.end(); ++i)
    {
      file_data old_data, new_data;
      I(app.db.file_version_exists(i->id_new));
      app.db.get_file_version(i->id_new, new_data);
      if (app.db.file_version_exists(i->id_old))
	{
	  app.db.get_file_version(i->id_old, old_data);
	  base64< gzip<delta> > del;
	  diff(old_data.inner(), new_data.inner(), del);
	  cons.consume_file_delta(i->id_old, i->id_new, file_delta(del));
	}
      else
	{
	  cons.consume_file_data(i->id_new, new_data);
	}
    }
}
