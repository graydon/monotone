/**************************************************************************
 *   Originally copyright (C) 2004 by Patrick Audley                       *
 *   paudley@blackcat.ca                                                   *
 *                                                                         *
 ***************************************************************************/

/**
 * @file lru_cache.cpp Template cache with an LRU removal policy
 * @author Patrick Audley
 * @version 1.0
 * @date December 2004
 */
#include <map>
#include <list>

#include "sanity.hh"
#include "safe_map.hh"

template <typename T> struct Countfn
{
  unsigned long operator () (T const & x)
  {
    return 1;
  }
};

/**
 * @brief Template cache with an LRU removal policy.
 * @class LRUCache
 *
 * @par
 * This template creats a simple collection of key-value pairs that grows
 * until the size specified at construction is reached and then begins
 * discard the Least Recently Used element on each insertion.
 *
 */
// Manager is a concept with a writeout(Key, Data) method
template <typename Key, typename Data, typename Manager,
          typename Sizefn = Countfn<Data> >
class LRUWritebackCache
{
public:
  /// Main cache storage typedef
  typedef std::list< std::pair<Key, Data> > List;
  /// Main cache iterator
  typedef typename List::iterator List_Iter;
  /// Index typedef
  typedef std::map<Key, List_Iter> Map;
  /// Index iterator
  typedef typename Map::iterator Map_Iter;

private:
  /// Main cache storage
  List _list;
  /// Cache storage index
  Map _index;
  /// Dirty list
  std::set<Key> _dirty;
  /// Manager
  Manager & _manager;
    
  /// Maximum abstract size of the cache
  unsigned long _max_size;
    
  /// Current abstract size of the cache
  unsigned long _curr_size;
    
public:
  /** @brief Creates a cache that holds at most Size worth of elements.
   *  @param Size maximum size of cache
   */
  LRUCache(const unsigned long Size, Manager & manager)
    : _manager(manager), _max_size(Size)
  {
  }

  /// Destructor - cleans up both index and storage
  ~LRUCache()
  {
    I(_dirty.empty());
  }

  /** @brief Gets the current abstract size of the cache.
   *  @return current size
   */
  inline const unsigned long size(void) const
  {
    return _curr_size;
  }

  /** @brief Gets the maximum sbstract size of the cache.
   *  @return maximum size
   */
  inline const unsigned long max_size(void) const
  {
    return _max_size;
  }

  /// Checks if all items are clean (this should be true before a SQL BEGIN)
  bool all_clean()
  {
    return _dirty.empty();
  }

  /// Cleans all dirty items (do this before a SQL COMMIT)
  void clean_all()
  {
    for (std::set<Key>::const_iterator i = _dirty.begin(); i != _dirty.end(); ++i)
      this->_writeout(*i);
    _dirty.clear();
  }

  /// Clears all storage and indices (do this at SQL ROLLBACK)
  void clear_and_drop_writes()
  {
    _list.clear();
    _index.clear();
    _dirty.clear();
  };

  /// Mark an item as not needing to be written back (do this when writing an
  /// alternative form of it to the db, e.g. a delta)
  void mark_clean(Key const & key)
  {
    safe_erase(_dirty, key);
  }

  /** @brief Checks for the existance of a key in the cache.
   *  @param key to check for
   *  @return bool indicating whether or not the key was found.
   */
  inline bool exists(Key const & key) const
  {
    return _index.find(key) != _index.end();
  }

  /** @brief Touches a key in the Cache and makes it the most recently used.
   *  @param key to be touched
   */
  inline void touch(Key const & key)
  {
    // Find the key in the index.
    Map_Iter miter = this->_touch(key);
  }

  /** @brief Fetches a copy of cache data.
   *  @param key to fetch data for
   *  @param data to fetch data into
   *  @param touch whether or not to touch the data
   *  @return whether or not data was filled in
   */
  inline bool fetch(Key const & key, Data & data, bool touch = true)
  {
    Map_Iter miter = _index.find(key);
    if (miter == _index.end())
      return false;
    if (touch)
      this->touch(key);
    data = miter->second->second;
    return true;
  }


  /** @brief Inserts a key-data pair into the cache and removes entries if neccessary.
   *  @param key object key for insertion
   *  @param data object data for insertion
   *  @note This function checks key existance and touches the key if it already exists.
   */
  inline void insert_clean(Key const & key, const Data & data)
  {
    // Ok, do the actual insert at the head of the list
    _list.push_front(std::make_pair(key, data));
    List_Iter liter = _list.begin();
    // Store the index
    safe_insert(_index, std::make_pair(key, liter));
    _curr_size += Sizefn()(data);
    // Check to see if we need to remove an element due to exceeding max_size
    while (_curr_size > _max_size)
      {
        // Remove the last element.
        liter = _list.end();
        --liter;
        this->_remove(liter->first);
      }
  }

  inline void insert_dirty(Key const & key, const Data & data)
  {
    insert_clean(key, data);
    safe_insert(_dirty, key);
  }

private:
  /** @brief Internal touch function.
   *  @param key to be touched
   *  @return a Map_Iter pointing to the key that was touched.
   */
  inline Map_Iter _touch(const Key & key)
  {
    Map_Iter miter = _index.find(key);
    if (miter == _index.end())
      return miter;
    // Move the found node to the head of the list.
    _list.splice(_list.begin(), _list, miter->second);
    return miter;
  }

  /** @brief Interal remove function
   *  @param miter Map_Iter that points to the key to remove
   *  @warning miter is now longer usable after being passed to this function.
   */
  inline void _remove(const Key & key)
  {
    if (_dirty.find(key) != _dirty.end())
      {
        this->_writeout(key);
        safe_erase(_dirty, key);
      }
    Map_Iter miter = _index.find(key);
    _curr_size -= Sizefn()(miter->second->second);
    _list.erase(miter->second);
    _index.erase(miter);
  }

  // NB: does _not_ remove 'key' from the dirty set
  inline void _writeout(Key const & key) const
  {
    List_Iter const & i = safe_get(_index, key);
    _manager.writeout(i->first, i->second);
  }
};
