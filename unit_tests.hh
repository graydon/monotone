#ifndef __UNIT_TESTS__
#define __UNIT_TESTS__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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
void add_numeric_vocab_tests(test_suite * suite);
void add_diff_patch_tests(test_suite * suite);
void add_file_io_tests(test_suite * suite);
void add_key_tests(test_suite * suite);
void add_transform_tests(test_suite * suite);
void add_charset_tests(test_suite * suite);
void add_simplestring_xform_tests(test_suite * suite);
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
void add_restrictions_tests(test_suite * suite);
void add_uri_tests(test_suite * suite);
void add_refiner_tests(test_suite * suite);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
