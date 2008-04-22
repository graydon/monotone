
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

-- At first drop the known key from the database where we pull to
-- and create a new key which has the same key id like the default
-- key used here for the netsync tests
check(mtn2("dropkey", "tester@test.net"), 0, false, false)
check(mtn2("genkey", "tester@test.net"), 0, false, false, "tester@test.net\ntester@test.net\n")

srv = netsync.start()

-- This should fail cleanly
srv:pull("testbranch", nil, 1)

-- Drop the errornous key
check(mtn2("dropkey", "tester@test.net"), 0, false, false)

-- And re-read the proper key into db2
check(get("../test_keys", "stdin"))
check(mtn2("read"), 0, false, false, true)

-- Now this should just work
srv:pull("testbranch")

srv:stop()

