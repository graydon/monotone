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
 * @par
 * This cache is thread safe if compiled with the _REENTRANT defined.  It
 * uses the BOOST scientific computing library to provide the thread safety
 * mutexes.
 */
#include <map>
#include <list>

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
template <typename Key, typename Data,
          typename Sizefn = Countfn<Data> >
class LRUCache
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
    
  /// Maximum abstract size of the cache
  unsigned long _max_size;
    
  /// Current abstract size of the cache
  unsigned long _curr_size;
    
public:
  /** @brief Creates a cache that holds at most Size worth of elements.
   *  @param Size maximum size of cache
   */
  LRUCache(const unsigned long Size)
    : _max_size(Size)
  {
  }

  /// Destructor - cleans up both index and storage
  ~LRUCache()
  {
    this->clear();
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

  /// Clears all storage and indices.
  void clear(void)
  {
    _list.clear();
    _index.clear();
  };

  /** @brief Checks for the existance of a key in the cache.
   *  @param key to check for
   *  @return bool indicating whether or not the key was found.
   */
  inline bool exists(const Key & key) const
  {
    return _index.find(key) != _index.end();
  }

  /** @brief Removes a key-data pair from the cache.
   *  @param key to be removed
   */
  inline void remove(const Key & key)
  {
    Map_Iter miter = _index.find(key);
    if (miter == _index.end())
      return;
    this->_remove(miter);
  }

  /** @brief Touches a key in the Cache and makes it the most recently used.
   *  @param key to be touched
   */
  inline void touch(const Key & key)
  {
    // Find the key in the index.
    Map_Iter miter = this->_touch(key);
  }

  /** @brief Fetches a pointer to cache data.
   *  @param key to fetch data for
   *  @param touch whether or not to touch the data
   *  @return pointer to data or NULL on error
   */
  inline Data *fetch(const Key & key, bool touch = true)
  {
    Map_Iter miter = _index.find(key);
    if (miter == _index.end())
      return NULL;
    if (touch)
      this->touch(key);
    return &(miter->second->second);
  }

  /** @brief Fetches a pointer to cache data.
   *  @param key to fetch data for
   *  @param data to fetch data into
   *  @param touch whether or not to touch the data
   *  @return whether or not data was filled in
   */
  inline bool fetch(const Key & key, Data & data, bool touch = true)
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
  inline void insert(const Key & key, const Data & data)
  {
    // Touch the key, if it exists, then replace the content.
    Map_Iter miter = this->_touch(key);
    if (miter != _index.end())
      {
        this->_remove(miter);
      }
    // Ok, do the actual insert at the head of the list
    _list.push_front(std::make_pair(key, data));
    List_Iter liter = _list.begin();
    // Store the index
    _index[key] = liter;
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
  inline void _remove(const Map_Iter & miter)
  {
    _curr_size -= Sizefn()(miter->second->second);
    _list.erase(miter->second);
    _index.erase(miter);
  }

  /** @brief Interal remove function
   *  @param key to remove
   */
  inline void _remove(const Key & key)
  {
    Map_Iter miter = _index.find(key);
    _remove(miter);
  }
};
