
mtn_setup()

writefile("testfile1", "blah blah")
writefile("_MTN/testfile2", "blah blah")

check(indir("_MTN", mtn("add", "testfile2")), 0, false, true)
check(qgrep("testfile2", "stderr"))
check(indir("_MTN", mtn("add", "../testfile1")), 0, false, false)
