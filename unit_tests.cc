// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <stdlib.h>

#include "botan/botan.h"

#include "unit_tests.hh"
#include "sanity.hh"

#include <set>
#include <string>

using std::set;
using std::string;

static bool clean_shutdown;
void dumper()
{
  if (!clean_shutdown)
        global_sanity.dump_buffer();
  Botan::Init::deinitialize();
}

void clean_shutdown_dummy_test()
{
  clean_shutdown = true;
}

test_suite * init_unit_test_suite(int argc, char * argv[])
{
  Botan::InitializerOptions botan_opt("thread_safe=0 selftest=1 seed_rng=1 "
                                    "use_engines=0 secure_memory=1 "
                                    "fips140=1");
  Botan::Init::initialize(botan_opt);

  clean_shutdown = false;
  atexit(&dumper);
  global_sanity.set_debug();

  test_suite * suite = BOOST_TEST_SUITE("monotone unit tests");
  I(suite);

  set<string> t;
  if (argc > 1)
    t = set<string>(argv+1, argv+argc);

  // call all the adders here

  if (t.empty() || t.find("file_io") != t.end())
    add_file_io_tests(suite);

  if (t.empty() || t.find("key") != t.end())
    add_key_tests(suite);

  if (t.empty() || t.find("transform") != t.end())
    add_transform_tests(suite);

  if (t.empty() || t.find("charset") != t.end())
    add_charset_tests(suite);

  if (t.empty() || t.find("simplestring_xform") != t.end())
    add_simplestring_xform_tests(suite);

  if (t.empty() || t.find("vocab") != t.end())
    add_vocab_tests(suite);

  if (t.empty() || t.find("revision") != t.end())
    add_revision_tests(suite);

  if (t.empty() || t.find("cset") != t.end())
    add_cset_tests(suite);

  if (t.empty() || t.find("diff_patch") != t.end())
    add_diff_patch_tests(suite);

  if (t.empty() || t.find("xdelta") != t.end())
    add_xdelta_tests(suite);

  if (t.empty() || t.find("packet") != t.end())
    add_packet_tests(suite);

  if (t.empty() || t.find("netcmd") != t.end())
    add_netcmd_tests(suite);

  if (t.empty() || t.find("globish") != t.end())
    add_globish_tests(suite);

  if (t.empty() || t.find("crypto") != t.end())
    add_crypto_tests(suite);

  if (t.empty() || t.find("pipe") != t.end())
    add_pipe_tests(suite);

  if (t.empty() || t.find("string_queue") != t.end())
    add_string_queue_tests(suite);

  if (t.empty() || t.find("paths") != t.end())
    add_paths_tests(suite);

  if (t.empty() || t.find("roster") != t.end())
    add_roster_tests(suite);

  if (t.empty() || t.find("roster_merge") != t.end())
    add_roster_merge_tests(suite);

  if (t.empty() || t.find("restrictions") != t.end())
    add_restrictions_tests(suite);

  if (t.empty() || t.find("uri") != t.end())
    add_uri_tests(suite);

  if (t.empty() || t.find("refiner") != t.end())
    add_refiner_tests(suite);

  // all done, add our clean-shutdown-indicator
  suite->add(BOOST_TEST_CASE(&clean_shutdown_dummy_test));

  return suite;
}

// Stub for options.cc's sake.
void
localize_monotone()
{
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
