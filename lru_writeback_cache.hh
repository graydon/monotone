/***************************************************************************
 *   Originally copyright (C) 2004 by Patrick Audley                       *
 *   paudley@blackcat.ca                                                   *
 *                                                                         *
 *   Revised and copyright (C) 2006 by Nathaniel Smith <njs@pobox.com>     *
 *   for the monotone project.                                             *
 *                                                                         *
 ***************************************************************************/

#include <map>
#include <list>

#include "sanity.hh"
#include "safe_map.hh"

template <typename T> struct WritebackCountfn
{
  unsigned long operator () (T const & x)
  {
    return 1;
  }
};

// for use in caches where objects never become dirty
template <typename Key, typename Data> struct NullManager
{
  inline void writeout(Key const &, Data const &)
  {
    I(false);
  }
};

/**
 * @brief Template cache with an LRU removal policy.
 * @class LRUWritebackCache
 *
 * @par
 * This template creats a simple collection of key-value pairs that grows
 * until the size specified at construction is reached and then begins
 * discard the Least Recently Used element on each insertion.
 *
 * It also tracks a 'dirty set'.  Any given item can be marked clean or dirty.
 * Importantly, when a dirty item is discarded, a Manager object is first
 * given the chance to write it out to disk.  All managing of the dirty bit is
 * done manually by calling code.
 *
 */
// Manager is a concept with a writeout(Key, Data) method
template <typename Key, typename Data,
          typename Sizefn = WritebackCountfn<Data>,
          typename Manager = NullManager<Key, Data> >
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
  Manager _manager;
    
  /// Maximum abstract size of the cache
  unsigned long _max_size;
    
  /// Current abstract size of the cache
  unsigned long _curr_size;
    
public:
  /** @brief Creates a cache that holds at most Size worth of elements.
   *  @param Size maximum size of cache
   */
  LRUWritebackCache(const unsigned long Size, Manager manager)
      : _manager(manager), _max_size(Size), _curr_size(0)
  {
  }

  // Also allow a default-instantiated manager, for using this as a pure LRU
  // cache with no writeback.
  LRUWritebackCache(const unsigned long Size)
      : _max_size(Size), _curr_size(0)
  {
  }

  /// Destructor - cleans up both index and storage
  ~LRUWritebackCache()
  {
    I(_dirty.empty());
  }

  /** @brief Gets the current abstract size of the cache.
   *  @return current size
   */
  inline unsigned long size(void) const
  {
    return _curr_size;
  }

  /** @brief Gets the maximum abstract size of the cache.
   *  @return maximum size
   */
  inline unsigned long max_size(void) const
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
    for (typename std::set<Key>::const_iterator i = _dirty.begin(); i != _dirty.end(); ++i)
      this->_writeout(*i);
    _dirty.clear();
  }

  /// Clears all storage and indices (do this at SQL ROLLBACK)
  void clear_and_drop_writes()
  {
    _list.clear();
    _index.clear();
    _dirty.clear();
    _curr_size = 0;
  };

  /// Mark an item as not needing to be written back (do this when writing an
  /// alternative form of it to the db, e.g. a delta).  No-op if the item was
  /// already clean.
  void mark_clean(Key const & key)
  {
    _dirty.erase(key);
  }

  /// Say if we're planning to write back an item (do this to figure out
  /// whether you should be writing an alternative form of it to the db,
  /// e.g. a delta).
  bool is_dirty(Key const & key)
  {
    return (_dirty.find(key) != _dirty.end());
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
    // A little sanity check -- if we were empty, then we should have
    // been zero-size:
    if (_list.empty())
      I(_curr_size == 0);
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
        I(liter != _list.begin());
        --liter;
        // liter now points to the last element.  If the last element is also
        // the first element -- i.e., the list has only one element, and we
        // know that it's the one we just inserted -- then never mind, we
        // never want to empty ourselves out completely.
        if (liter == _list.begin())
          break;
        this->_remove(liter->first);
      }
    I(exists(key));
  }

  inline void insert_dirty(Key const & key, const Data & data)
  {
    insert_clean(key, data);
    safe_insert(_dirty, key);
    I(is_dirty(key));
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
   */
  inline void _remove(const Key & key)
  {
    if (_dirty.find(key) != _dirty.end())
      {
        this->_writeout(key);
        safe_erase(_dirty, key);
      }
    Map_Iter miter = _index.find(key);
    I(miter != _index.end());
    _curr_size -= Sizefn()(miter->second->second);
    _list.erase(miter->second);
    _index.erase(miter);
  }

  // NB: does _not_ remove 'key' from the dirty set
  inline void _writeout(Key const & key)
  {
    List_Iter const & i = safe_get(_index, key);
    _manager.writeout(i->first, i->second);
  }
};


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

