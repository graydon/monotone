#ifndef __SMAP_HH__
#define __SMAP_HH__

// copyright (C) 2005 graydon hoare <graydon@pobox.com>,
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <functional>
#include <numeric>
#include <vector>

#include "quick_alloc.hh"

// this is a linear-for-smaller-maps, sorted-binary-search-for-larger maps
// map. for small key sets which don't change much it's faster than other
// types of maps. it's a derived version of stl_map.h from libstdc++; I
// *believe* deriving from its text and upgrading to GPL is kosher.

template<typename _K, typename _D, 
         typename _compare = std::less<_K>,
#if defined(__GNUC__) && __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
         typename _alloc = std::__allocator<std::pair<_K, _D>, std::__single_client_alloc> > 
#else 
         typename _alloc = std::allocator<std::pair<_K, _D> > > 
#endif
class
smap
{
public:
  typedef _K key_type;
  typedef _D mapped_type;
  typedef std::pair<key_type, mapped_type> value_type;
  typedef _compare key_compare;

protected:
  class value_compare
    : public std::binary_function<value_type, value_type, bool>
  {
    friend class smap<_K,_D,_compare>;
  protected:
    _compare _comp;    
    value_compare(_compare _c) : _comp(_c) {}
    
  public:
    bool operator()(const value_type & _a, 
                    const value_type & _b) const
    { 
      return _comp(_a.first, _b.first); 
    }
  };

  value_compare _val_cmp;

  typedef std::vector<value_type> _vec_type;  
  mutable _vec_type _vec;
  mutable bool _damaged;

  inline void 
  _ensure_sort() const
  {
    if (_damaged)
      {
        std::sort(_vec.begin(), _vec.end(), _val_cmp);
        _damaged = false;
      }
  }

public:

  typedef _alloc allocator_type;

  typedef typename _alloc::pointer pointer;
  typedef typename _alloc::const_pointer const_pointer;

  typedef typename _alloc::reference reference;
  typedef typename _alloc::const_reference const_reference;

  typedef typename _vec_type::size_type size_type;
  typedef typename _vec_type::difference_type difference_type;

  typedef typename _vec_type::iterator iterator;
  typedef typename _vec_type::const_iterator const_iterator;
  typedef typename _vec_type::reverse_iterator reverse_iterator;
  typedef typename _vec_type::const_reverse_iterator const_reverse_iterator;  

  smap() 
    : _val_cmp(_compare()), 
      _damaged(false) 
  { };
  
  explicit
  smap(_compare const & _cmp, 
       _alloc const & _a = _alloc()) 
    : _val_cmp(_cmp), 
      _vec(_a),
      _damaged(false)
  { }
  
  smap(smap const & _other)
    : _val_cmp(_other._val_cmp),       
      _vec(_other._vec),
      _damaged(_other._damaged)       
  { }

  template <typename _InputIterator>
  smap(_InputIterator _first, _InputIterator _last)
    : _damaged(false)
  { 
    insert(_first, _last); 
  }
  
  template <typename _InputIterator>
  smap(_InputIterator _first, _InputIterator _last,
       _compare const & _cmp, 
       _alloc const & _a = _alloc())
    : _val_cmp(_cmp),
      _vec(_a),
      _damaged(false)      
  { 
    insert(_first, _last); 
  }

  smap &
  operator=(smap const & _other)
  {
    _val_cmp = _other._val_cmp;
    _vec = _other._vec;
    _damaged = _other._damaged;
    return *this;
  }


  allocator_type get_allocator() const { return _vec.get_allocator(); }
  
  iterator begin() { return _vec.begin(); }
  iterator end() { return _vec.end(); }
  iterator rbegin() { return _vec.rbegin(); }
  iterator rend() { return _vec.rend(); }

  const_iterator begin() const { return _vec.begin(); }
  const_iterator end() const { return _vec.end(); }
  const_iterator rbegin() const { return _vec.rbegin(); }
  const_iterator rend() const { return _vec.rend(); }

  bool empty() const { return _vec.empty(); }
  size_type size() const { return _vec.size(); }
  size_type max_size() const { return _vec.max_size(); }

  mapped_type &
  operator[](const key_type & _k)
  {
    iterator i = find(_k);
    if (i != end() && i->first == _k)
    {
      return i->second;
    }

    value_type _v = std::make_pair(_k, mapped_type());
    if (size() > 0 && _val_cmp(_v, *(begin() + size() - 1)))
    {
      _damaged = true;
    }
    _vec.push_back(_v);
    return _v.second;
  }

  std::pair<iterator, bool>
  insert(value_type const & _v)
  {
    iterator _i = find(_v.first);
    if (_i != end())
    {
      return std::make_pair(_vec.end(), false);
    }
    if (size() > 0 && _val_cmp(_v, *(begin() + size() - 1)))
    {
      _damaged = true;
    }
    _vec.push_back(_v);
    return std::make_pair(begin() + (size() - 1), true);
  }

  iterator
  insert(iterator _pos, value_type const & _v)
  {
    iterator _i = find(_v.first);
    if (_i != end())
    {
      return _i;
    }
    if (size() > 0 && _val_cmp(_v, *(begin() + size() - 1)))
    {
      _damaged = true;
    }
    _vec.push_back(_v);
    return begin() + (size() - 1);
  }

  template <typename _InputIterator>
  void
  insert(_InputIterator _first, _InputIterator _last)
  {
    iterator i = begin();
    while (_first != _last)
    {
      i = insert(i, *_first++);
    }
  }
  
  void 
  erase(iterator _i)
  {
    _vec.erase(_i);
  }
  
  size_type 
  erase(key_type const & _k)
  {
    iterator _i = find(_k);
    size_type _c = 0;
    while (_i != end() && _i->first == _k) 
    {
      erase(_i);
      ++_c;
      ++_i;
    }
    return _c;
  }
  
  void
  erase(iterator _first, iterator _last)
  {
    while (_first != _last)
    {
      erase(_first++);
    }
  }

  void
  swap(smap & _x)
  {
    _ensure_sort();
    _x._ensure_sort();
    _vec.swap(_x._vec);
  }

  void
  clear()
  {
    _vec.clear();
    _damaged = false;
  }

  key_compare
  key_comp() const
  {
    return _val_cmp.comp;
  }

  value_compare
  value_comp() const
  {
    return _val_cmp;
  }

  iterator
  find(key_type const & _k)
  {
    if (size() < 256) 
    {
      // linear search
      iterator e = end();
      for (iterator i = begin(); i != e; ++i)
      {
        if (i->first == _k)
          {
            return i;
          }
      }
      return e;
    }
    else
    {
      // maybe-sort + binary search
      iterator i = lower_bound(_k);
      if (i != end() && i->first == _k)
      {
        return i;
      }
      return end();
    }
  }

  const_iterator
  find(key_type const & _k) const
  {
    if (size() < 256) 
    {
      // linear search
      const_iterator e = end();
      for (const_iterator i = begin(); i != e; ++i)
      {
        if (i->first == _k)
          {
            return i;
          }
      }
      return e;
    }
    else
    {
      // maybe-sort + binary search
      const_iterator i = lower_bound(_k);
      if (i != end() && i->first == _k)
      {
        return i;
      }
      return end();
    }
  }

  size_type
  count(key_type const & _k) const
  {
    return (find(_k) == end() ? 0 : 1);
  }

  iterator
  lower_bound(key_type const & _k)
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::lower_bound(begin(), end(), _v, _val_cmp);
  }

  const_iterator
  lower_bound(key_type const & _k) const
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::lower_bound(begin(), end(), _v, _val_cmp);
  }

  iterator
  upper_bound(key_type const & _k)
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::upper_bound(begin(), end(), _v, _val_cmp);
  }

  const_iterator
  upper_bound(key_type const & _k) const
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::upper_bound(begin(), end(), _v, _val_cmp);
  }

  std::pair<iterator, iterator>
  equal_range(key_type const & _k)
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::equal_range(begin(), end(), _v, _val_cmp);
  }

  std::pair<const_iterator, const_iterator>
  equal_range(key_type const & _k) const
  {
    _ensure_sort();
    value_type _v(_k, mapped_type());
    return std::equal_range(begin(), end(), _v, _val_cmp);
  }


  template <typename _K1, typename _T1, typename _C1, typename _A1>
  friend bool
  operator== (const smap<_K1,_T1,_C1,_A1>&,
              const smap<_K1,_T1,_C1,_A1>&);
  
  template <typename _K1, typename _T1, typename _C1, typename _A1>
  friend bool
  operator< (const smap<_K1,_T1,_C1,_A1>&,
             const smap<_K1,_T1,_C1,_A1>&);

};

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator==(smap<_K, _D, _compare, _alloc> const & _a,
           smap<_K, _D, _compare, _alloc> const & _b)
{ 
  _a._ensure_sort();
  _b._ensure_sort();
  return _a._vec == _b._vec;
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator<(smap<_K, _D, _compare, _alloc> const & _a,
          smap<_K, _D, _compare, _alloc> const & _b)
{ 
  _a._ensure_sort();
  _b._ensure_sort();
  return _a._vec < _b._vec;
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator!=(smap<_K, _D, _compare, _alloc> const & _a,
           smap<_K, _D, _compare, _alloc> const & _b)
{ 
  return !(_a == _b);
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator>(smap<_K, _D, _compare, _alloc> const & _a,
          smap<_K, _D, _compare, _alloc> const & _b)
{ 
  return _b < _a;
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator<=(smap<_K, _D, _compare, _alloc> const & _a,
           smap<_K, _D, _compare, _alloc> const & _b)
{ 
  return !(_b < _a);
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline bool
operator>=(smap<_K, _D, _compare, _alloc> const & _a,
           smap<_K, _D, _compare, _alloc> const & _b)
{ 
  return !(_a < _b);
}

template <typename _K, typename _D, 
          typename _compare, 
          typename _alloc>
inline void
swap(smap<_K, _D, _compare, _alloc> & _a, 
     smap<_K, _D, _compare, _alloc> & _b)
{ 
  _a.swap(_b); 
}


#endif // __SMAP_HH__
