
include("/common/netsync.lua")
mtn_setup()

netsync.setup()

check(get("read-permissions"))
check(get("write-permissions"))

srv = netsync.start({"--confdir=."}, nil, false)

-- Try pushing just one exact branch
addfile("testfile1", "test file 1")
commit("testbranch", "testfile")
srv:push("testbranch")

-- Try pushing just branches matching a wild card
addfile("testfile2", "test file 2")
commit("testbranch", "testfile")
srv:push("testbra*")

srv:stop()
