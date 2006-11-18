
include("common/netsync.lua")
mtn_setup()
netsync.setup()

-- If the sink has more epochs than the source (say, on branches that are
-- included in the include glob, but only exist on the sink), then the epoch
-- refiner on the source will say we have items to receive. But since we're in
-- source-only mode, we won't actually be receiving any items. So our epoch
-- refiner will forever say we have items to receive, and we don't want to hang
-- forever waiting for them.

addfile("foo", "foo")
commit()
check(mtn("db", "set_epoch", "testbranch", string.rep("1234567890", 4)))
remove("_MTN")
check(mtn2("setup", "-b", "otherbranch", "."), 0, false, false)
writefile("bar", "bar")
check(mtn2("add", "bar"), 0, false, false)
check(mtn2("commit", "-m", "blah-blah"), 0, false, false)

srv = netsync.start()

-- We don't want the standard function, because we don't want to hang if it hangs.
-- srv.push("*branch")
cli = bg(mtn2("--rcfile=netsync.lua", "push", srv.address, "*branch"), 0, false, false)
check(cli:wait(20)) -- give it 20 seconds.
srv:stop()

check(mtn("ls", "branches"), 0, true, false)
check(qgrep("testbranch", "stdout"), 0, false, false)
check(qgrep("otherbranch", "stdout"), 0, false, false)
