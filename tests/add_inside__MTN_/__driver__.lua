
mtn_setup()

writefile("testfile1", "blah blah")
writefile("_MTN/testfile2", "blah blah")
writefile("testfile3", "blah blah BLAH")

check(indir("_MTN", mtn("add", "testfile2")), 1, false, true)
check(qgrep("ignored.*testfile2", "stderr"))
check(indir("_MTN", mtn("add", "../testfile1")), 0, false, false)
check(indir("_MTN", mtn("add", "testfile2", "../testfile3")), 0, false, true)
check(qgrep("ignored.*testfile2", "stderr"))
check(qgrep("adding.*testfile3", "stderr"))
