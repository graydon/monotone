
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("input.txt", "version 0 of the file")
commit()

-- check that tickers are quiet
srv = netsync.start()

check(mtn2("--rcfile=netsync.lua", "pull", srv.address, "testbranch", "--quiet"), 0, nil, true)
check(qgrep(': warning: ', "stderr"))

srv:stop()

-- check that warnings aren't...
-- (list keys with a pattern that doesn't match anything generates a warning)
check(mtn("--quiet", "list", "keys", "foo"), 0, false, true)
check(qgrep(': warning: ', "stderr"))
