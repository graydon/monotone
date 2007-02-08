
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "version 0 data")
commit("testbranch")
ver = {}
ver[0] = base_revision()

netsync.pull("testbranch")

addfile("testfile2", "some data")
commit("testbranch")
ver[1] = base_revision()

revert_to(ver[0])
remove("testfile2")

writefile("testfile", "version 1 data")
commit("testbranch")
ver[2] = base_revision()

check(mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)
ver[3] = base_revision()

netsync.pull("testbranch")

check_same_stdout(mtn("automate", "graph"), mtn2("automate", "graph"))

for i = 1,3 do
  check_same_stdout(mtn("ls", "certs", ver[i]),
                    mtn2("ls", "certs", ver[i]))
end
