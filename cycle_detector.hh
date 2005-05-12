#ifndef __CYCLE_DETECTOR_HH__
#define __CYCLE_DETECTOR_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <stack>
#include <set>

#include "quick_alloc.hh"
#include "sanity.hh"

template <typename T>
struct cycle_detector
{
  
  typedef std::vector< T > edge_vec;
  typedef std::vector <edge_vec > edge_map;
  typedef std::pair <typename edge_vec::const_iterator, 
                     typename edge_vec::const_iterator> state;
  typedef std::stack <state > edge_stack;
  
  edge_map edges;
  edge_stack stk;
  std::set<T> global_in_edges;

  void put_edge (T const & src, T const & dst)
  {
    if (src >= edges.size())
      edges.resize(src + 1);
    edge_vec & src_edges = edges.at(src);
    for (typename edge_vec::const_iterator i = src_edges.begin();
         i != src_edges.end(); ++i)
      if (*i == dst)
        return;
    src_edges.push_back(dst);
    global_in_edges.insert(dst);
  }
  

  bool edge_makes_cycle(T const & src, T const & dst)
  {
    if (src == dst)
        return true;

    if (dst >= edges.size() || edges.at(dst).empty())
        return false;

    if (global_in_edges.find(src) == global_in_edges.end())
        return false;

    while (!stk.empty())
      stk.pop();

    stk.push(make_pair(edges.at(dst).begin(),
                       edges.at(dst).end()));

    std::set<T> visited;
    while (!stk.empty())
      {
        bool pushed = false;
        for (state & curr = stk.top(); curr.first != curr.second && !pushed; ++curr.first)
          {
            T val = *(curr.first);
            if (val == src)
              {
                return true;
              }
            if (val < edges.size() && ! edges.at(val).empty() 
                && visited.find(val) == visited.end())
              {
                visited.insert(val);
                stk.push(make_pair(edges.at(val).begin(),
                                   edges.at(val).end()));
                pushed = true;
              }
          }
        if (!pushed)
            stk.pop();
      }
    return false;
  }
};

#endif // __CYCLE_DETECTOR_HH__
