
mtn_setup()

writefile("testfile", "This is complete junk")
check(mtn("--branch=testbranch", "add", "testfile"), 1, false, false)
check(mtn("add", "testfile"), 0, false, false)
commit()

check(mtn("--branch=testbranch", "--last=1", "log"), 1, false, false)
check(mtn("--last=1", "log"), 0, false, false)

check(mtn("--depth=0", "status"), 0, false, false)
check(mtn("--depth=0", "pubkey", "tester@test.net"), 1, false, false)

-- command-specific options with non-existent commands give correct error
-- message:
check(mtn("--branch=testbranch", "loggg"), 1, "", true)
output = readfile("stderr")
-- 'not not' means 'coerce to bool'
check(string.find(output, "unknown command") ~= nil)
