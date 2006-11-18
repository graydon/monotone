
include("/common/netsync.lua")
mtn_setup()
netsync.setup()
revs = {}

writefile("testfile", "some data")
check(mtn2("add", "testfile"), 0, false, false)
check(mtn2("commit", "--message=foo", "--branch=testbranch"), 0, false, false)
revs[0] = base_revision()

netsync.push("testbranch")

-- check that the first db got our epoch
check_same_stdout{"list", "epochs"}

-- check that epochs are only sent for netsync'ed branches
check(mtn("list", "epochs"), 0, true, false)
rename("stdout", "orig-epochs")
writefile("otherfile", "other data")
check(mtn2("add", "otherfile"), 0, false, false)
check(mtn2("commit", "--message=foo", "--branch=otherbranch"), 0, false, false)
-- Run an irrelevant netsync, just to force epochs to be regenerated
srv = netsync.start(2)
srv:sync("otherbranch", 3)
srv:finish()
-- Run the real netsync
netsync.push("testbranch")
check(mtn("list", "epochs"), 0, true, false)
rename("stdout", "new-epochs")
check(samefile("orig-epochs", "new-epochs"))
check_different_stdout{"list", "epochs"}

writefile("testfile", "new version of data")
check(mtn2("commit", "--message=foo", "--branch=testbranch"), 0, false, false)
revs[1] = base_revision()

-- change the epochs in the second db
check(mtn("db", "set_epoch", "testbranch", string.rep("a", 40)), 0, false, false)
check(mtn("db", "set_epoch", "otherbranch", string.rep("a", 40)), 0, false, false)

-- this should *fail* to sync 
srv = netsync.start()
-- This should probably expect an exit value of 1, but ATM the netsync
-- client doesn't report errors in its exit value.
-- srv:pull("testbranch", 2, 1)
srv:pull("testbranch")
srv:finish()

check(mtn("list", "epochs"), 0, true)
check(qgrep("testbranch", "stdout"))
check(mtn2("list", "epochs"), 0, true)
check(qgrep("testbranch", "stdout"))
check_different_stdout{"list", "epochs"}

-- confirm, we did not get the new revision
check(mtn("automate", "get_revision", revs[1]), 1, false, false)
