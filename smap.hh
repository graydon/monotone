#ifndef __SMAP_HH__
#define __SMAP_HH__

// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <functional>
#include <numeric>
#include "vector.hh"


// this is a map that works by storing a sorted vector and doing binary
// search.  for maps that are filled once and then used many times, it is
// faster than other types of maps.  it's a derived version of stl_map.h from
// libstdc++; I *believe* deriving from its text and upgrading to GPL is
// kosher.

// this is _not_ fully compatible with a std::map; in fact, it's not even a
// Unique Sorted Associative Container.  this is because:
//   -- our 'insert' operations return void, rather than an iterator
//      (this could be fixed by creating a very clever sort of iterator, that
//      knew how to sort the map and then initialize itself when dereferenced)
//   -- if you 'insert' two items with the same key, then later on find() will
//      throw an assertion
//      (this could be fixed by using stable_sort instead of sort, and then
//      deleting duplicates instead of asserting.)
// it is, however, still close enough that STL algorithms generally work.

// FIXME: it is not clear how much of a win this really is; replacing our uses
// of it with hash_map's in change_set.cc caused an 8% slowdown, but that may
// have to do with smap using single_client_alloc and hash_map not.
// Re-evaluate when later gcc automatically uses single_client_alloc when
// possible...

// We don't use quick_alloc.hh's QA() macro here, because macros don't know
// about <>'s as grouping mechanisms, so it thinks the "," in the middle of
// std::pair<K, D> is breaking things into two arguments.
template<typename K, typename D,
         typename compare = std::less<K>,
#if defined(__GNUC__) && __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
         typename alloc = std::__allocator<std::pair<K, D>, std::__single_client_alloc> >
#else
         typename alloc = std::allocator<std::pair<K, D> > >
