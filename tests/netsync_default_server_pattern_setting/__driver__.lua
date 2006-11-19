
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "blah blah")
commit()

srv = netsync.start()
srv:pull("testbranch")

srv:stop()

writefile("testfile", "other stuff")
commit()
rev = base_revision()

-- Having done an explicit pull once, future ones should default to the
-- same
srv:restart()
check(mtn2("pull"), 0, false, false)
srv:finish()

check(mtn2("checkout", "--revision", rev, "testdir"), 0, false, false)
check(samefile("testfile", "testdir/testfile"))
