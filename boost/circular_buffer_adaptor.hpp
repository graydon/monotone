// Implementation of the circular buffer adaptor.

// Copyright (c) 2003
// Jan Gaspar, Whitestein Technologies

// Permission to use or copy this software for any purpose is hereby granted 
// without fee, provided the above notices are retained on all copies.
// Permission to modify the code and to distribute modified code is granted,
// provided the above notices are retained, and a notice that the code was
// modified is included with the above copyright notice.

// This material is provided "as is", with absolutely no warranty expressed
// or implied. Any use is at your own risk.

#if !defined(BOOST_CIRCULAR_BUFFER_ADAPTOR_HPP)
#define BOOST_CIRCULAR_BUFFER_ADAPTOR_HPP

namespace boost {

/*!
    \class circular_buffer_space_optimized
    \brief Space optimized circular buffer container adaptor.
    \param T The type of the elements stored in the space optimized circular buffer.
    \param Alloc The allocator type used for all internal memory management.
    \author <a href="mailto:jano_gaspar@yahoo.com">Jan Gaspar</a>
    \version 1.0
    \date 2003

    For more information how to use the space optimized circular
    buffer see the <a href="../circular_buffer_adaptor.html">
    documentation</a>.
*/
template<class T, class Alloc>
class circular_buffer_space_optimized : private circular_buffer<T, Alloc> {
public:
// Typedefs

    typedef typename circular_buffer<T, Alloc>::value_type value_type;
    typedef typename circular_buffer<T, Alloc>::pointer pointer;
    typedef typename circular_buffer<T, Alloc>::const_pointer const_pointer;
    typedef typename circular_buffer<T, Alloc>::reference reference;
    typedef typename circular_buffer<T, Alloc>::const_reference const_reference;
    typedef typename circular_buffer<T, Alloc>::size_type size_type;
    typedef typename circular_buffer<T, Alloc>::difference_type difference_type;
    typedef typename circular_buffer<T, Alloc>::allocator_type allocator_type;
    typedef typename circular_buffer<T, Alloc>::param_value_type param_value_type;
    typedef typename circular_buffer<T, Alloc>::const_iterator const_iterator;
    typedef typename circular_buffer<T, Alloc>::iterator iterator;
    typedef typename circular_buffer<T, Alloc>::const_reverse_iterator const_reverse_iterator;
    typedef typename circular_buffer<T, Alloc>::reverse_iterator reverse_iterator;

// Inherited
    
    using circular_buffer<T, Alloc>::get_allocator;
    using circular_buffer<T, Alloc>::begin;
    using circular_buffer<T, Alloc>::end;
    using circular_buffer<T, Alloc>::rbegin;
    using circular_buffer<T, Alloc>::rend;
    using circular_buffer<T, Alloc>::operator[];
    using circular_buffer<T, Alloc>::at;
    using circular_buffer<T, Alloc>::front;
    using circular_buffer<T, Alloc>::back;
    using circular_buffer<T, Alloc>::data;
    using circular_buffer<T, Alloc>::size;
    using circular_buffer<T, Alloc>::max_size;
    using circular_buffer<T, Alloc>::empty;

private:
// Member variables

    //! The capacity of the optimized circular buffer.
    size_type m_final_capacity;

public:
// Overridden

    //! See the circular_buffer source documentation.
    bool full() const { return size() == capacity(); }
    
    //! See the circular_buffer source documentation.
    size_type capacity() const { return m_final_capacity; }
    
    //! See the circular_buffer source documentation.
    void set_capacity(size_type new_capacity) {
        if (m_final_capacity > new_capacity)
            circular_buffer<T, Alloc>::set_capacity(new_capacity);
        m_final_capacity = new_capacity;
    }
    
    //! See the circular_buffer source documentation.
    void resize(size_type new_size, param_value_type item = T()) {
        if (new_size > size()) {
            if (new_size > capacity())
                m_final_capacity = new_size;
            insert(end(), new_size - size(), item);
        } else {
            erase(begin(), end() - new_size);
        }
    }

    //! See the circular_buffer source documentation.
    explicit circular_buffer_space_optimized(
        size_type capacity,
        const allocator_type& a = allocator_type())
    : circular_buffer<T, Alloc>(0, a), m_final_capacity(capacity) {}

