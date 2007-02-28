
mtn_setup()

mkdir("adddir")
writefile("adddir/test.txt", "A test file that won't be added unless --recursive is used\n")

check(mtn("add", "--unknown"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(not qgrep("adddir/test.txt", "stdout"))
check(mtn("drop", "--bookkeep-only", "adddir"), 0, true, false)
check(mtn("add", "--unknown", "--recursive"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("adddir/test.txt", "stdout"))
