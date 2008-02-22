// Implementation of the base circular buffer.

// Copyright (c) 2003
// Jan Gaspar, Whitestein Technologies

// Permission to use or copy this software for any purpose is hereby granted 
// without fee, provided the above notices are retained on all copies.
// Permission to modify the code and to distribute modified code is granted,
// provided the above notices are retained, and a notice that the code was
// modified is included with the above copyright notice.

// This material is provided "as is", with absolutely no warranty expressed
// or implied. Any use is at your own risk.

#if !defined(BOOST_CIRCULAR_BUFFER_BASE_HPP)
#define BOOST_CIRCULAR_BUFFER_BASE_HPP

#include <boost/concept_check.hpp>
#include <boost/iterator.hpp>
#include <boost/iterator_adaptors.hpp>
#include <boost/call_traits.hpp>
#include <boost/type_traits.hpp>
#include <boost/throw_exception.hpp>
#include <boost/assert.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION >= 103100
#include <boost/iterator/reverse_iterator.hpp>
#endif

#include <memory>
#include <algorithm>
#if !defined(BOOST_NO_EXCEPTIONS)
    #include <stdexcept>
#endif

namespace boost {

// Exception handling macros.
#if !defined(BOOST_NO_EXCEPTIONS)
    #define BOOST_CB_TRY try {
    #define BOOST_CB_UNWIND(action) } catch(...) { action; throw; }
#else
    #define BOOST_CB_TRY
    #define BOOST_CB_UNWIND(action)
#endif

namespace cb_details {

/*
    \struct cb_int_iterator_tag
    \brief Identifying tag for integer types (not for iterators).
*/
struct cb_int_iterator_tag {};

/*
    \struct cb_iterator_category
    \brief Defines iterator category.
*/
template <bool>
struct cb_iterator_category {
    //! Represents iterators.
    typedef std::input_iterator_tag iterator_category;
};

template <>
struct cb_iterator_category<true> {
    //! Represents integral types (not iterators).
    typedef cb_int_iterator_tag iterator_category;
};

/*
    \struct cb_iterator_category_traits
    \brief Defines the iterator category tag for the given iterator.
*/
template <class Iterator>
struct cb_iterator_category_traits {
    //! Iterator category tag type.
    /*!
        Depending on the template parameter the <tt>tag</tt> distinguishes
        between iterators and non-iterators. If the template parameter
        is an iterator the <tt>tag</tt> is typedef for <tt>std::input_iterator_tag</tt>.
        If the parameter is not an iterator the <tt>tag</tt> is typedef for
        <tt>cb_int_iterator_tag</tt>.
    */
    typedef typename cb_details::cb_iterator_category<
        is_integral<Iterator>::value>::iterator_category tag;
};

template <class Traits> struct cb_nonconst_traits;

/*
    \struct cb_const_traits
    \brief Defines the data types for a const iterator.
    \param Traits Defines the basic types.
*/
template <class Traits>
struct cb_const_traits {
// Basic types
    typedef typename Traits::value_type value_type;
    typedef typename Traits::const_pointer pointer;
    typedef typename Traits::const_reference reference;
    typedef typename Traits::size_type size_type;
    typedef typename Traits::difference_type difference_type;

// Non-const traits
    typedef cb_nonconst_traits<Traits> nonconst_traits;
};

/*
    \struct cb_nonconst_traits
    \brief Defines the data types for a non-const iterator.
    \param Traits Defines the basic types.
*/
template <class Traits>
struct cb_nonconst_traits {
// Basic types
    typedef typename Traits::value_type value_type;
    typedef typename Traits::pointer pointer;
    typedef typename Traits::reference reference;
    typedef typename Traits::size_type size_type;
    typedef typename Traits::difference_type difference_type;

// Non-const traits
    typedef cb_nonconst_traits<Traits> nonconst_traits;
};

/*
    \struct cb_internal_pointer
    \brief Helper pointer used in the cb_iterator.
*/
template <class Traits0>
struct cb_helper_pointer {
    bool m_end;
    typename Traits0::pointer m_it;
};

/*
    \class cb_iterator
    \brief Random access iterator for the circular buffer.
    \param Buff The type of the underlying circular buffer.
    \param Traits Defines basic iterator types.
    \note This iterator is not circular. It was designed
          for iterating from begin() to end() of the circular buffer.
*/
template <class Buff, class Traits> 
class cb_iterator : 
    public boost::iterator< 
        std::random_access_iterator_tag,
        typename Traits::value_type,
        typename Traits::difference_type,
        typename Traits::pointer,
        typename Traits::reference>
{
private:
// Helper types

    //! Base iterator.
    typedef boost::iterator<
        std::random_access_iterator_tag,
        typename Traits::value_type,
        typename Traits::difference_type,
        typename Traits::pointer,
        typename Traits::reference> base_type;

    //! Non-const iterator.
    typedef cb_iterator<Buff, typename Traits::nonconst_traits> nonconst_self;

public:
// Basic types

    //! The type of the elements stored in the circular buffer.
    typedef typename base_type::value_type value_type;

    //! Pointer to the element.
    typedef typename base_type::pointer pointer;

    //! Reference to the element.
    typedef typename base_type::reference reference;

    //! Size type.
    typedef typename Traits::size_type size_type;

    //! Difference type.
    typedef typename base_type::difference_type difference_type;

public:
// Member variables

    //! The circular buffer where the iterator points to.
    const Buff* m_buff;

    //! An internal iterator.
    pointer m_it;

public:
// Construction & assignment

    // Default copy constructor.

    //! Default constructor.
    cb_iterator() : m_buff(0), m_it(0) {}

    //! Copy constructor (used for converting from a non-const to a const iterator).
    cb_iterator(const nonconst_self& it)
        : m_buff(it.m_buff), m_it(it.m_it) {}

    //! Internal constructor.
    /*!
        \note This constructor is not intended to be used directly by the user.
    */
    cb_iterator(const Buff* cb, const pointer it)
        : m_buff(cb), m_it(it) {}

    // Default assign operator.

public:
// Random access iterator methods

    //! Dereferencing operator.
    reference operator * () const {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_it != 0); // iterator pointing to the end
        return *m_it;
    }

