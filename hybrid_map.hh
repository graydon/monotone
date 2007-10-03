#ifndef __HYBRID_MAP_HH__
#define __HYBRID_MAP_HH__

// Copyright 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "hash_map.hh"
#include <map>

template<typename Key, typename Val>
class hybrid_map
{
  typedef std::map<Key, Val> omap;
  typedef hashmap::hash_map<Key, Val> umap;
  omap ordered;
  umap unordered;
public:
  typedef typename omap::value_type value_type;
  typedef typename omap::key_type key_type;
  typedef typename omap::mapped_type mapped_type;
  typedef typename omap::size_type size_type;
  class const_iterator;
  class iterator
  {
    friend class hybrid_map<Key, Val>;
    bool ordered;
    bool valid;
    typename omap::iterator o;
    typename umap::iterator u;
    hybrid_map<Key, Val> * n;
    friend class const_iterator;
  public:
    iterator(hybrid_map<Key, Val> * n, typename omap::iterator const & i)
      : ordered(true), valid(i != n->ordered.end()), o(i), n(n)
    {}
    iterator(hybrid_map<Key, Val> * n, typename umap::iterator const & i)
      : ordered(false), valid(i != n->unordered.end()), u(i), n(n)
    {}
    iterator()
      : valid(false)
    {}
    iterator & operator++()
    {
      I(valid);
      if (!ordered)
        {
          ordered = true;
          o = n->ordered.find(u->first);
        }
      ++o;
      valid = (o != n->ordered.end());
      return *this;
    }
    iterator operator++(int)
    {
      iterator out = *this;
      ++*this;
      return out;
    }
    value_type & operator*()
    {
      I(valid);
      if (ordered)
        return *o;
      else
        return *u;
    }
    value_type const & operator*() const
    {
      I(valid);
      if (ordered)
        return *o;
      else
        return *u;
    }
    value_type * operator->()
    {
      I(valid);
      if (ordered)
        return o.operator->();
      else
        return u.operator->();
    }
    value_type const * operator->() const
    {
      I(valid);
      if (ordered)
        return o.operator->();
      else
        return u.operator->();
    }
    bool operator==(iterator const & other) const
    {
      return (valid == other.valid) && (!valid || (*this)->first == other->first);
    }
    bool operator!=(iterator const & other) const
    {
      return !(*this == other);
    }
  };
  class const_iterator
  {
    friend class hybrid_map<Key, Val>;
    bool ordered;
    bool valid;
    typename omap::const_iterator o;
    typename umap::const_iterator u;
    hybrid_map<Key, Val> const * n;
  public:
    const_iterator(hybrid_map<Key, Val> const * n,
		   typename omap::const_iterator const & i)
      : ordered(true), valid(i != n->ordered.end()), o(i), n(n)
    {}
    const_iterator(hybrid_map<Key, Val> const * n,
		   typename umap::const_iterator const & i)
      : ordered(false), valid(i != n->unordered.end()), u(i), n(n)
    {}
    const_iterator(iterator const & i)
      : ordered(i.ordered), valid(i.valid), o(i.o), u(i.u), n(i.n)
    {}
    const_iterator()
      : valid(false)
    {}
    const_iterator & operator++()
    {
      I(valid);
      if (!ordered)
        {
          ordered = true;
          o = n->ordered.find(u->first);
        }
      ++o;
      valid = (o != n->ordered.end());
      return *this;
    }
    const_iterator operator++(int)
    {
      const_iterator out = *this;
      ++*this;
      return out;
    }
    value_type const & operator*() const
    {
      I(valid);
      if (ordered)
        return *o;
      else
        return *u;
    }
    value_type const * operator->() const
    {
      I(valid);
      if (ordered)
        return o.operator->();
      else
        return u.operator->();
    }
    bool operator==(const_iterator const & other) const
    {
      return (valid == other.valid) && (!valid || (*this)->first == other->first);
    }
    bool operator!=(const_iterator const & other) const
    {
      return !(*this == other);
    }
  };
  friend class iterator;
  friend class const_iterator;
  iterator begin()
  {
    return iterator(this, ordered.begin());
  }
  iterator end()
  {
    return iterator(this, ordered.end());
  }
  iterator find(key_type const & k)
  {
    return iterator(this, unordered.find(k));
  }
  const_iterator begin() const
  {
    return const_iterator(this, ordered.begin());
  }
  const_iterator end() const
  {
    return const_iterator(this, ordered.end());
  }
  const_iterator find(key_type const & k) const
  {
    return const_iterator(this, unordered.find(k));
  }
  size_t size() const
  {
    return ordered.size();
  }
  std::pair<iterator, bool> insert(value_type const & x)
  {
    std::pair<typename omap::iterator, bool> o = ordered.insert(x);
    unordered.insert(x);
    std::pair<iterator, bool> out;
    out.first = iterator(this, o.first);
    out.second = o.second;
    return out;
  }
  iterator insert(iterator const & where, value_type const & what)
  {
    unordered.insert(what);
    if (where.valid && where.ordered)
      {
        return iterator(this, ordered.insert(where.o, what));
      }
    else if (!where.valid)
      {
        return iterator(this, ordered.insert(ordered.end(), what));
      }
    else
      {
        std::pair<typename omap::iterator, bool> o = ordered.insert(what);
        return iterator(this, o.first);
      }
  }
  size_t erase(key_type const & x)
  {
    size_t out = ordered.erase(x);
    unordered.erase(x);
    return out;
  }
  bool empty() const
  {
    return ordered.empty();
  }
  void clear()
  {
    ordered.clear();
    unordered.clear();
  }
};


#endif
