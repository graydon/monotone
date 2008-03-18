-- Test that sync uses _MTN/options properly
-- It ignored 'keydir' in 0.39, and did not set 'key' in development for 0.40

include("/common/netsync.lua")

-- 'mtn_setup' sets 'key' in _MTN/options; we don't want that.
check(getstd("test_keys"))
check(getstd("test_hooks.lua"))
check(getstd("min_hooks.lua"))

check(mtn("db", "init"), 0, false, false)
check(mtn("read", "test_keys"), 0, false, false)
remove("test_keys")

check(mtn_ws_opts("--db=test.db", "--keydir=keys", "setup", "--branch=testbranch", "."), 0, false, false)

netsync.setup()
srv = netsync.start(2)

-- This should find the single key in the keydir specified by
-- _MTN/options
check(mtn_ws_opts("sync", "--set-default", srv.address, "testbranch"), 0, false, false)

-- Add a key so sync can't tell which to use
check(mtn_ws_opts("genkey", "foo@bar"), 0, false, false, string.rep("foo@bar\n", 2))
check(mtn_ws_opts("sync"), 1, false, false)

-- Set the key in _MTN/options
check(mtn_ws_opts("sync", "--set-default", "--key=tester@test.net"), 0, false, false)

-- use it
check(mtn_ws_opts("sync"), 0, false, false)

srv:stop()

-- end of file