    //! See the circular_buffer source documentation.
    circular_buffer_space_optimized(
        size_type capacity,
        param_value_type item,
        const allocator_type& a = allocator_type())
    : circular_buffer<T, Alloc>(capacity, item, a), m_final_capacity(capacity) {}

    // Default copy constructor

    //! See the circular_buffer source documentation.
    template <class InputIterator>
    circular_buffer_space_optimized(
        size_type capacity,
        InputIterator first,
        InputIterator last,
        const allocator_type& a = allocator_type())
    : circular_buffer<T, Alloc>(init_capacity(capacity, first, last), first, last, a)
    , m_final_capacity(capacity) {}

    // Default destructor

    // Default assign operator

    //! See the circular_buffer source documentation.
    void assign(size_type n, param_value_type item) {
        if (n > m_final_capacity)
            m_final_capacity = n;
        circular_buffer<T, Alloc>::assign(n, item);
    }

    //! See the circular_buffer source documentation.
    template <class InputIterator>
    void assign(InputIterator first, InputIterator last) {
        circular_buffer<T, Alloc>::assign(first, last);
        size_type capacity = circular_buffer<T, Alloc>::capacity();
        if (capacity > m_final_capacity)
            m_final_capacity = capacity;
    }

    //! See the circular_buffer source documentation.
    void swap(circular_buffer_space_optimized& cb) {
        std::swap(m_final_capacity, cb.m_final_capacity);
        circular_buffer<T, Alloc>::swap(cb);
    }

    //! See the circular_buffer source documentation.
    void push_back(param_value_type item) {
        check_low_capacity();
        circular_buffer<T, Alloc>::push_back(item);
    }

    //! See the circular_buffer source documentation.
    void push_back() { push_back(value_type()); }

    //! See the circular_buffer source documentation.
    void push_front(param_value_type item) {
        check_low_capacity();
        circular_buffer<T, Alloc>::push_front(item);
    }

    //! See the circular_buffer source documentation.
    void push_front() { push_front(value_type()); }

    //! See the circular_buffer source documentation.
    void pop_back() {
        circular_buffer<T, Alloc>::pop_back();
        check_high_capacity();
    }

    //! See the circular_buffer source documentation.
    void pop_front() {
        circular_buffer<T, Alloc>::pop_front();
        check_high_capacity();
    }

    //! See the circular_buffer source documentation.
    iterator insert(iterator pos, param_value_type item) {
        size_type index = pos - begin();
        check_low_capacity();
        return circular_buffer<T, Alloc>::insert(begin() + index, item);
    }

    //! See the circular_buffer source documentation.
    iterator insert(iterator pos) { return insert(pos, value_type()); }

    //! See the circular_buffer source documentation.
    void insert(iterator pos, size_type n, param_value_type item) {
        size_type index = pos - begin();
        check_low_capacity(n);
        circular_buffer<T, Alloc>::insert(begin() + index, n, item);
    }

    //! See the circular_buffer source documentation.
    template <class InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last) {
        insert(pos, first, last, cb_details::cb_iterator_category_traits<InputIterator>::tag());
    }
    
    //! See the circular_buffer source documentation.
    iterator rinsert(iterator pos, param_value_type item) {
        size_type index = pos - begin();
        check_low_capacity();
        return circular_buffer<T, Alloc>::rinsert(begin() + index, item);
    }

    //! See the circular_buffer source documentation.
    iterator rinsert(iterator pos) { return rinsert(pos, value_type()); }

    //! See the circular_buffer source documentation.
    void rinsert(iterator pos, size_type n, param_value_type item) {
        size_type index = pos - begin();
        check_low_capacity(n);
        circular_buffer<T, Alloc>::rinsert(begin() + index, n, item);
    }

    //! See the circular_buffer source documentation.
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator first, InputIterator last) {
        rinsert(pos, first, last, cb_details::cb_iterator_category_traits<InputIterator>::tag());
    }

    //! See the circular_buffer source documentation.
    iterator erase(iterator pos) {
        iterator it = circular_buffer<T, Alloc>::erase(pos);
        size_type index = it - begin();
        check_high_capacity();
        return begin() + index;
    }

    //! See the circular_buffer source documentation.
    iterator erase(iterator first, iterator last) {
        iterator it = circular_buffer<T, Alloc>::erase(first, last);
        size_type index = it - begin();
        circular_buffer<T, Alloc>::set_capacity(size());
        return begin() + index;
    }

    //! See the circular_buffer source documentation.
    void clear() { circular_buffer<T, Alloc>::set_capacity(0); }

