
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdlib.h>

#include "botan/botan.h"

#include "unit_tests.hh"
#include "sanity.hh"

#include <set>
#include <string>

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
  Botan::Init::initialize();

  clean_shutdown = false;
  atexit(&dumper);
  global_sanity.set_debug();

  test_suite * suite = BOOST_TEST_SUITE("monotone unit tests");
  I(suite);

  std::set<std::string> t;
  if (argc > 1)
    t = std::set<std::string>(argv+1, argv+argc);

  // call all the adders here

  if (t.empty() || t.find("file_io") != t.end())
    add_file_io_tests(suite);
  
  if (t.empty() || t.find("key") != t.end())
    add_key_tests(suite);
  
  if (t.empty() || t.find("transform") != t.end())
    add_transform_tests(suite);
  
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
  
  if (t.empty() || t.find("string_queue") != t.end())
    add_string_queue_tests(suite);  
  
  if (t.empty() || t.find("paths") != t.end())
    add_paths_tests(suite);  

  if (t.empty() || t.find("roster") != t.end())
    add_roster_tests(suite);  
  
  if (t.empty() || t.find("roster_merge") != t.end())
    add_roster_merge_tests(suite);

  // all done, add our clean-shutdown-indicator
  suite->add(BOOST_TEST_CASE(&clean_shutdown_dummy_test));

  return suite;
}
