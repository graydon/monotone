//  Boost io/streambuf_wrapping.hpp header file  -----------------------------//

//  (C) Copyright Daryle Walker 2002.  Permission to copy, use, modify, sell and
//  distribute this software is granted provided this copyright notice appears 
//  in all copies.  This software is provided "as is" without express or implied 
//  warranty, and with no claim as to its suitability for any purpose. 

//  See http://www.boost.org for updates, documentation, and revision history. 

#ifndef BOOST_IO_STREAMBUF_WRAPPING_HPP
#define BOOST_IO_STREAMBUF_WRAPPING_HPP

#include <boost/io_fwd.hpp>  // self include

#include <boost/utility/base_from_member.hpp>  // for boost::base_from_member

#include <ios>      // for std::basic_ios
#include <istream>  // for std::basic_istream and std::basic_iostream
#include <ostream>  // for std::basic_ostream


namespace boost
{
namespace io
{


//  Implementation detail stuff  ---------------------------------------------//

namespace detail
{

template < class StreamBuf >
class basic_wrapping_ios
    : private ::boost::base_from_member< StreamBuf >
    , virtual public ::std::basic_ios< typename StreamBuf::char_type,
       typename StreamBuf::traits_type >
{
    typedef ::boost::base_from_member< StreamBuf >  pbase_type;

    typedef ::std::basic_ios< typename StreamBuf::char_type,
     typename StreamBuf::traits_type >  base_type;

public:
    // Types
    typedef StreamBuf  streambuf_type;

    typedef typename StreamBuf::char_type    char_type;
    typedef typename StreamBuf::traits_type  traits_type;

    // Accessors
    streambuf_type *
    get_internal_streambuf()
    {
        return &this->pbase_type::member;
    }

    streambuf_type const *
    get_internal_streambuf() const
    {
        return &this->pbase_type::member;
    }

    bool
    is_using_internal_streambuf() const
    {
        return this->get_internal_streambuf() == this->base_type::rdbuf();
    }

protected:
    // Constructors
    explicit  basic_wrapping_ios( streambuf_type const &s )
        : pbase_type( s )
    {
    }

    basic_wrapping_ios()
        : pbase_type()
    {
    }

    template< typename T1 >
    explicit  basic_wrapping_ios( T1 x1 )
        : pbase_type( x1 )
    {
    }

    template< typename T1, typename T2 >
    basic_wrapping_ios( T1 x1, T2 x2 )
        : pbase_type( x1, x2 )
    {
    }

    template< typename T1, typename T2, typename T3 >
    basic_wrapping_ios( T1 x1, T2 x2, T3 x3 )
        : pbase_type( x1, x2, x3 )
    {
    }

};  // boost::io::detail::basic_wrapping_ios

}  // namespace detail

//  Streambuf-wrapping stream class template declarations  -------------------//

template < class StreamBuf >
class basic_wrapping_istream
    : public detail::basic_wrapping_ios< StreamBuf >
    , public ::std::basic_istream< typename StreamBuf::char_type,
       typename StreamBuf::traits_type >
{
    typedef detail::basic_wrapping_ios< StreamBuf >  base1_type;

    typedef ::std::basic_istream< typename StreamBuf::char_type,
     typename StreamBuf::traits_type >  base2_type;

public:
    // Types
    typedef StreamBuf  streambuf_type;

    typedef typename StreamBuf::char_type    char_type;
    typedef typename StreamBuf::traits_type  traits_type;

protected:
    // Constructors
    explicit  basic_wrapping_istream( streambuf_type const &s );

    basic_wrapping_istream();

    template< typename T1 >
    explicit  basic_wrapping_istream( T1 x1 )
        : base1_type( x1 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2 >
    basic_wrapping_istream( T1 x1, T2 x2 )
        : base1_type( x1, x2 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2, typename T3 >
    basic_wrapping_istream( T1 x1, T2 x2, T3 x3 )
        : base1_type( x1, x2, x3 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

};  // boost::io::basic_wrapping_istream

template < class StreamBuf >
class basic_wrapping_ostream
    : public detail::basic_wrapping_ios< StreamBuf >
    , public ::std::basic_ostream< typename StreamBuf::char_type,
       typename StreamBuf::traits_type >
{
    typedef detail::basic_wrapping_ios< StreamBuf >  base1_type;

    typedef ::std::basic_ostream< typename StreamBuf::char_type,
     typename StreamBuf::traits_type >  base2_type;

public:
    // Types
    typedef StreamBuf  streambuf_type;

    typedef typename StreamBuf::char_type    char_type;
    typedef typename StreamBuf::traits_type  traits_type;

protected:
    // Constructors
    explicit  basic_wrapping_ostream( streambuf_type const &s );

    basic_wrapping_ostream();

    template< typename T1 >
    explicit  basic_wrapping_ostream( T1 x1 )
        : base1_type( x1 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2 >
    basic_wrapping_ostream( T1 x1, T2 x2 )
        : base1_type( x1, x2 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2, typename T3 >
    basic_wrapping_ostream( T1 x1, T2 x2, T3 x3 )
        : base1_type( x1, x2, x3 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

};  // boost::io::basic_wrapping_ostream

template < class StreamBuf >
class basic_wrapping_iostream
    : public detail::basic_wrapping_ios< StreamBuf >
    , public ::std::basic_iostream< typename StreamBuf::char_type,
       typename StreamBuf::traits_type >
{
    typedef detail::basic_wrapping_ios< StreamBuf >  base1_type;

    typedef ::std::basic_iostream< typename StreamBuf::char_type,
     typename StreamBuf::traits_type >  base2_type;

public:
    // Types
    typedef StreamBuf  streambuf_type;

    typedef typename StreamBuf::char_type    char_type;
    typedef typename StreamBuf::traits_type  traits_type;

protected:
    // Constructors
    explicit  basic_wrapping_iostream( streambuf_type const &s );

    basic_wrapping_iostream();

    template< typename T1 >
    explicit  basic_wrapping_iostream( T1 x1 )
        : base1_type( x1 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2 >
    basic_wrapping_iostream( T1 x1, T2 x2 )
        : base1_type( x1, x2 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

    template< typename T1, typename T2, typename T3 >
    basic_wrapping_iostream( T1 x1, T2 x2, T3 x3 )
        : base1_type( x1, x2, x3 )
        , base2_type( this->base1_type::get_internal_streambuf() )
    {
    }

};  // boost::io::basic_wrapping_iostream


//  Streambuf-wrapping stream class template member function definitions  ----//

template < class StreamBuf >
inline
basic_wrapping_istream<StreamBuf>::basic_wrapping_istream
(
    streambuf_type const &  s
)
    : base1_type( s )
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}

template < class StreamBuf >
inline
basic_wrapping_istream<StreamBuf>::basic_wrapping_istream
(
)
    : base1_type()
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}

template < class StreamBuf >
inline
basic_wrapping_ostream<StreamBuf>::basic_wrapping_ostream
(
    streambuf_type const &  s
)
    : base1_type( s )
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}

template < class StreamBuf >
inline
basic_wrapping_ostream<StreamBuf>::basic_wrapping_ostream
(
)
    : base1_type()
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}

template < class StreamBuf >
inline
basic_wrapping_iostream<StreamBuf>::basic_wrapping_iostream
(
    streambuf_type const &  s
)
    : base1_type( s )
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}

template < class StreamBuf >
inline
basic_wrapping_iostream<StreamBuf>::basic_wrapping_iostream
(
)
    : base1_type()
    , base2_type( this->base1_type::get_internal_streambuf() )
{
}


}  // namespace io
}  // namespace boost


#endif  // BOOST_IO_STREAMBUF_WRAPPING_HPP
