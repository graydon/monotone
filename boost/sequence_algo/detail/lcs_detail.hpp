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
#ifndef LCS_DETAIL_H_
#define LCS_DETAIL_H_


namespace boost {
    namespace detail {

        // recursive lcs algorithm
        template<typename size_type,
                 typename ItIn1,
                 typename ItIn2,
                 typename ItSubSeq>
        size_type linear_space_lcs(ItIn1      begin_first,
                                   ItIn1      end_first,
                                   ItIn2      begin_second,
                                   ItIn2      end_second,
                                   ItSubSeq   subsequence,
                                   size_type *pA)
        {
#ifdef BOOST_STATIC_ASSERT  // Not supported for BCC 5.5.1
            BOOST_STATIC_ASSERT(boost::is_integral<size_type>::value);
#endif
            std::ptrdiff_t col, row;

            // calculate the length of each subsequence
            std::ptrdiff_t size_first  = std::distance<>(begin_first, end_first);
            std::ptrdiff_t size_second = std::distance<>(begin_second, end_second);

#ifdef DBG_SCOPE
            DBG_SCOPE("linear_space_lcs: Sequence lengths %lu and %lu", size_first, size_second);
#endif

            // the algorithm will use less memory if the second sequence is longer
            // than the first, so we can swap the input iterators
            if (size_first > size_second)
            {
                std::swap(begin_first, begin_second);
                std::swap(end_first, end_second);
                std::swap(size_first, size_second);
            }
            // if both sequences have a single element, then we can compare the
            // element for equally immediately and save ourselves some time
            else if (size_first == 1  &&  size_second == 1)
            {
                if (*begin_first == *begin_second)
                {
                    *subsequence++ = *begin_first;
                    return 1;
                }

                return 0;
            }

            // initialise working array
            const std::ptrdiff_t array_size = (size_first + 1) * sizeof(size_type) * 3;
            memset(pA, 0, array_size);
            size_type      *pB = pA + size_first + 1;
            std::ptrdiff_t *pO = reinterpret_cast<std::ptrdiff_t *>(pB + size_first + 1);

            // we need to track the position in middle row that each entry
            // comes from, so create another working array and initial each
            // member to contains it's index
            for (col=0; col<=size_first; ++col)
                pO[col] = col;

            // work down the imaginary array, until the 'middle' row is reached
            ItIn2 it2 = begin_second;
            for (row=1; row<=(size_second>>1); ++row, ++it2)
            {
                ItIn1 it1 = begin_first;
                for (col=1; col<=size_first; ++col, ++it1)
                {
                    if (*it1 == *it2)
                        pB[col] = 1 + pA[col-1];
                    else
                        pB[col] = std::max(pB[col-1], pA[col]);
                }
                std::swap(pA, pB);
            }
                
            // now continue processing the rest of the imaginary array
            for (; row<=size_second; ++row, ++it2)
            {
                ItIn1 it1 = begin_first;
                for (col=1; col<=size_first; ++col, ++it1)
                {
                    if (*it1 == *it2)
                    {
                        pB[col] = 1 + pA[col-1];
                        pO[col] = pO[col-1];
                    }
                    else if (pB[col-1] > pA[col])
                    {
                        pB[col] = pB[col-1];
                        pO[col] = pO[col-1];
                    }
                    else
                        pB[col] = pA[col];
                }
                std::swap(pA, pB);
            }

            // now we have reached the end of the imaginary array, the last entry
            // in pA contains the length of the subsequence and the last entry
            // in pO contains the position in the middle row that the last entry
            // came from. This is the middle node along the solution path.
            size_type lcs = pA[size_first];
            if (lcs > 0)
            {
                if (size_second > 2  &&  pO[size_first] > 0)
                {
                    ItIn1 left_it   = begin_first;
                    ItIn1 left_ite  = begin_first;
                    std::advance<>(left_ite, pO[size_first]);

                    ItIn2 right_it  = begin_second;
                    ItIn2 right_ite = begin_second;
                    std::advance<>(right_ite, size_second>>1);

                    if ((size_second>>1) > 0  &&  pO[size_first] > 0)
                    {
                        linear_space_lcs<size_type>(left_it, left_ite,
                                                    right_it, right_ite,
                                                    subsequence, std::min(pA,pB));
                    }

                    if (left_ite != end_first  &&  right_ite != end_second)
                    {
                        linear_space_lcs<size_type>(left_ite, end_first,
                                                    right_ite, end_second,
                                                    subsequence, std::min(pA,pB));
                    }
                }
                else
                {
                    // Note that pA and pB have been swapped so pB is actually
                    // the top most row
                    ItIn1     it1 = begin_first;
                    size_type val = 1;
                    for (col=1; col<=size_first; ++col, ++it1)
                    {
                        if (pA[col] == pB[col-1]+1  &&  pA[col] == val)
                        {
                            *subsequence++ = *it1;
                            ++val;
                        }
                    }
                }
            }

            return lcs;
        }

    }       // namespace detail
}           // namespace boost

#endif              // LCS_DETAIL_H_
