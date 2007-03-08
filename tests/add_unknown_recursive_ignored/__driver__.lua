
mtn_setup()

mkdir("adddir")
writefile("adddir/test.txt", "A test file that won't be added unless --recursive is used\n")
writefile("adddir/fake_test_hooks.lua", "A test file that won't be added unless --recursive and --no-respect-ignore are used\n")

check(mtn("add", "--unknown", "--recursive"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("adddir/test.txt", "stdout"))
check(not qgrep("adddir/fake_test_hooks.lua", "stdout"))
check(mtn("drop", "--bookkeep-only", "adddir/test.txt"), 0, true, false)
check(mtn("drop", "--bookkeep-only", "adddir"), 0, true, false)
check(mtn("add", "--unknown", "--recursive", "--no-respect-ignore"), 0, false, false)
check(mtn("ls", "known"), 0, true, false)
check(qgrep("adddir/fake_test_hooks.lua", "stdout"))
