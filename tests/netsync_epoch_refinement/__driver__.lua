include("common/netsync.lua")
mtn_setup()
netsync.setup()

-- If we already have epochs for branches we don't have any revisions for,
-- we might get sent those epochs. Make sure that our already having them
-- doesn't prevent us from noting that we received them.

addfile("foo", "foo")
commit()
check(mtn("db", "set_epoch", "testbranch", string.rep("1234567890", 4)))
check(mtn2("db", "set_epoch", "testbranch", string.rep("1234567890", 4)))

-- If we completely ignore the epoch (because we already have it), we'll
-- never notice that refinement is done.
srv = netsync.start()
cli = bg(mtn2("--rcfile=netsync.lua", "sy", srv.address, "testbranch"), 0, false, false)
check(cli:wait(20)) -- give it 20 seconds.
srv:stop()

check(mtn("ls", "branches"), 0, true, false)
check(qgrep("testbranch", "stdout"), 0, false, false)
