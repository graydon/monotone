// unit tests for string_queue

#include <string>

#include "string_queue.hh"

#ifdef BUILD_UNIT_TESTS

#include <iostream>
#include "unit_tests.hh"

using std::logic_error;

UNIT_TEST(string_queue, string_queue)
{
  string_queue sq1;

  BOOST_CHECKPOINT( "append" );

  sq1.append("123");
  sq1.append("45z", 2); // 'z' shall be ignored
  sq1.append('6');

  BOOST_CHECK( sq1.size() == 6 );

  BOOST_CHECKPOINT( "retrieve" );

  BOOST_CHECK( sq1.substr(0, 6) == "123456" );
  BOOST_CHECK( sq1.substr(3, 2) == "45" );

  BOOST_CHECK( sq1[5] == '6' );
  BOOST_CHECK( sq1[0] == '1' );

  BOOST_CHECK( *(sq1.front_pointer(6)) == '1');

  BOOST_CHECK( sq1.size() == 6);

  BOOST_CHECKPOINT( "failures" );

  // check a few things will fail
  BOOST_CHECK_THROW( sq1.substr(3, 4), logic_error );
  BOOST_CHECK_THROW( sq1.front_pointer(7), logic_error );

  // modification
  BOOST_CHECKPOINT( "modification" );

  sq1[5] = 'r';
  BOOST_CHECK_THROW( sq1[6], logic_error );

  BOOST_CHECK( sq1[5] == 'r' );
  BOOST_CHECK( sq1.substr(3, 3) == "45r" );

  // empty it out
  BOOST_CHECKPOINT( "emptying" );

  BOOST_CHECK_THROW( sq1.pop_front( 7 ), logic_error );
  sq1.pop_front(1);
  BOOST_CHECK( sq1.size() == 5 );
  BOOST_CHECK(sq1[0] == '2');

  BOOST_CHECK(sq1[4] == 'r');
  BOOST_CHECK_THROW( sq1[5], logic_error );
  BOOST_CHECK_THROW( sq1.pop_front( 6 ), logic_error );
  sq1.pop_front(5);
  BOOST_CHECK_THROW( sq1.pop_front( 1 ), logic_error );

  // it's empty again
  BOOST_CHECK( sq1.size() == 0 );
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
