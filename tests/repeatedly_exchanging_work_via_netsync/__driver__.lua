
mtn_setup()
netsync_setup()

addfile("testfile", "version 0 data")
commit("testbranch")
ver = {}
ver[0] = base_revision()

run_netsync("pull", "testbranch")

addfile("testfile2", "some data")
commit("testbranch")
ver[1] = base_revision()

revert_to(ver[0])

writefile("testfile", "version 1 data")
commit("testbranch")
ver[2] = base_revision()

check(cmd(mtn("--branch=testbranch", "merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
ver[3] = base_revision()

run_netsync("pull", "testbranch")

check_same_stdout(cmd(mtn("automate", "graph")), cmd(mtn2("automate", "graph")))

for i = 1,3 do
  check_same_stdout(cmd(mtn("ls", "certs", ver[i])),
                    cmd(mtn2("ls", "certs", ver[i])))
end
