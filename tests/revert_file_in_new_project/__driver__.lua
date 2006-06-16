
mtn_setup()


-- reverting one of one files
writefile("testfile0", "version 0 of first test file")

check(mtn("add", "testfile0"), 0, false, false)
check(mtn("revert", "testfile0"), 0, false, false)
check(mtn("status"), 0, true, false)
check(not qgrep("testfile0", "stdout"))


-- reverting one of two files
writefile("testfile0", "version 0 of first test file")
writefile("testfile1", "version 0 of second test file")

check(mtn("add", "testfile0", "testfile1"), 0, false, false)
check(mtn("revert", "testfile0"), 0, false, false)
check(mtn("status"), 0, true, false)
check(not qgrep("testfile0", "stdout"))
