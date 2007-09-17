#ifndef __CYCLE_DETECTOR_HH__
#define __CYCLE_DETECTOR_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vector.hh"
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CYCLE_DETECTOR_HH__
