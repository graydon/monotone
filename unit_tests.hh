#ifndef __UNIT_TESTS__
#define __UNIT_TESTS__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/test/unit_test_suite.hpp>
#include <boost/test/test_tools.hpp>

// strangely this was left out. perhaps it'll arrive later?
#ifndef BOOST_CHECK_NOT_THROW
#define BOOST_CHECK_NOT_THROW( statement, exception ) \
    try { statement; BOOST_CHECK_MESSAGE(true, "exception "#exception" did not occur" ); } \
    catch( exception const& ) { \
        BOOST_ERROR( "exception "#exception" occurred" ); \
    }
#endif

using boost::unit_test_framework::test_suite;

// list the various add-tests-to-the-testsuite functions here
void add_diff_patch_tests(test_suite * suite);
void add_file_io_tests(test_suite * suite);
void add_key_tests(test_suite * suite);
void add_transform_tests(test_suite * suite);
void add_vocab_tests(test_suite * suite);
void add_cset_tests(test_suite * suite);
void add_revision_tests(test_suite * suite);
void add_xdelta_tests(test_suite * suite);
void add_packet_tests(test_suite * suite);
void add_netcmd_tests(test_suite * suite);
void add_globish_tests(test_suite * suite);
void add_crypto_tests(test_suite * suite);
void add_string_queue_tests(test_suite * suite);
void add_pipe_tests(test_suite * suite);
void add_paths_tests(test_suite * suite);
void add_roster_tests(test_suite * suite);
void add_roster_merge_tests(test_suite * suite);

#endif
