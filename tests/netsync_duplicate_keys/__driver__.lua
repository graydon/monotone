
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

srv = netsync.start(2)

check(mtn("genkey", "committer@test.net"), 0, false, false,
       "committer@test.net\ncommitter@test.net\n")

writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
check(mtn("ci", "-mx", "-k", "committer@test.net"), 0, false, false)

check(mtn("au", "select", "b:testbranch"), 0, true)

srv:push("testbranch", 1)


check(mtn("dropkey", "committer@test.net"), 0, false, false)
check(mtn("genkey", "committer@test.net"), 0, false, false,
       "committer@test.net\ncommitter@test.net\n")

writefile("testfile", "version 1 of test file")
check(mtn("ci", "-mx", "-k", "committer@test.net"), 0, false, false)

srv:push("testbranch", 1, 1)

srv:stop()

check(mtn2("au", "select", "b:testbranch"), 0, {"stdout"})