    //! Dereferencing operator.
    pointer operator -> () const { return &(operator*()); }

    //! Difference operator.
    difference_type operator - (const cb_iterator& it) const {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(it.m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_buff == it.m_buff); // iterators of different containers or invalidated iterator
        cb_helper_pointer<Traits> lhs = create_helper_pointer(*this);
        cb_helper_pointer<Traits> rhs = create_helper_pointer(it);
        if (less(rhs, lhs) && lhs.m_it <= rhs.m_it)
            return lhs.m_it + m_buff->capacity() - rhs.m_it;
        if (less(lhs, rhs) && lhs.m_it >= rhs.m_it)
            return lhs.m_it - m_buff->capacity() - rhs.m_it;
        return lhs.m_it - rhs.m_it;
    }

    //! Increment operator (prefix).
    cb_iterator& operator ++ () {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_it != 0); // iterator pointing to the end
        m_buff->increment(m_it);
        if (m_it == m_buff->m_last)
            m_it = 0;
        return *this;
    }

    //! Increment operator (postfix).
    cb_iterator operator ++ (int) {
        cb_iterator tmp = *this;
        ++*this;
        return tmp;
    }

    //! Decrement operator (prefix).
    cb_iterator& operator -- () {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        if (m_it == 0)
            m_it = m_buff->m_last;
        m_buff->decrement(m_it);
        return *this;
    }

    //! Decrement operator (postfix).
    cb_iterator operator -- (int) {
        cb_iterator tmp = *this;
        --*this;
        return tmp;
    }

    //! Iterator addition.
    cb_iterator& operator += (difference_type n) {
        if (n > 0) {
            BOOST_ASSERT(m_buff != 0); // uninitialized iterator
            BOOST_ASSERT(m_it != 0); // iterator pointing to the end
            m_it = m_buff->add(m_it, n);
            if (m_it == m_buff->m_last)
                m_it = 0;
        } else if (n < 0) {
            *this -= -n;
        }
        return *this;
    }

    //! Iterator addition.
    cb_iterator operator + (difference_type n) const { return cb_iterator(*this) += n; }

    //! Iterator subtraction.
    cb_iterator& operator -= (difference_type n) {
        if (n > 0) {
            BOOST_ASSERT(m_buff != 0);
            m_it = m_buff->sub(m_it == 0 ? m_buff->m_last : m_it, n);
        } else if (n < 0) {
            *this += -n;
        }
        return *this;
    }

    //! Iterator subtraction.
    cb_iterator operator - (difference_type n) const { return cb_iterator(*this) -= n; }

    //! Element access operator.
    reference operator [] (difference_type n) const { return *(*this + n); }

public:
// Equality & comparison

    //! Equality.
    template <class Traits0>
    bool operator == (const cb_iterator<Buff, Traits0>& it) const {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(it.m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_buff == it.m_buff); // iterators of different containers or invalidated iterator
        return m_it == it.m_it;
    }

    //! Inequality.
    template <class Traits0>
    bool operator != (const cb_iterator<Buff, Traits0>& it) const {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(it.m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_buff == it.m_buff); // iterators of different containers or invalidated iterator
        return m_it != it.m_it;
    }

    //! Less.
    template <class Traits0>
    bool operator < (const cb_iterator<Buff, Traits0>& it) const {
        BOOST_ASSERT(m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(it.m_buff != 0); // uninitialized iterator
        BOOST_ASSERT(m_buff == it.m_buff); // iterators of different containers or invalidated iterator
        return less(create_helper_pointer(*this), create_helper_pointer(it));
    }

    //! Greater.
    template <class Traits0>
    bool operator > (const cb_iterator<Buff, Traits0>& it) const  { return it < *this; }

    //! Less or equal.
    template <class Traits0>
    bool operator <= (const cb_iterator<Buff, Traits0>& it) const { return !(it < *this); }

    //! Greater or equal.
    template <class Traits0>
    bool operator >= (const cb_iterator<Buff, Traits0>& it) const { return !(*this < it); }

private:
// Helpers

    //! Create helper pointer.
    template <class Traits0>
    cb_helper_pointer<Traits0> create_helper_pointer(const cb_iterator<Buff, Traits0>& it) const {
        cb_helper_pointer<Traits0> helper;
        helper.m_end = (it.m_it == 0);
        helper.m_it = helper.m_end ? m_buff->m_last : it.m_it;
        return helper;
    }

    //! Compare two pointers.
    /*!
        \return 1 if p1 is greater than p2.
        \return 0 if p1 is equal to p2.
        \return -1 if p1 is lower than p2.
    */
    template <class Pointer0, class Pointer1>
    static difference_type compare(Pointer0 p1, Pointer1 p2) {
        return p1 < p2 ? -1 : (p1 > p2 ? 1 : 0);
    }

    //! Less.
    template <class InternalIterator0, class InternalIterator1>
    bool less(const InternalIterator0& lhs, const InternalIterator1& rhs) const {
        switch (compare(lhs.m_it, m_buff->m_first)) {
        case -1:
            switch (compare(rhs.m_it, m_buff->m_first)) {
            case -1: return lhs.m_it < rhs.m_it;
            case 0: return rhs.m_end;
            case 1: return false;
            }
        case 0:
            switch (compare(rhs.m_it, m_buff->m_first)) {
            case -1: return !lhs.m_end;
            case 0: return !lhs.m_end && rhs.m_end;
            case 1: return !lhs.m_end;
            }
        case 1:
            switch (compare(rhs.m_it, m_buff->m_first)) {
            case -1: return true;
            case 0: return rhs.m_end;
            case 1: return lhs.m_it < rhs.m_it;
            }
        }
        return false;
    }
};

//! Iterator addition.
template <class Buff, class Traits>
inline cb_iterator<Buff, Traits>
operator + (typename Traits::difference_type n, const cb_iterator<Buff, Traits>& it) {
    return it + n;
}

#if defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION) && !defined(BOOST_MSVC_STD_ITERATOR)

//! Iterator category.
template <class Buff, class Traits>
inline std::random_access_iterator_tag
iterator_category(const cb_iterator<Buff, Traits>&) {
    return std::random_access_iterator_tag();
}

//! The type of the elements stored in the circular buffer.
template <class Buff, class Traits>
inline typename Traits::value_type*
value_type(const cb_iterator<Buff, Traits>&) { return 0; }

//! Distance type.
template <class Buff, class Traits>
inline typename Traits::difference_type*
distance_type(const cb_iterator<Buff, Traits>&) { return 0; }

#endif // #if defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION) && !defined(BOOST_MSVC_STD_ITERATOR)

}; // namespace cb_details

