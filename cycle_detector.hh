#ifndef __CYCLE_DETECTOR_HH__
#define __CYCLE_DETECTOR_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <set>

template <typename T>
struct cycle_detector
{
  typedef multimap <T, T> edge_map;
  edge_map edges;

  void put_edge (T const & src, T const & dst)
  {
    edges.insert(make_pair(src, dst));
  }

  bool dfs_cycle(set<T> & parents, T const & curr)
  {
    if (parents.find(curr) != parents.end())
      return true;
    else
      {
	parents.insert(curr);
	pair<
	  typename edge_map::const_iterator, 
	  typename edge_map::const_iterator > 
	  range = edges.equal_range (curr);
	
	for (typename edge_map::const_iterator i = range.first; 
	     i != range.second; ++i)
	  {
	    if (dfs_cycle (parents, i->second))
	      return true;
	  }
	parents.erase(curr);
      }
    return false;
  } 

  bool edge_makes_cycle(T const & src, T const & dst)
  {

    if (src == dst)
      return true;
    
    set<T> seen;
    seen.insert(src);
    return dfs_cycle (seen, dst);
  }
};

#endif // __CYCLE_DETECTOR_HH__
