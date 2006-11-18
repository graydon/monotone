
include("common/netsync.lua")
mtn_setup()
netsync.setup()

-- Now, commit something to transfer
writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
commit()

-- Hack _MTN/options
opts = readfile("_MTN/options")
writefile("_MTN/options", string.gsub(opts, 'key ".*"', 'key "foobar@hacked.com"'))
-- Double-check that _MTN/options was correctly hacked
check(not qgrep('key "tester@test.net"', "_MTN/options"))
check(qgrep('key "foobar@hacked.com"', "_MTN/options"))

srv = netsync.start()
srv:push({"testbranch", "--key=tester@test.net"})
srv:stop()
