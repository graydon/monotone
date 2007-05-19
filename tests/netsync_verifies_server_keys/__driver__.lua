
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

check(mtn("genkey", "foo@bar"), 0, false, false, "foo@bar\nfoo@bar\n")

-- Once to let the client note down the key...
-- We need to set the random seed, because each time a server is
-- started it picks a new random port.
seed = get_pid()
math.randomseed(seed)
netsync.pull("testbranch")

-- Then again with a different key; should fail.
math.randomseed(seed)
srv = netsync.start{"--key=foo@bar"}

srv:pull("testbranch", nil, 1)
-- It shouldn't have absorbed the key, either.
check(mtn2("pubkey", "foo@bar"), 1, true, false)

-- But if we then clear the client's known_hosts entry, it should be fine
check(mtn2("unset", "known-servers", srv.address), 0, false, false)

-- Now it should succeed
srv:pull("testbranch")
-- And have absorbed the key
check(mtn2("pubkey", "foo@bar"), 0, true, false)

srv:stop()
