// (C) Copyright Craig Henderson 2002
//               cdm.henderson@virgin.net
//
// Permission to use, copy, modify, distribute and sell this software
// and its documentation for any purpose is hereby granted without fee,
// provided that the above copyright notice appears in all copies and
// that both that copyright notice and this permission notice appear
// in supporting documentation.  The author makes no representations
// about the suitability of this software for any purpose.  It is
// provided "as is" without express or implied warranty.

// guarded header file to prevent multiple include compilation errors
#ifndef LCS_H_
#define LCS_H_

#include "detail/lcs_detail.hpp"

namespace boost {
    // Note: size_type must be a signed integral type
    //       'must be signed' is a requirement for the algorithm function correctly
    //       'must be integral' is a requirement imposed by the implementor for
    //       performance reasons. I don't believe this is unreasonable, but if
    //       someone can justify any reason where this is unreasonable, I will
    //       happily look at specializing integral/non integral functions.
    template<typename size_type,
             typename ItIn1,
             typename ItIn2,
             typename ItSubSeq>
    inline
    size_type
    longest_common_subsequence(ItIn1    begin_first,
                               ItIn1    end_first,
                               ItIn2    begin_second,
                               ItIn2    end_second,
                               ItSubSeq subsequence)
    {
        std::allocator<size_type> alloc;
        return longest_common_subsequence<size_type>(begin_first,
                                                     end_first,
                                                     begin_second,
                                                     end_second,
                                                     subsequence,
                                                     alloc);
    }

    template<typename size_type,
             typename Alloc,
             typename ItIn1,
             typename ItIn2,
             typename ItSubSeq>
    size_type
    longest_common_subsequence(ItIn1     begin_first,
                               ItIn1     end_first,
                               ItIn2     begin_second,
                               ItIn2     end_second,
                               ItSubSeq  subsequence,
                               Alloc    &alloc)
    {
        // calculate the length of each subsequence
        std::ptrdiff_t size_first  = std::distance<>(begin_first, end_first);
        std::ptrdiff_t size_second = std::distance<>(begin_second, end_second);

        // compare the heads of the sequences to skip equal
        // elements more efficiently
        register std::ptrdiff_t min_size = std::min(size_first, size_second);
        while (min_size > 0  &&  *begin_first == *begin_second)
        {
            *subsequence++ = *begin_first;
            ++begin_first;
            ++begin_second;
            --min_size;
            --size_first;
            --size_second;
        }

        // the algorithm will use less memory if the second sequence is longer
        // than the first, so we can swap the input iterators
        if (size_first > size_second)
        {
            std::swap(begin_first, begin_second);
            std::swap(end_first, end_second);
            std::swap(size_first, size_second);
        }

        // allocate a working array large enough for three entire
        // lengths of the first sequence
        const std::ptrdiff_t array_size = (size_first + 1) * ((sizeof(size_type) * 2) + sizeof(std::ptrdiff_t));

        // VC6's dinkumware STL does not provide an allocate without the hint
        size_type *pA = alloc.allocate(array_size, 0);

        size_type result;
        result = detail::linear_space_lcs(begin_first, end_first,
                                          begin_second, end_second,
                                          subsequence, pA);
        alloc.deallocate(pA, array_size);
        return result;
    }
    


    // calculates the length of the longest common subsequence
    //
    // Note: size_type must be a signed integral type
    //       'must be signed' is a requirement for the algorithm function correctly
    //       'must be integral' is a requirement imposed by the implementor for
    //       performance reasons. I don't believe this is unreasonable, but if
    //       someone can justify any reason where this is unreasonable, I will
    //       happily look at specializing integral/non integral functions.
    template<typename size_type, typename ItIn1, typename ItIn2>
    inline
    size_type
    longest_common_subsequence_length(ItIn1 begin_first, ItIn1 end_first,
                                      ItIn2 begin_second, ItIn2 end_second)
    {
        std::allocator<size_type> alloc;
        return longest_common_subsequence_length<size_type>(
            begin_first, end_first, begin_second, end_second, alloc);
    }

    template<typename size_type, typename Alloc, typename ItIn1, typename ItIn2>
    size_type
    longest_common_subsequence_length(ItIn1 begin_first, ItIn1 end_first,
                                      ItIn2 begin_second, ItIn2 end_second,
                                      Alloc &alloc)
    {
#ifdef BOOST_STATIC_ASSERT  // Not supported for BCC 5.5.1
        BOOST_STATIC_ASSERT(boost::is_integral<size_type>::value);
#endif
        // calculate the length of each subsequence
        std::ptrdiff_t min_size;
        const std::ptrdiff_t size_first  = std::distance<>(begin_first, end_first);
        const std::ptrdiff_t size_second = std::distance<>(begin_second, end_second);

#ifdef DBG_SCOPE
        DBG_SCOPE("longest_common_subsequence_length: Sequence lengths %lu and %lu",
                  size_first, size_second);
#endif

        // the algorithm will use less memory if the second sequence is longer
        // than the first, so we can swap the input iterators
        if (size_first > size_second)
        {
            std::swap(begin_first, begin_second);
            std::swap(end_first, end_second);
            min_size = size_second;
        }
        else
            min_size = size_first;

        const int array_size = ((min_size + 1) << 1) * sizeof(size_type);

        // VC6's dinkumware STL does not provide an allocate without the hint
        size_type *row1 = alloc.allocate(array_size, 0);
        size_type *row2 = row1 + min_size + 1;
        memset(row1, 0, array_size);

        register ItIn2 it2;
        for (it2=begin_second; it2!=end_second; ++it2)
        {
            register ItIn1      it1;
            register size_type *pA  = row1 + 1;
            register size_type *pB  = row2 + 1;
#ifndef BOOST_MSVC
            typename std::iterator_traits<ItIn1>::reference val2 = *it2;
#endif
            for (it1=begin_first; it1!=end_first; ++it1, ++pA, ++pB)
            {
#ifdef BOOST_MSVC
                if (*it1 == *it2)
#else
                if (*it1 == val2)
#endif
                    *pB = 1 + *(pA-1);
                else
                    *pB = std::max(*(pB-1), *pA);
            }

            std::swap(row1, row2);
        }

        size_type result = row1[min_size];
        alloc.deallocate(std::min(row1,row2), array_size);
        return result;
    }

}       // namespace boost

#endif  // LCS_H_
