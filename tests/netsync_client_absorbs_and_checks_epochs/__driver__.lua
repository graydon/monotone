
include("/common/netsync.lua")
mtn_setup()
netsync.setup()
revs = {}

addfile("testfile", "version 0 data")
commit()
revs[0] = base_revision()

netsync.pull("testbranch")

-- check that the second db got our epoch
check_same_stdout(mtn("list", "epochs"), mtn2("list", "epochs"))

-- check that epochs are only sent for netsync'ed branches
check(mtn2("list", "epochs"), 0, true, false)
rename("stdout", "orig-epochs")
addfile("testfile2", "other data")
commit("otherbranch")
-- Run an irrelevant netsync, just to force epochs to be regenerated
srv = netsync.start()
srv:sync("otherbranch", 3)
srv:finish()
-- Run the real netsync
netsync.pull()
check(mtn2("list", "epochs"), 0, true, false)
rename("stdout", "new-epochs")
check(samefile("orig-epochs", "new-epochs"))
check_different_stdout(mtn("list", "epochs"), mtn2("list", "epochs"))

addfile("testfile3", "some data")
commit()
revs[1] = base_revision()

-- change the epochs in the first db
check(mtn("db", "set_epoch", "testbranch", "12345"), 1, false, false)
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
check_different_stdout(mtn("list", "epochs"), mtn2("list", "epochs"))

-- confirm, we did not get the new revision
check(mtn2("automate", "get_revision", revs[1]), 1, false, false)
