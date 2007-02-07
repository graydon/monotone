
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "1")
commit("branch1")
B11=base_revision()

writefile("testfile", "2")
commit("branch2")
B21=base_revision()


netsync.pull({'branch*', "--exclude=branch2"})
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 1, false, false)

revert_to(B11)
writefile("testfile", "12")
commit("branch1")
B12=base_revision()

revert_to(B21)
writefile("testfile", "21")
commit("branch2")
B22=base_revision()

netsync.pull()
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 1, false, false)
check(mtn2("automate", "get_revision", B12), 0, false, false)
check(mtn2("automate", "get_revision", B22), 1, false, false)

-- passing only an include pattern causes the default exclude to be
-- ignored...
netsync.pull("branch2")
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 0, false, false)
check(mtn2("automate", "get_revision", B12), 0, false, false)
check(mtn2("automate", "get_revision", B22), 0, false, false)

-- but not set by default

revert_to(B12)
writefile("testfile", "13")
commit("branch1")
B13=base_revision()

revert_to(B22)
writefile("testfile", "23")
commit("branch2")
B23=base_revision()

netsync.pull()
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 0, false, false)
check(mtn2("automate", "get_revision", B12), 0, false, false)
check(mtn2("automate", "get_revision", B22), 0, false, false)
check(mtn2("automate", "get_revision", B13), 0, false, false)
check(mtn2("automate", "get_revision", B23), 1, false, false)

-- but --set-default overrides

srv = netsync.start()
srv:pull({"--set-default", 'branch*'})
srv:stop()
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 0, false, false)
check(mtn2("automate", "get_revision", B12), 0, false, false)
check(mtn2("automate", "get_revision", B22), 0, false, false)
check(mtn2("automate", "get_revision", B13), 0, false, false)
check(mtn2("automate", "get_revision", B23), 0, false, false)

revert_to(B13)
writefile("testfile", "14")
commit("branch1")
B14=base_revision()

revert_to(B23)
writefile("testfile", "24")
commit("branch2")
B24=base_revision()

netsync.pull()
check(mtn2("automate", "get_revision", B11), 0, false, false)
check(mtn2("automate", "get_revision", B21), 0, false, false)
check(mtn2("automate", "get_revision", B12), 0, false, false)
check(mtn2("automate", "get_revision", B22), 0, false, false)
check(mtn2("automate", "get_revision", B13), 0, false, false)
check(mtn2("automate", "get_revision", B23), 0, false, false)
check(mtn2("automate", "get_revision", B14), 0, false, false)
check(mtn2("automate", "get_revision", B24), 0, false, false)
