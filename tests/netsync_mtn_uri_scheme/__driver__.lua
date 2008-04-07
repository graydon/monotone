
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("file1", "foo")
commit("branch")

addfile("file2", "bar")
commit("branch-test")

addfile("file3", "baz")
commit("branch-test-exclude")


srv = netsync.start()

-- %61 = 'a'
check(mtn2("pull", "mtn://" .. srv.address .. "?br%61nch-te*/-br%61nch-test-*"), 0, false, false)
check(mtn2("ls", "branches"), 0, true)
check(not qgrep("^branch$", "stdout"))
check(    qgrep("^branch-test$", "stdout"))
check(not qgrep("^branch-test-exclude$", "stdout"))

check(mtn2("pull", "mtn://" .. srv.address .. "?include=*"), 0, false, false)
check(mtn2("ls", "branches"), 0, true)
check(qgrep("^branch$", "stdout"))
check(qgrep("^branch-test$", "stdout"))
check(qgrep("^branch-test-exclude$", "stdout"))

srv:stop()