#endif
class
smap
{
public:
  typedef K key_type;
  typedef D mapped_type;
  typedef std::pair<key_type, mapped_type> value_type;
  typedef compare key_compare;

protected:
  class value_compare
    : public std::binary_function<value_type, value_type, bool>
  {
    friend class smap<K,D,compare>;
  protected:
    compare comp;
    value_compare(compare c) : comp(c) {}

  public:
    bool operator()(const value_type & a,
                    const value_type & b) const
    {
      return comp(a.first, b.first);
    }
  };

  value_compare val_cmp;

  typedef std::vector<value_type> vec_type;
  mutable vec_type vec;
  mutable bool damaged;

  inline void
  ensure_sort() const
  {
    if (damaged)
      {
        std::sort(vec.begin(), vec.end(), val_cmp);
        // make sure we don't have any duplicate entries
        const_iterator leader, lagged;
        lagged = vec.begin();
        leader = vec.begin();
        I(leader != vec.end());
        ++leader;
        for (; leader != vec.end(); ++lagged, ++leader)
          I(lagged->first != leader->first);
        damaged = false;
      }
  }

public:

  typedef alloc allocator_type;

  typedef typename alloc::pointer pointer;
  typedef typename alloc::const_pointer const_pointer;

  typedef typename alloc::reference reference;
  typedef typename alloc::const_reference const_reference;

  typedef typename vec_type::size_type size_type;
  typedef typename vec_type::difference_type difference_type;

  typedef typename vec_type::iterator iterator;
  typedef typename vec_type::const_iterator const_iterator;
  typedef typename vec_type::reverse_iterator reverse_iterator;
  typedef typename vec_type::const_reverse_iterator const_reverse_iterator;

  smap()
    : val_cmp(compare()),
      damaged(false)
  { };

  explicit
  smap(compare const & cmp,
       alloc const & a = alloc())
    : val_cmp(cmp),
      vec(a),
      damaged(false)
  { }

  smap(smap const & other)
    : val_cmp(other.val_cmp),
      vec(other.vec),
      damaged(other.damaged)
  { }

  template <typename InputIterator>
  smap(InputIterator first, InputIterator last)
    : damaged(false)
  {
    insert(first, last);
  }

  template <typename InputIterator>
  smap(InputIterator first, InputIterator last,
       compare const & cmp,
       alloc const & a = alloc())
    : val_cmp(cmp),
      vec(a),
      damaged(false)
  {
    insert(first, last);
  }

  smap &
  operator=(smap const & other)
  {
    val_cmp = other.val_cmp;
    vec = other.vec;
    damaged = other.damaged;
    return *this;
  }


  allocator_type get_allocator() const { return vec.get_allocator(); }

  iterator begin() { return vec.begin(); }
  iterator end() { return vec.end(); }
  reverse_iterator rbegin() { return vec.rbegin(); }
  reverse_iterator rend() { return vec.rend(); }

  const_iterator begin() const { return vec.begin(); }
  const_iterator end() const { return vec.end(); }
  const_reverse_iterator rbegin() const { return vec.rbegin(); }
  const_reverse_iterator rend() const { return vec.rend(); }

  bool empty() const { return vec.empty(); }
  size_type size() const { return vec.size(); }
  size_type max_size() const { return vec.max_size(); }

  mapped_type &
  operator[](const key_type & k)
  {
    iterator i = find(k);
    if (i != end() && i->first == k)
    {
      return i->second;
    }

    value_type v = std::make_pair(k, mapped_type());
    if (size() > 0 && val_cmp(v, *(begin() + size() - 1)))
    {
      damaged = true;
    }
    vec.push_back(v);
    return v.second;
  }

  void
  insert(value_type const & v)
  {
    I(size() == 0 || v.first != vec.back().first);
    if (size() > 0 && val_cmp(v, *(begin() + size() - 1)))
    {
      damaged = true;
    }
    vec.push_back(v);
  }

  void
  insert(iterator pos, value_type const & v)
  {
    insert(v);
  }

  template <typename InputIterator>
  void
  insert(InputIterator first, InputIterator last)
  {
    iterator i = begin();
    while (first != last)
    {
      i = insert(i, *first++);
    }
  }

  void
  erase(iterator i)
  {
    vec.erase(i);
  }

  size_type
  erase(key_type const & k)
  {
    iterator i = find(k);
    size_type c = 0;
    while (i != end() && i->first == k)
    {
      erase(i);
      ++c;
      ++i;
    }
    return c;
  }

  void
  erase(iterator first, iterator last)
  {
    while (first != last)
    {
      erase(first++);
    }
  }

  void
  swap(smap & x)
  {
    ensure_sort();
    x.ensure_sort();
    vec.swap(x.vec);
  }

  void
  clear()
  {
    vec.clear();
    damaged = false;
  }

  key_compare
  key_comp() const
  {
    return val_cmp.comp;
  }

  value_compare
  value_comp() const
  {
    return val_cmp;
  }

  iterator
  find(key_type const & k)
  {
    iterator i = lower_bound(k);
    if (i != end() && i->first == k)
    {
      return i;
    }
    return end();
  }

  const_iterator
  find(key_type const & k) const
  {
    // maybe-sort + binary search
    const_iterator i = lower_bound(k);
    if (i != end() && i->first == k)
    {
      return i;
    }
    return end();
  }

  size_type
  count(key_type const & k) const
  {
    return (find(k) == end() ? 0 : 1);
  }

  iterator
  lower_bound(key_type const & k)
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::lower_bound(begin(), end(), v, val_cmp);
  }

  const_iterator
  lower_bound(key_type const & k) const
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::lower_bound(begin(), end(), v, val_cmp);
  }

  iterator
  upper_bound(key_type const & k)
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::upper_bound(begin(), end(), v, val_cmp);
  }

  const_iterator
  upper_bound(key_type const & k) const
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::upper_bound(begin(), end(), v, val_cmp);
  }

  std::pair<iterator, iterator>
  equal_range(key_type const & k)
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::equal_range(begin(), end(), v, val_cmp);
  }

  std::pair<const_iterator, const_iterator>
  equal_range(key_type const & k) const
  {
    ensure_sort();
    value_type v(k, mapped_type());
    return std::equal_range(begin(), end(), v, val_cmp);
  }


  template <typename K1, typename T1, typename C1, typename A1>
  friend bool
  operator== (const smap<K1,T1,C1,A1>&,
              const smap<K1,T1,C1,A1>&);

  template <typename K1, typename T1, typename C1, typename A1>
  friend bool
  operator< (const smap<K1,T1,C1,A1>&,
             const smap<K1,T1,C1,A1>&);

};

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator==(smap<K, D, compare, alloc> const & a,
           smap<K, D, compare, alloc> const & b)
{
  a.ensure_sort();
  b.ensure_sort();
  return a.vec == b.vec;
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator<(smap<K, D, compare, alloc> const & a,
          smap<K, D, compare, alloc> const & b)
{
  a.ensure_sort();
  b.ensure_sort();
  return a.vec < b.vec;
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator!=(smap<K, D, compare, alloc> const & a,
           smap<K, D, compare, alloc> const & b)
{
  return !(a == b);
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator>(smap<K, D, compare, alloc> const & a,
          smap<K, D, compare, alloc> const & b)
{
  return b < a;
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator<=(smap<K, D, compare, alloc> const & a,
           smap<K, D, compare, alloc> const & b)
{
  return !(b < a);
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline bool
operator>=(smap<K, D, compare, alloc> const & a,
           smap<K, D, compare, alloc> const & b)
{
  return !(a < b);
}

template <typename K, typename D,
          typename compare,
          typename alloc>
inline void
swap(smap<K, D, compare, alloc> & a,
     smap<K, D, compare, alloc> & b)
{
  a.swap(b);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __SMAP_HH__