/*!
    \class circular_buffer
    \brief Circular buffer - a STL compliant container.
    \param T The type of the elements stored in the circular buffer.
    \param Alloc The allocator type used for all internal memory management.
    \author <a href="mailto:jano_gaspar@yahoo.com">Jan Gaspar</a>
    \version 3.3
    \date 2003

    For more information how to use the circular buffer see the
    <a href="../circular_buffer.html">documentation</a>.
*/
template <class T, class Alloc>
class circular_buffer {

// Requirements
    BOOST_CLASS_REQUIRE(T, boost, AssignableConcept);

public:
// Basic types

    //! The type of the elements stored in the circular buffer.
    typedef typename Alloc::value_type value_type;

    //! Pointer to the element.
    typedef typename Alloc::pointer pointer;

    //! Const pointer to the element.
    typedef typename Alloc::const_pointer const_pointer;

    //! Reference to the element.
    typedef typename Alloc::reference reference;

    //! Const reference to the element.
    typedef typename Alloc::const_reference const_reference;

    //! Size type.
    typedef typename Alloc::size_type size_type;

    //! Difference type.
    typedef typename Alloc::difference_type difference_type;

    //! The type of the allocator used in the circular buffer.
    typedef Alloc allocator_type;

    //! Return the allocator.
    /*!
        \return Allocator
    */
    allocator_type get_allocator() const { return m_alloc; }

// Helper types

    // Define a type that represents the "best" way to pass the value_type to a method.
    typedef typename call_traits<value_type>::param_type param_value_type;

// Iterators

    //! Const (random access) iterator used to iterate through a circular buffer.
    typedef cb_details::cb_iterator< circular_buffer<T, Alloc>, cb_details::cb_const_traits<Alloc> > const_iterator;

    //! Iterator (random access) used to iterate through a circular buffer.
    typedef cb_details::cb_iterator< circular_buffer<T, Alloc>, cb_details::cb_nonconst_traits<Alloc> > iterator;

#if BOOST_VERSION >= 103100
    //! Const iterator used to iterate backwards through a circular buffer.
    typedef boost::reverse_iterator<const_iterator> const_reverse_iterator;

    //! Iterator used to iterate backwards through a circular buffer.
    typedef boost::reverse_iterator<iterator> reverse_iterator;
#else 
    //! Const iterator used to iterate backwards through a circular buffer.
    typedef typename reverse_iterator_generator<const_iterator>::type const_reverse_iterator;

    //! Iterator used to iterate backwards through a circular buffer.
    typedef typename reverse_iterator_generator<iterator>::type reverse_iterator;
#endif

private:
// Member variables

    //! The internal buffer used for storing elements in the circular buffer.
    pointer m_buff;

    //! The internal buffer's end (end of the storage space).
    pointer m_end;

    //! The virtual beginning of the circular buffer (the leftmost element).
    pointer m_first;

    //! The virtual end of the circular buffer (the rightmost element).
    pointer m_last;

    //! The number of items currently stored in the circular buffer.
    size_type m_size;

    //! The allocator.
    allocator_type m_alloc;

// Friends
#if defined(BOOST_NO_MEMBER_TEMPLATE_FRIENDS)
    friend iterator;
    friend const_iterator;
#else
    friend struct cb_details::cb_iterator< circular_buffer<T, Alloc>, cb_details::cb_const_traits<Alloc> >;
    friend struct cb_details::cb_iterator< circular_buffer<T, Alloc>, cb_details::cb_nonconst_traits<Alloc> >;
#endif

public:
// Element access

    //! Return an iterator pointing to the beginning of the circular buffer.
    iterator begin() { return iterator(this, empty() ? 0 : m_first); }

    //! Return an iterator pointing to the end of the circular buffer.
    iterator end() { return iterator(this, 0); }

    //! Return a const iterator pointing to the beginning of the circular buffer.
    const_iterator begin() const { return const_iterator(this, empty() ? 0 : m_first); }

    //! Return a const iterator pointing to the end of the circular buffer.
    const_iterator end() const { return const_iterator(this, 0); }

    //! Return a reverse iterator pointing to the beginning of the reversed circular buffer.
    reverse_iterator rbegin() { return reverse_iterator(end()); }

    //! Return a reverse iterator pointing to the end of the reversed circular buffer.
    reverse_iterator rend() { return reverse_iterator(begin()); }
    
