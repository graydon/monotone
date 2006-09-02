
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

-- This is important, because if I only have read-only access to your
-- database, I shouldn't be able to clutter it with random epochs...

addfile("testfile", "some data")
commit()

remove("_MTN")
check(mtn2("setup", "--branch=testbranch", "."))
writefile("otherfile", "some data")
check(mtn2("add", "testfile"), 0, false, false)
check(mtn2("commit", "--message=foo", "--branch=testbranch.subbranch"), 0, false, false)

netsync.pull("testbranch.*")

check_different_stdout{"list", "epochs"}