private:
// Helper methods

    //! Check for low capacity.
    /*
        \post If the capacity is low it will be increased.
    */
    void check_low_capacity() {
        if (circular_buffer<T, Alloc>::full() && size() < m_final_capacity) {
            size_type new_capacity = empty() ? 1 : size() << 1; // (size * 2)
            if (new_capacity > m_final_capacity)
                new_capacity = m_final_capacity;
            circular_buffer<T, Alloc>::set_capacity(new_capacity);
        }
    }

    //! Check for low capacity.
    /*
        \post If the capacity is low it will be increased.
    */
    void check_low_capacity(size_type n) {
        size_type new_capacity = size() + n;
        if (new_capacity > m_final_capacity)
            new_capacity = m_final_capacity;
        if (new_capacity > circular_buffer<T, Alloc>::capacity())
            circular_buffer<T, Alloc>::set_capacity(new_capacity);
    }

    //! Check for high capacity.
    /*
        \post If the capacity is high it will be decreased.
    */
    void check_high_capacity() {
        size_type new_capacity = circular_buffer<T, Alloc>::capacity() >> 1; // (capacity / 2)
        if (new_capacity >= size())
            circular_buffer<T, Alloc>::set_capacity(new_capacity);
    }

    //! Determine the initial capacity.
    template <class InputIterator>
    static size_type init_capacity(size_type capacity, InputIterator first, InputIterator last) {
        size_type diff = std::distance(first, last);
        return diff > capacity ? capacity : diff;
    }

    //! Helper insert method.
    template <class InputIterator>
    void insert(iterator pos, InputIterator n, InputIterator item, cb_details::cb_int_iterator_tag) {
        insert(pos, (size_type)n, item);
    }

    //! Helper insert method.
    template <class InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last, std::input_iterator_tag) {
        size_type index = pos - begin();
        check_low_capacity(std::distance(first, last));
        circular_buffer<T, Alloc>::insert(begin() + index, first, last);
    }

    //! Helper rinsert method.
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator n, InputIterator item, cb_details::cb_int_iterator_tag) {
        rinsert(pos, (size_type)n, item);
    }

    //! Helper rinsert method.
    template <class InputIterator>
    void rinsert(iterator pos, InputIterator first, InputIterator last, std::input_iterator_tag) {
        size_type index = pos - begin();
        check_low_capacity(std::distance(first, last));
        circular_buffer<T, Alloc>::rinsert(begin() + index, first, last);
    }
};

// Non-member functions

//! Test two space optimized circular buffers for equality.
template <class T, class Alloc>
inline bool operator == (const circular_buffer_space_optimized<T, Alloc>& lhs,
                         const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return lhs.size() == rhs.size() &&
        std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator < (const circular_buffer_space_optimized<T, Alloc>& lhs,
                        const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

#if !defined(BOOST_NO_FUNCTION_TEMPLATE_ORDERING) || defined(BOOST_MSVC)

//! Test two space optimized circular buffers for non-equality.
template <class T, class Alloc>
inline bool operator != (const circular_buffer_space_optimized<T, Alloc>& lhs,
                         const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return !(lhs == rhs);
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator > (const circular_buffer_space_optimized<T, Alloc>& lhs,
                        const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return rhs < lhs;
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator <= (const circular_buffer_space_optimized<T, Alloc>& lhs,
                         const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return !(rhs < lhs);
}

//! Lexicographical comparison.
template <class T, class Alloc>
inline bool operator >= (const circular_buffer_space_optimized<T, Alloc>& lhs,
                         const circular_buffer_space_optimized<T, Alloc>& rhs) {
    return !(lhs < rhs);
}

//! Swap the contents of two space optimized circular buffers.
template <class T, class Alloc>
inline void swap(circular_buffer_space_optimized<T, Alloc>& lhs,
                 circular_buffer_space_optimized<T, Alloc>& rhs) {
    lhs.swap(rhs);
}

#endif // #if !defined(BOOST_NO_FUNCTION_TEMPLATE_ORDERING) || defined(BOOST_MSVC)

} // namespace boost

#endif // #if !defined(BOOST_CIRCULAR_BUFFER_ADAPTOR_HPP)