    //! Return a const reverse iterator pointing to the beginning of the reversed circular buffer.
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }

    //! Return a const reverse iterator pointing to the end of the reversed circular buffer.
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

    //! Return the element at the <tt>index</tt> position.
    reference operator [] (size_type index) { return *add(m_first, index); }

    //! Return the element at the <tt>index</tt> position.
    const_reference operator [] (size_type index) const { return *add(m_first, index); }

    //! Return the element at the <tt>index</tt> position.
    /*!
        \throws std::out_of_range thrown when the <tt>index</tt> is invalid.
    */
    reference at(size_type index) {
        check_position(index);
        return (*this)[index];
    }

    //! Return the element at the <tt>index</tt> position.
    /*!
        \throws std::out_of_range thrown when the <tt>index</tt> is invalid.
    */
    const_reference at(size_type index) const {
        check_position(index);
        return (*this)[index];
    }

    //! Return the first (leftmost) element.
    reference front() { return *m_first; }

    //! Return the last (rightmost) element.
    reference back() { return *((m_last == m_buff ? m_end : m_last) - 1); }

    //! Return the first (leftmost) element.
    const_reference front() const { return *m_first; }

    //! Return the last (rightmost) element.
    const_reference back() const { return *((m_last == m_buff ? m_end : m_last) - 1); }

    //! Return pointer to data stored in the circular buffer as a continuous array of values.
    /*!
        This method can be usefull e.g. when passing the stored data into the legacy C API.
        \post <tt>\&(*this)[0] \< \&(*this)[1] \< ... \< \&(*this).back()</tt>
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    pointer data() {
        if (m_first < m_last || m_first == m_buff)
            return m_first;
        size_type constructed = 0;
        pointer src = m_first;
        pointer dest = m_buff;
        BOOST_CB_TRY
        for (pointer first = m_first; dest < src; src = first) {
            for (size_type ii = 0; src < m_end; ++src, ++dest, ++ii) {
                if (dest == first) {
                    first += ii;
                    break;
                }
                if (is_uninitialized(dest)) {
                    m_alloc.construct(dest, *src);
                    ++constructed;
                } else {
                    std::swap(*dest, *src);
                }
            }
        }
        BOOST_CB_UNWIND(
            for (dest = m_last; constructed > 0; ++dest, --constructed)
                    m_alloc.destroy(dest);
        )
        for (dest = m_buff + size(); dest < m_end; ++dest)
            m_alloc.destroy(dest);
        m_first = m_buff;
        m_last = add(m_buff, size());
        return m_buff;
    }

// Size and capacity

    //! Return the number of elements currently stored in the circular buffer.
    size_type size() const { return m_size; }

    //! Return the largest possible size (or capacity) of the circular buffer.
    size_type max_size() const { return m_alloc.max_size(); }
    
    //! Is the circular buffer empty?
    /*!
        \return true if there are no elements stored in the circular buffer.
        \return false otherwise.
    */
    bool empty() const { return size() == 0; }

    //! Is the circular buffer full?
    /*!
        \return true if the number of elements stored in the circular buffer
                equals the capacity of the circular buffer.
        \return false otherwise.
    */
    bool full() const { return size() == capacity(); }

    //! Return the capacity of the circular buffer.
    size_type capacity() const { return m_end - m_buff; }

    //! Change the capacity of the circular buffer.
    /*!
        \post <tt>(*this).capacity() == new_capacity</tt><br>
              If the current number of elements stored in the circular
              buffer is greater than the desired new capacity then the
              first (leftmost) <tt>((*this).size() - new_capacity)</tt> elements
              will be removed.
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void set_capacity(size_type new_capacity) {
        if (new_capacity == capacity())
            return;
        pointer buff = allocate(new_capacity);
        size_type new_size = new_capacity < size() ? new_capacity : size();
        BOOST_CB_TRY
        std::uninitialized_copy(end() - new_size, end(), buff);
        BOOST_CB_UNWIND(deallocate(buff, new_capacity))
        destroy();
        m_size = new_size;
        m_buff = m_first = buff;
        m_end = m_buff + new_capacity;
        m_last = add(m_buff, size());
    }

    //! Change the size of the circular buffer.
    /*!
        \post <tt>(*this).size() == new_size</tt><br>
              If the new size is greater than the current size, the rest
              of the circular buffer is filled with copies of <tt>item</tt>.
              In case the resulting size exceeds the current capacity
              the capacity is set to <tt>new_size</tt>.
              If the new size is lower than the current size, the first
              (leftmost) <tt>((*this).size() - new_size)</tt> elements will be removed.
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void resize(size_type new_size, param_value_type item = T()) {
        if (new_size > size()) {
            if (new_size > capacity())
                set_capacity(new_size);
            insert(end(), new_size - size(), item);
        } else {
            erase(begin(), end() - new_size);
        }
    }

// Construction/Destruction

    //! Create an empty circular buffer with a given capacity.
    /*!
        \post <tt>(*this).capacity() == capacity \&\& (*this).size == 0</tt>
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
    */
    explicit circular_buffer(
        size_type capacity,
        const allocator_type& a = allocator_type())
    : m_size(0), m_alloc(a) {
        m_first = m_last = m_buff = allocate(capacity);
        m_end = m_buff + capacity;
    }

    //! Create a full circular buffer with a given capacity and filled with copies of <tt>item</tt>.
    /*!
        \post <tt>(*this).size() == capacity \&\& (*this)[0] == (*this)[1] == ... == (*this).back() == item</tt>
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
    */
    circular_buffer(
        size_type capacity,
        param_value_type item,
        const allocator_type& a = allocator_type())
    : m_size(capacity), m_alloc(a) {
        m_first = m_last = m_buff = allocate(capacity);
        m_end = m_buff + capacity;
        BOOST_CB_TRY
        std::uninitialized_fill_n(m_buff, size(), item);
        BOOST_CB_UNWIND(deallocate(m_buff, capacity))
    }

    //! Copy constructor.
    /*!
        \post <tt>*this == cb</tt>
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
    */
    circular_buffer(const circular_buffer<T, Alloc>& cb)
    : m_size(cb.size()), m_alloc(cb.get_allocator()) {
        m_first = m_last = m_buff = allocate(cb.capacity());
        BOOST_CB_TRY
        m_end = std::uninitialized_copy(cb.begin(), cb.end(), m_buff);
        BOOST_CB_UNWIND(deallocate(m_buff, cb.capacity()))
    }

    //! Create a circular buffer with a copy of a range.
    /*!
        \post <tt>(*this).capacity() == capacity</tt><br>
              If the number of items to copy from the range
              <tt>[first, last)</tt> is greater than the specified
              <tt>capacity</tt> then only elements from the range
              <tt>[last - capacity, last)</tt> will be copied.
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
    */
    template <class InputIterator>
    circular_buffer(
        size_type capacity,
        InputIterator first,
        InputIterator last,
        const allocator_type& a = allocator_type())
    : m_alloc(a) {
        m_first = m_buff = allocate(capacity);
        m_end = m_buff + capacity;
        size_type diff = std::distance(first, last);
        if (diff > capacity) {
            std::advance(first, diff - capacity);
            m_size = capacity;
            m_last = m_buff;
        } else {
            m_size = diff;
            if (diff == capacity) 
                m_last = m_buff;
            else
                m_last = m_buff + size();
        }
        BOOST_CB_TRY
        std::uninitialized_copy(first, last, m_buff);
        BOOST_CB_UNWIND(deallocate(m_buff, capacity))
    }

    //! Destructor.
    ~circular_buffer() { destroy(); }

