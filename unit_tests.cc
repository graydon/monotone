
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdlib.h>

#include "unit_tests.hh"
#include "sanity.hh"

static bool clean_shutdown;
void dumper() 
{
  if (!clean_shutdown)
	global_sanity.dump_buffer();    
}

void clean_shutdown_dummy_test()
{
  clean_shutdown = true;
}

test_suite * init_unit_test_suite(int argc, char * argv[])
{
  clean_shutdown = false;
  atexit(&dumper);
  global_sanity.set_verbose();

  test_suite * suite = BOOST_TEST_SUITE("monotone unit tests");
  I(suite);

  // call all the adders here
  add_diff_patch_tests(suite);
  add_file_io_tests(suite);
  add_key_tests(suite);
  add_transform_tests(suite);
  add_vocab_tests(suite);
  add_packet_tests(suite);
  add_url_tests(suite);

  // all done, add our clean-shutdown-indicator
  suite->add(BOOST_TEST_CASE(&clean_shutdown_dummy_test));

  return suite;
}
