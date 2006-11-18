
include("/common/netsync.lua")
mtn_setup()
netsync.setup()
revs = {}

addfile("testfile", "blah blah")
commit()
revs.testbranch = base_revision()

writefile("testfile", "stuff stuff")
commit("otherbranch")
revs.otherbranch = base_revision()

writefile("testfile", "nonsense nonsense")
commit("thirdbranch")
revs.thirdbranch = base_revision()

srv = netsync.start()

-- First make sure netsync with explicit server/pattern override defaults
check(mtn2("set", "database", "default-server", "nonsense"), 0, false, false)
check(mtn2("set", "database", "default-include-pattern", "nonsense"), 0, false, false)
srv:pull("testbranch")
check(mtn2("checkout", "--branch=testbranch", "--revision", revs.testbranch, "testdir1"), 0, false, false)
check(exists("testdir1/testfile"))

-- Now make sure explicit server with default pattern works...
check(mtn2("set", "database", "default-server", "nonsense"), 0, false, false)
check(mtn2("set", "database", "default-include-pattern", "otherbranch"), 0, false, false)
srv:pull()
check(mtn2("checkout", "--branch=otherbranch", "--revision", revs.otherbranch, "testdir2"), 0, false, false)
check(exists("testdir2/testfile"))

-- And finally, make sure that passing nothing at all also works (uses default)
check(mtn2("set", "database", "default-server", srv.address), 0, false, false)
check(mtn2("set", "database", "default-include-pattern", "thirdbranch"), 0, false, false)
check(mtn2("sync"), 0, false, false)
check(mtn2("checkout", "--branch=thirdbranch", "--revision", revs.thirdbranch, "testdir3"), 0, false, false)
check(exists("testdir3/testfile"))

srv:finish()
