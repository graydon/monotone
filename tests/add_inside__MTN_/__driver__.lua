
mtn_setup()

writefile("testfile1", "blah blah")
writefile("_MTN/testfile2", "blah blah")

check(indir("_MTN", mtn("add", "testfile2")), 1, false, false)
check(indir("_MTN", mtn("add", "../testfile1")), 0, false, false)
