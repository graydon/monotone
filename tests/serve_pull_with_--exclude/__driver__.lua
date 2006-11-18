
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "1")
commit("branch1")
B1=base_revision()

writefile("testfile", "2")
commit("branch2")
B2=base_revision()

addfile("testfile", "3")
commit("branch3")
B3=base_revision()

writefile("testfile", "4")
commit("branch4")
B4=base_revision()

-- Serve excluding branch2, branch4
-- attempting to pull them should fail
-- pulling everything but them should give revs B1, B2, B3; and only
-- give branch certs on B1, B3.
get("read-permissions")

srv = netsync.start()

-- it is apparently a permissions error to pull a branch that is not served
-- i.e. 'received network error: access to branch 'branch2' denied by server'

srv:pull("branch2", nil, 1)
srv:pull("branch4", nil, 1)
check(mtn2("automate", "get_revision", B1), 1, false, false)
check(mtn2("automate", "get_revision", B2), 1, false, false)
check(mtn2("automate", "get_revision", B3), 1, false, false)
check(mtn2("automate", "get_revision", B4), 1, false, false)

srv:pull({'branch*', "--exclude=branch2", "--exclude=branch4"})
check(mtn2("automate", "get_revision", B1), 0, false, false)
check(mtn2("automate", "get_revision", B2), 0, false, false)
check(mtn2("automate", "get_revision", B3), 0, false, false)
check(mtn2("automate", "get_revision", B4), 1, false, false)

check(mtn2("ls", "certs", B2), 0, true)
check(not qgrep("branch2", "stdout"))

srv:stop()
