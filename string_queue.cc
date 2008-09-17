// unit tests for string_queue


#include "base.hh"
#include "string_queue.hh"

#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"

using std::logic_error;

UNIT_TEST(string_queue, string_queue)
{
  string_queue sq1;

  UNIT_TEST_CHECKPOINT( "append" );

  sq1.append("123");
  sq1.append("45z", 2); // 'z' shall be ignored
  sq1.append('6');

  UNIT_TEST_CHECK( sq1.size() == 6 );

  UNIT_TEST_CHECKPOINT( "retrieve" );

  UNIT_TEST_CHECK( sq1.substr(0, 6) == "123456" );
  UNIT_TEST_CHECK( sq1.substr(3, 2) == "45" );

  UNIT_TEST_CHECK( sq1[5] == '6' );
  UNIT_TEST_CHECK( sq1[0] == '1' );

  UNIT_TEST_CHECK( *(sq1.front_pointer(6)) == '1');

  UNIT_TEST_CHECK( sq1.size() == 6);

  UNIT_TEST_CHECKPOINT( "failures" );

  // check a few things will fail
  UNIT_TEST_CHECK_THROW( sq1.substr(3, 4), logic_error );
  UNIT_TEST_CHECK_THROW( sq1.front_pointer(7), logic_error );

  // modification
  UNIT_TEST_CHECKPOINT( "modification" );

  sq1[5] = 'r';
  UNIT_TEST_CHECK_THROW( sq1[6], logic_error );

  UNIT_TEST_CHECK( sq1[5] == 'r' );
  UNIT_TEST_CHECK( sq1.substr(3, 3) == "45r" );

  // empty it out
  UNIT_TEST_CHECKPOINT( "emptying" );

  UNIT_TEST_CHECK_THROW( sq1.pop_front( 7 ), logic_error );
  sq1.pop_front(1);
  UNIT_TEST_CHECK( sq1.size() == 5 );
  UNIT_TEST_CHECK(sq1[0] == '2');

  UNIT_TEST_CHECK(sq1[4] == 'r');
  UNIT_TEST_CHECK_THROW( sq1[5], logic_error );
  UNIT_TEST_CHECK_THROW( sq1.pop_front( 6 ), logic_error );
  sq1.pop_front(5);
  UNIT_TEST_CHECK_THROW( sq1.pop_front( 1 ), logic_error );

  // it's empty again
  UNIT_TEST_CHECK( sq1.empty() );
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