private:
// Helper functors

    // Functor for assigning n items.
    struct assign_n {
        size_type m_n;
        param_value_type m_item;
        assign_n(size_type n, param_value_type item) : m_n(n), m_item(item) {}
        void operator () (pointer p) const {
            std::uninitialized_fill_n(p, m_n, m_item);
        }
    private:
        assign_n& operator = (const assign_n&); // do not generate
    };

    // Functor for assigning range of items.
    template <class InputIterator>
    struct assign_range {
        InputIterator m_first;
        InputIterator m_last;
        assign_range(InputIterator first, InputIterator last) : m_first(first), m_last(last) {}
        void operator() (pointer p) const {
            std::uninitialized_copy(m_first, m_last, p);
        }
    };

public:
// Assign methods

    //! Assignment operator.
    /*!
        \post <tt>*this == cb</tt>
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    circular_buffer<T, Alloc>& operator = (const circular_buffer<T, Alloc>& cb) {
        if (this == &cb)
            return *this;
        pointer buff = allocate(cb.capacity());
        BOOST_CB_TRY
        pointer last = std::uninitialized_copy(cb.begin(), cb.end(), buff);
        destroy();
        m_size = cb.size();
        m_first = m_buff = buff;
        m_end = m_buff + cb.capacity();
        m_last = full() ? m_buff : last;
        BOOST_CB_UNWIND(deallocate(buff, cb.capacity()))
        return *this;
    }

    //! Assign <tt>n</tt> items into the circular buffer.
    /*!
        \post <tt>(*this).size() == n \&\&
              (*this)[0] == (*this)[1] == ... == (*this).back() == item</tt><br>
              If the number of items to be assigned exceeds
              the capacity of the circular buffer the capacity
              is increased to <tt>n</tt> otherwise it stays unchanged.
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void assign(size_type n, param_value_type item) { do_assign(n, assign_n(n, item)); }
    
    //! Assign a copy of range.
    /*!
        \post <tt>(*this).size() == std::distance(first, last)</tt><br>
              If the number of items to be assigned exceeds
              the capacity of the circular buffer the capacity
              is set to that number otherwise is stays unchanged.
        \throws "An allocation error" if memory is exhausted (<tt>std::bad_alloc</tt> if standard allocator is used).
        \throws Whatever T::T(const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    template <class InputIterator>
    void assign(InputIterator first, InputIterator last) {
        assign(first, last, cb_details::cb_iterator_category_traits<InputIterator>::tag());
    }

    //! Swap the contents of two circular buffers.
    /*!
        \post <tt>this</tt> contains elements of <tt>cb</tt> and vice versa.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void swap(circular_buffer& cb) {
        std::swap(m_alloc, cb.m_alloc); // in general this is not necessary,
                                        // because allocators should not have state
        std::swap(m_buff, cb.m_buff);
        std::swap(m_end, cb.m_end);
        std::swap(m_first, cb.m_first);
        std::swap(m_last, cb.m_last);
        std::swap(m_size, cb.m_size);
    }

// push and pop

    //! Insert a new element at the end.
    /*!
        \post <tt>(*this).back() == item</tt><br>
              If the circular buffer is full, the first (leftmost) element will be removed.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void push_back(param_value_type item) {
        if (full()) {
            if (empty())
                return;
            *m_last = item;
            increment(m_first);
            m_last = m_first;
        } else {
            m_alloc.construct(m_last, item);
            increment(m_last);
            ++m_size;
        }
    }

    //! Insert a new element with the default value at the end.
    /*!
        \post <tt>(*this).back() == value_type()</tt><br>
              If the circular buffer is full, the first (leftmost) element will be removed.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void push_back() { push_back(value_type()); }

    //! Insert a new element at the start.
    /*!
        \post <tt>(*this).front() == item</tt><br>
              If the circular buffer is full, the last (rightmost) element will be removed.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void push_front(param_value_type item) {
        BOOST_CB_TRY
        if (full()) {
            if (empty())
                return;
            decrement(m_first);
            *m_first = item;
            m_last = m_first;
        } else {
            decrement(m_first);
            m_alloc.construct(m_first, item);
            ++m_size;
        }
        BOOST_CB_UNWIND(increment(m_first))
    }

    //! Insert a new element with the default value at the start.
    /*!
        \post <tt>(*this).front() == value_type()</tt><br>
              If the circular buffer is full, the last (rightmost) element will be removed.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void push_front() { push_front(value_type()); }

    //! Remove the last (rightmost) element.
    /*!
        \pre <tt>iterator it = (*this).end()</tt>
        \post <tt>(*this).end() != it</tt>
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void pop_back() {
        decrement(m_last);
        m_alloc.destroy(m_last);
        --m_size;
    }

    //! Remove the first (leftmost) element.
    /*!
        \pre <tt>iterator it = (*this).begin()</tt>
        \post <tt>(*this).begin() != it</tt>
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void pop_front() {
        m_alloc.destroy(m_first);
        increment(m_first);
        --m_size;
    }

private:
// Helper wrappers

    // Iterator dereference wrapper.
    template <class InputIterator>
    struct item_wrapper {
        mutable InputIterator m_it;
        item_wrapper(InputIterator it) : m_it(it) {}
        operator const_reference () const { return *m_it++; }
    };

public:
// Insert

    //! Insert the <tt>item</tt> before the given position.
    /*!
        \post The <tt>item</tt> will be inserted at the position <tt>pos</tt>.<br>
              If the circular buffer is full, the first (leftmost) element will be removed.
        \return iterator to the inserted element.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator insert(iterator pos, param_value_type item) {
        if (full() && pos == begin())
            return begin();
        if (pos.m_it == 0) {
            if (full())
                *m_last = item;
            else
                m_alloc.construct(m_last, item);
            pos.m_it = m_last;
        } else {
            pointer src = m_last;
            pointer dest = m_last;
            BOOST_CB_TRY
            while (src != pos.m_it) {
                decrement(src);
                create_copy(dest, *src);
                decrement(dest);
            }
            *pos = item;
            BOOST_CB_UNWIND(
                for (pointer it = m_last; it != dest; decrement(it))
                    destroy_copy(it);
            )
        }
        increment(m_last);
        if (full())
            increment(m_first);
        else
            ++m_size;
        return iterator(this, pos.m_it);
    }

    //! Insert a new element with the default value before the given position.
    /*!
        \post <tt>value_type()</tt> will be inserted at the position <tt>pos</tt>.<br>
              If the circular buffer is full, the first (leftmost) element will be removed.
        \return iterator to the inserted element.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator insert(iterator pos) { return insert(pos, value_type()); }

    //! Insert <tt>n</tt> copies of the item before the given position.
    /*!
        \post This operation preserves the capacity of the circular buffer.
              If the insertion would result in exceeding the capacity
              of the circular buffer then the necessary number of elements
              from the beginning (left) of the circular buffer will be removed
              or not all <tt>n</tt> elements will be inserted or both.<tt><br>
              Example:<br>
                original circular buffer |1|2|3|4| | | - capacity: 6, size: 4<br>
                position ---------------------^<br>
                insert(position, (size_t)5, 6);<br>
                (If the operation won't preserve capacity, the buffer
                would look like this |1|2|6|6|6|6|6|3|4|)<br>
                RESULTING circular buffer |6|6|6|6|3|4| - capacity: 6, size: 6</tt>
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void insert(iterator pos, size_type n, param_value_type item) {
        if (n == 0)
            return;
        size_type copy = capacity() - (end() - pos);
        if (copy == 0)
            return;
        if (n > copy)
            n = copy;
        insert_n_item(pos, n, item);
    }

    //! Insert the range <tt>[first, last)</tt> before the given position.
    /*!
        \post This operation preserves the capacity of the circular buffer.
              If the insertion would result in exceeding the capacity
              of the circular buffer then the necessary number of elements
              from the beginning (left) of the circular buffer will be removed
              or not the whole range will be inserted or both.
              In case the whole range cannot be inserted it will be inserted just
              some elements from the end (right) of the range (see the example).<tt><br>
              Example:<br>
                array to insert: int array[] = { 5, 6, 7, 8, 9 };<br>
                original circular buffer |1|2|3|4| | | - capacity: 6, size: 4<br>
                position ---------------------^<br>
                insert(position, array, array + 5);<br>
                (If the operation won't preserve capacity, the buffer
                would look like this |1|2|5|6|7|8|9|3|4|)<br>
                RESULTING circular buffer |6|7|8|9|3|4| - capacity: 6, size: 6</tt>
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    template <class InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last) {
        insert(pos, first, last, cb_details::cb_iterator_category_traits<InputIterator>::tag());
    }

    //! Insert an <tt>item</tt> before the given position.
    /*!
        \post The <tt>item</tt> will be inserted at the position <tt>pos</tt>.<br>
              If the circular buffer is full, the last element (rightmost) will be removed.
        \return iterator to the inserted element.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator rinsert(iterator pos, param_value_type item) {
        if (full() && pos == end())
            return end();
        if (pos == begin()) {
            BOOST_CB_TRY
            decrement(m_first);
            if (full())
                *m_first = item;
            else
                m_alloc.construct(m_first, item);
            BOOST_CB_UNWIND(increment(m_first))
        } else {
            pointer src = m_first;
            pointer dest = m_first;
            pointer it = get_valid_pointer(pos.m_it);
            decrement(dest);
            BOOST_CB_TRY
            while (src != it) {
                create_copy(dest, *src);
                increment(src);
                increment(dest);
            }
            decrement(m_first);
            *--pos = item;
            BOOST_CB_UNWIND(
                it = m_first;
                for (increment(m_first); it != dest; increment(it))
                    destroy_copy(it);
            )
        }
        if (full())
            decrement(m_last);
        else
            ++m_size;
        return iterator(this, pos.m_it);
    }

    //! Insert a new element with the default value before the given position.
    /*!
        \post <tt>value_type()</tt> will be inserted at the position <tt>pos</tt>.<br>
              If the circular buffer is full, the last (rightmost) element will be removed.
        \return iterator to the inserted element.
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator rinsert(iterator pos) { return rinsert(pos, value_type()); }

    //! Insert <tt>n</tt> copies of the item before the given position.
    /*!
        \post This operation preserves the capacity of the circular buffer.
              If the insertion would result in exceeding the capacity
              of the circular buffer then the necessary number of elements
              from the end (right) of the circular buffer will be removed
              or not all <tt>n</tt> elements will be inserted or both.<tt><br>
              Example:<br>
                original circular buffer |1|2|3|4| | | - capacity: 6, size: 4<br>
                position ---------------------^<br>
                insert(position, (size_t)5, 6);<br>
                (If the operation won't preserve capacity, the buffer
                would look like this |1|2|6|6|6|6|6|3|4|)<br>
                RESULTING circular buffer |1|2|6|6|6|6| - capacity: 6, size: 6</tt>
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void rinsert(iterator pos, size_type n, param_value_type item) { rinsert_n_item(pos, n, item); }

    //! Insert the range <tt>[first, last)</tt> before the given position.
    /*!
        \post This operation preserves the capacity of the circular buffer.
              If the insertion would result in exceeding the capacity
              of the circular buffer then the necessary number of elements
              from the end (right) of the circular buffer will be removed
              or not the whole range will be inserted or both.
              In case the whole range cannot be inserted it will be inserted just
              some elements from the beginning (left) of the range (see the example).<tt><br>
              Example:<br>
                array to insert: int array[] = { 5, 6, 7, 8, 9 };<br>
                original circular buffer |1|2|3|4| | | - capacity: 6, size: 4<br>
                position ---------------------^<br>
                insert(position, array, array + 5);<br>
                (If the operation won't preserve capacity, the buffer
                would look like this |1|2|5|6|7|8|9|3|4|)<br>
                RESULTING circular buffer |1|2|5|6|7|8| - capacity: 6, size: 6</tt>
        \throws Whatever T::T(const T&) throws.
        \throws Whatever T::operator = (const T&) throws.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator first, InputIterator last) {
        rinsert(pos, first, last, cb_details::cb_iterator_category_traits<InputIterator>::tag());
    }

// Erase

    //! Erase the element at the given position.
    /*!
        \pre <tt>size_type old_size = (*this).size()</tt>
        \post <tt>(*this).size() == old_size - 1</tt><br>
              Removes an element at the position <tt>pos</tt>.
        \return iterator to the first element remaining beyond the removed
                element or <tt>(*this).end()</tt> if no such element exists.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator erase(iterator pos) {
        std::copy(pos + 1, end(), pos);
        decrement(m_last);
        m_alloc.destroy(m_last);
        --m_size;
        return iterator(this, pos.m_it == m_last ? 0 : pos.m_it);
    }

    //! Erase the range <tt>[first, last)</tt>.
    /*!
        \pre <tt>size_type old_size = (*this).size()</tt>
        \post <tt>(*this).size() == old_size - std::distance(first, last)</tt><br>
              Removes the elements from the range <tt>[first, last)</tt>.
        \return iterator to the first element remaining beyond the removed
                element or <tt>(*this).end()</tt> if no such element exists.
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    iterator erase(iterator first, iterator last) {
        if (first != last)
            std::copy(last, end(), first);
        difference_type diff = last - first;
        m_size -= diff;
        for (; diff > 0; --diff) {
            decrement(m_last);
            m_alloc.destroy(m_last);
        }
        return iterator(this, first.m_it == m_last ? 0 : first.m_it);
    }

    //! Erase all the stored elements.
    /*!
        \post (*this).size() == 0
        \note For iterator invalidation see the <a href="../circular_buffer.html#invalidation">documentation</a>.
    */
    void clear() {
        destroy_content();
        m_first = m_last = m_buff;
        m_size = 0;
    }

