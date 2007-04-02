
include("common/netsync.lua")
mtn_setup()
netsync.setup()
revs = {}

addfile("testfile", "foo")
commit("1branch1")
revs[11] = base_revision()

remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."))
addfile("testfile", "bar")
commit("1branch2")
revs[12] = base_revision()

remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."))
addfile("testfile", "baz")
commit("2branch1")
revs[21] = base_revision()

srv = netsync.start()

-- check a glob
srv:pull("*anch2")
check(mtn2("automate", "get_revision", revs[11]), 1, false, false)
check(mtn2("automate", "get_revision", revs[12]), 0, false, false)
check(mtn2("automate", "get_revision", revs[21]), 1, false, false)

-- check explicit multiple branches
srv:pull({"1branch1", "2branch1"}, 3)
check(mtn3("automate", "get_revision", revs[11]), 0, false, false)
check(mtn3("automate", "get_revision", revs[12]), 1, false, false)
check(mtn3("automate", "get_revision", revs[21]), 0, false, false)

srv:stop()