private:
// Helper methods

    //! Check if the <tt>index</tt> is valid.
    void check_position(size_type index) const {
        if (index >= size())
            throw_exception(std::out_of_range("circular_buffer"));
    }

    //! Increment the pointer.
    template <class Pointer0>
    void increment(Pointer0& p) const {
        if (++p == m_end)
            p = m_buff;
    }

    //! Decrement the pointer.
    template <class Pointer0>
    void decrement(Pointer0& p) const {
        if (p == m_buff)
            p = m_end;
        --p;
    }

    //! Add <tt>n</tt> to the pointer.
    template <class Pointer0>
    Pointer0 add(Pointer0 p, difference_type n) const {
        return p + (n < (m_end - p) ? n : n - capacity());
    }

    //! Subtract <tt>n</tt> from the pointer.
    template <class Pointer0>
    Pointer0 sub(Pointer0 p, difference_type n) const {
        return p - (n > (p - m_buff) ? n - capacity() : n);
    }

    //! Return valid pointer.
    pointer get_valid_pointer(pointer p) const { return p == 0 ? m_last : p; }

    //! Does the pointer point to the uninitialized memory?
    bool is_uninitialized(pointer p) const { return p >= m_last && (m_first < m_last || p < m_first); }

    //! Create a copy of the <tt>item</tt> at the given position.
    /*!
        The copy is created either at uninitialized memory
        or replaces the old item.
    */
    void create_copy(pointer pos, param_value_type item) {
        if (is_uninitialized(pos))
            m_alloc.construct(pos, item);
        else
            *pos = item;
    }

    //! Try to recover when the create_copy fails.
    void destroy_copy(pointer pos) {
        if (is_uninitialized(pos))
            m_alloc.destroy(pos);
        // the assignment cannot be rolled back
    }

    //! Allocate memory.
    pointer allocate(size_type n) {
        if (n > max_size())
            throw_exception(std::length_error("circular_buffer"));
        return (n == 0) ? 0 : m_alloc.allocate(n, 0);
    }

    //! Deallocate memory.
    void deallocate(pointer p, size_type n) {
        if (p != 0)
            m_alloc.deallocate(p, n);
    }

    //! Destroy the content of the circular buffer.
    void destroy_content() {
        iterator last = end();
        for (iterator it = begin(); it != last; ++it)
            m_alloc.destroy(it.m_it);
    }

    //! Destroy content and frees allocated memory.
    void destroy() {
        destroy_content();
        deallocate(m_buff, capacity());
    }

    //! Helper assign method.
    template <class InputIterator>
    void assign(InputIterator n, InputIterator item, cb_details::cb_int_iterator_tag) {
        assign((size_type)n, item);
    }

    //! Helper assign method.
    template <class InputIterator>
    void assign(InputIterator first, InputIterator last, std::input_iterator_tag) {
        do_assign(std::distance(first, last), assign_range<InputIterator>(first, last));
    }

    //! Helper assign method.
    template <class Functor>
    void do_assign(size_type n, const Functor& fnc) {
        if (n > capacity()) {
            pointer buff = allocate(n);
            BOOST_CB_TRY
            fnc(buff);
            BOOST_CB_UNWIND(deallocate(buff, n))
            destroy();
            m_buff = buff;
            m_end = m_buff + n;
        } else {
            destroy_content();
            BOOST_CB_TRY
            fnc(m_buff);
            BOOST_CB_UNWIND(m_size = 0;)
        }
        m_size = n;
        m_first = m_buff;
        m_last = add(m_buff, size());
    }

    //! Helper insert method.
    template <class InputIterator>
    void insert(iterator pos, InputIterator n, InputIterator item, cb_details::cb_int_iterator_tag) {
        insert(pos, (size_type)n, item);
    }

    //! Helper insert method.
    template <class InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last, std::input_iterator_tag) {
        difference_type n = std::distance(first, last);
        if (n == 0)
            return;
        difference_type copy = capacity() - (end() - pos);
        if (copy == 0)
            return;
        if (n > copy) {
            std::advance(first, n - copy);
            n = copy;
        }
        insert_n_item(pos, n, item_wrapper<InputIterator>(first));
    }

    //! Helper insert method.
    template <class Item>
    void insert_n_item(iterator pos, size_type n, const Item& item) {
        size_type construct = capacity() - size();
        if (construct > n)
            construct = n;
        if (pos.m_it == 0) {
            size_type ii = 0;
            pointer p = m_last;
            BOOST_CB_TRY
            for (; ii < construct; ++ii, increment(p))
                m_alloc.construct(p, item);
            for (;ii < n; ++ii, increment(p))
                *p = item;
            BOOST_CB_UNWIND(
                size_type unwind = ii < construct ? ii : construct;
                for (ii = 0, p = m_last; ii < unwind; ++ii, increment(p))
                    m_alloc.destroy(p);
            )
        } else {
            pointer src = m_last;
            pointer dest = add(m_last, n - 1);
            pointer p = pos.m_it;
            size_type ii = 0;
            BOOST_CB_TRY
            while (src != p) {
                decrement(src);
                create_copy(dest, *src);
                decrement(dest);
            }
            for (; ii < n; ++ii, increment(p))
                create_copy(p, item);
            BOOST_CB_UNWIND(
                for (p = add(m_last, n - 1); p != dest; decrement(p))
                    destroy_copy(p);
                for (n = 0, p = pos.m_it; n < ii; ++n, increment(p))
                    destroy_copy(p);
            )
        }
        m_last = add(m_last, n);
        m_first = add(m_first, n - construct);
        m_size += construct;
    }

    //! Helper rinsert method.
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator n, InputIterator item, cb_details::cb_int_iterator_tag) {
        rinsert(pos, (size_type)n, item);
    }

    //! Helper rinsert method.
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator first, InputIterator last, std::input_iterator_tag) {
        rinsert_n_item(pos, std::distance(first, last), item_wrapper<InputIterator>(first));
    }

    //! Helper rinsert method.
    template <class Item>
    void rinsert_n_item(iterator pos, size_type n, const Item& item) {
        if (n == 0)
            return;
        size_type copy = capacity() - (pos - begin());
        if (copy == 0)
            return;
        if (n > copy)
            n = copy;
        size_type construct = capacity() - size();
        if (construct > n)
            construct = n;
        if (pos == begin()) {
            pointer p = sub(get_valid_pointer(pos.m_it), n);
            size_type ii = n;
            BOOST_CB_TRY
            for (;ii > construct; --ii, increment(p))
                *p = item;
            for (; ii > 0; --ii, increment(p))
                m_alloc.construct(p, item);
            BOOST_CB_UNWIND(
                size_type unwind = ii < construct ? construct - ii : 0;
                p = sub(get_valid_pointer(pos.m_it), construct);
                for (ii = 0; ii < unwind; ++ii, increment(p))
                    m_alloc.destroy(p);
            )
        } else {
            pointer src = m_first;
            pointer dest = sub(m_first, n);
            pointer p = get_valid_pointer(pos.m_it);
            size_type ii = 0;
            BOOST_CB_TRY
            while (src != p) {
                create_copy(dest, *src);
                increment(src);
                increment(dest);
            }
            p = sub(p, n);
            for (; ii < n; ++ii, increment(p))
                create_copy(p, item);
            BOOST_CB_UNWIND(
                for (p = sub(m_first, n); p != dest; increment(p))
                    destroy_copy(p);
                p = sub(get_valid_pointer(pos.m_it), n);
                for (n = 0; n < ii; ++n, increment(p))
                    destroy_copy(p);
            )
        }
        m_first = sub(m_first, n);
        m_last = sub(m_last, n - construct);
        m_size += construct;
    }
};

// Non-member functions

//! Test two circular buffers for equality.
template <class T, class Alloc>
inline bool operator == (const circular_buffer<T, Alloc>& lhs,
                         const circular_buffer<T, Alloc>& rhs) {
    return lhs.size() == rhs.size() &&
        std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator < (const circular_buffer<T, Alloc>& lhs,
                        const circular_buffer<T, Alloc>& rhs) {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

#if !defined(BOOST_NO_FUNCTION_TEMPLATE_ORDERING) || defined(BOOST_MSVC)

//! Test two circular buffers for non-equality.
template <class T, class Alloc>
inline bool operator != (const circular_buffer<T, Alloc>& lhs,
                         const circular_buffer<T, Alloc>& rhs) {
    return !(lhs == rhs);
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator > (const circular_buffer<T, Alloc>& lhs,
                        const circular_buffer<T, Alloc>& rhs) {
    return rhs < lhs;
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator <= (const circular_buffer<T, Alloc>& lhs,
                         const circular_buffer<T, Alloc>& rhs) {
    return !(rhs < lhs);
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator >= (const circular_buffer<T, Alloc>& lhs,
                         const circular_buffer<T, Alloc>& rhs) {
    return !(lhs < rhs);
}

//! Swap the contents of two circular buffers.
template <class T, class Alloc>
inline void swap(circular_buffer<T, Alloc>& lhs, circular_buffer<T, Alloc>& rhs) {
    lhs.swap(rhs);
}

#endif // #if !defined(BOOST_NO_FUNCTION_TEMPLATE_ORDERING) || defined(BOOST_MSVC)

#undef BOOST_CB_UNWIND
#undef BOOST_CB_TRY

} // namespace boost

#endif // #if !defined(BOOST_CIRCULAR_BUFFER_BASE_HPP)
